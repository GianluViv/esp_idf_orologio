#include "imu.h"

#include "esp_log.h"
#include "i2c_bus.h"

static const char *TAG = "imu";

/* Indirizzo I2C a 7 bit del QMI8658C. Dipende dal pin SA0 del chip: se
 * lasciato flottante (pull-down interno debole) l'indirizzo e' 0x6A, se
 * collegato a VDDIO diventa 0x6B (vedi datasheet QMI8658C.pdf, §12.2).
 * Non conoscendo con certezza il cablaggio di questa revisione della
 * board, proviamo prima 0x6A e usiamo 0x6B come ripiego. */
#define IMU_I2C_ADDR_PRIMARY   0x6A
#define IMU_I2C_ADDR_SECONDARY 0x6B

/* Indirizzi dei registri usati (vedi datasheet, Table 20 "UI Register
 * Overview" e Table 27 "Sensor Data Output Registers Description"). */
#define REG_WHO_AM_I 0x00
#define REG_CTRL1    0x02
#define REG_CTRL2    0x03
#define REG_CTRL3    0x04
#define REG_CTRL7    0x08
#define REG_TEMP_L   0x33
#define REG_GZ_H     0x40
#define SENSOR_DATA_REGS_COUNT (REG_GZ_H - REG_TEMP_L + 1) // 14 registri: TEMP_L..GZ_H

#define QMI8658_WHO_AM_I_VALUE 0x05

/* Fondo scala scelto per accelerometro (+-4g) e giroscopio (+-512 dps): un
 * compromesso ragionevole fra sensibilita' e range per un dispositivo
 * indossabile/da scrivania, che non subisce urti violenti ma di cui vale
 * la pena leggere bene anche piccoli movimenti. I fattori di sensibilita'
 * (LSB per unita' fisica) sono presi dalle tabelle "Sensitivity" del
 * datasheet (§3.4, Table 7 e Table 8) e dipendono dal fondo scala scelto:
 * se si cambia aFS/gFS qui sotto, vanno aggiornati anche questi divisori
 * in imu_read(). */
#define ACCEL_FS_SEL   0x1 // 001 = +-4g
#define ACCEL_SENSITIVITY_LSB_PER_G 8192.0f
#define GYRO_FS_SEL    0x5 // 101 = +-512 dps
#define GYRO_SENSITIVITY_LSB_PER_DPS 64.0f

/* Output Data Rate: 250 campioni al secondo, ben oltre la velocita' con
 * cui la dashboard LVGL rilegge i dati (500 ms, vedi sensor_dashboard.c):
 * non serve una ODR piu' alta, ci limiteremmo solo a rileggere piu' volte
 * lo stesso campione. Codice 0101 in tabella "Set Accelerometer/Gyroscope
 * Output Data Rate (ODR)" del datasheet (§5.4). */
#define ACCEL_GYRO_ODR_SEL 0x5 // 250 Hz

static i2c_master_dev_handle_t s_imu_dev = NULL;

void imu_init(void) {
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();

    uint8_t addr = IMU_I2C_ADDR_PRIMARY;
    if (i2c_master_probe(bus, IMU_I2C_ADDR_PRIMARY, 100) != ESP_OK) {
        addr = IMU_I2C_ADDR_SECONDARY;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_config, &s_imu_dev));

    // Verifica di comunicazione: il registro WHO_AM_I e' fisso a 0x05 su
    // ogni chip QMI8658C, indipendentemente dalla configurazione. Se non
    // corrisponde, quasi certamente l'indirizzo I2C usato e' sbagliato o
    // il cablaggio ha un problema.
    uint8_t reg = REG_WHO_AM_I;
    uint8_t who_am_i = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(s_imu_dev, &reg, 1, &who_am_i, 1, -1));
    if (who_am_i != QMI8658_WHO_AM_I_VALUE) {
        ESP_LOGW(TAG, "WHO_AM_I inatteso: letto 0x%02X, atteso 0x%02X (indirizzo I2C usato: 0x%02X)", who_am_i,
                 QMI8658_WHO_AM_I_VALUE, addr);
    }

    // CTRL1, bit6 (SPI_AI, "address auto increment"): a differenza di
    // molti altri chip I2C, sul QMI8658C questo bit e' 0 di fabbrica,
    // cioe' l'indirizzo di registro NON avanza da solo durante una
    // transazione multi-byte, ne' in scrittura ne' in lettura. Senza
    // abilitarlo esplicitamente, ogni transazione a piu' byte (comprese le
    // scritture di configurazione qui sotto e le letture dati in
    // imu_read()) scriverebbe/leggerebbe ripetutamente lo stesso registro
    // invece di avanzare a quello successivo. Va scritto da solo, in una
    // transazione a un solo registro: non puo' beneficiare lui stesso
    // dell'auto-incremento che sta abilitando.
    uint8_t ctrl1[] = {REG_CTRL1, 0x40}; // bit6=1 (SPI_AI), bit0=0 (oscillatore interno abilitato)
    ESP_ERROR_CHECK(i2c_master_transmit(s_imu_dev, ctrl1, sizeof(ctrl1), -1));

    // Ora che l'auto-incremento e' attivo, CTRL2 e CTRL3 (indirizzi
    // contigui 0x03, 0x04) si possono scrivere in un'unica transazione:
    //   CTRL2 = fondo scala accelerometro + ODR.
    //   CTRL3 = fondo scala giroscopio + ODR.
    uint8_t ctrl2_3[] = {
        REG_CTRL2,
        (uint8_t)((ACCEL_FS_SEL << 4) | ACCEL_GYRO_ODR_SEL),
        (uint8_t)((GYRO_FS_SEL << 4) | ACCEL_GYRO_ODR_SEL),
    };
    ESP_ERROR_CHECK(i2c_master_transmit(s_imu_dev, ctrl2_3, sizeof(ctrl2_3), -1));

    // CTRL7: bit0=aEN, bit1=gEN. Abilita sia l'accelerometro che il
    // giroscopio in modalita' normale (non AttitudeEngine, bit3 sEN=0).
    uint8_t ctrl7[] = {REG_CTRL7, 0x03};
    ESP_ERROR_CHECK(i2c_master_transmit(s_imu_dev, ctrl7, sizeof(ctrl7), -1));

    ESP_LOGI(TAG, "IMU QMI8658C inizializzato (indirizzo I2C 0x%02X, accel +-4g, giroscopio +-512dps, 250Hz)",
             addr);
}

bool imu_read(imu_data_t *out) {
    uint8_t reg = REG_TEMP_L;
    uint8_t raw[SENSOR_DATA_REGS_COUNT];

    esp_err_t err = i2c_master_transmit_receive(s_imu_dev, &reg, 1, raw, sizeof(raw), -1);
    if (err != ESP_OK) {
        return false;
    }

    // Ogni grandezza e' su 16 bit in complemento a 2, byte basso
    // all'indirizzo di registro piu' basso: raw[] e' quindi una sequenza
    // di 7 coppie (basso, alto) da ricomporre con "alto<<8 | basso".
    int16_t temp_raw = (int16_t)((raw[1] << 8) | raw[0]);
    int16_t ax_raw = (int16_t)((raw[3] << 8) | raw[2]);
    int16_t ay_raw = (int16_t)((raw[5] << 8) | raw[4]);
    int16_t az_raw = (int16_t)((raw[7] << 8) | raw[6]);
    int16_t gx_raw = (int16_t)((raw[9] << 8) | raw[8]);
    int16_t gy_raw = (int16_t)((raw[11] << 8) | raw[10]);
    int16_t gz_raw = (int16_t)((raw[13] << 8) | raw[12]);

    // La temperatura ha una sensibilita' fissa di 256 LSB/°C (vedi
    // datasheet §3.8 "Temperature Sensor"), indipendente dal fondo scala.
    out->temperature_c = temp_raw / 256.0f;

    out->accel_g[0] = ax_raw / ACCEL_SENSITIVITY_LSB_PER_G;
    out->accel_g[1] = ay_raw / ACCEL_SENSITIVITY_LSB_PER_G;
    out->accel_g[2] = az_raw / ACCEL_SENSITIVITY_LSB_PER_G;

    out->gyro_dps[0] = gx_raw / GYRO_SENSITIVITY_LSB_PER_DPS;
    out->gyro_dps[1] = gy_raw / GYRO_SENSITIVITY_LSB_PER_DPS;
    out->gyro_dps[2] = gz_raw / GYRO_SENSITIVITY_LSB_PER_DPS;

    return true;
}
