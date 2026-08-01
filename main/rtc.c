#include "rtc.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "i2c_bus.h"

static const char *TAG = "rtc";

/* Indirizzo I2C a 7 bit del PCF85063A: e' fisso, il chip non ha un pin di
 * selezione indirizzo (vedi datasheet PCF85063A.pdf, §8.4: l'esempio di
 * lettura usa 0xA2/0xA3 come byte indirizzo+R/W a 8 bit, cioe' 0x51 a 7
 * bit). */
#define RTC_I2C_ADDR 0x51

/* Indirizzi dei registri usati (vedi datasheet, Table 5 "Registers
 * overview"). I registri da Seconds a Years sono contigui e leggibili in
 * un solo colpo grazie all'auto-incremento dell'indirizzo interno del
 * chip. */
#define REG_SECONDS 0x04
#define REG_YEARS   0x0A
#define RTC_TIME_REGS_COUNT (REG_YEARS - REG_SECONDS + 1) // 7 registri: Seconds..Years

/* Bit 7 del registro Seconds: "oscillator stop", si alza quando il chip
 * rileva che l'oscillatore al quarzo non ha girato con continuita' da
 * un'ultima lettura nota (primo utilizzo, o batteria tampone scarica). */
#define SECONDS_OS_FLAG_MASK 0x80

static i2c_master_dev_handle_t s_rtc_dev = NULL;

/* --- Conversione BCD <-> binario ---
 * Il PCF85063A codifica ogni campo di data/ora in BCD: il nibble alto e il
 * nibble basso del byte rappresentano separatamente la cifra delle decine
 * e quella delle unita' (es. il numero 34 e' il byte 0x34, non 0x22). */
static uint8_t bcd_to_bin(uint8_t bcd) {
    return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t bin) {
    return (uint8_t)(((bin / 10) << 4) | (bin % 10));
}

/*
 * Calcola il giorno della settimana (0=domenica ... 6=sabato) per una data
 * del calendario gregoriano, con l'algoritmo di Sakamoto: e' compatto e
 * non richiede tabelle di giorni-per-mese ne' casi speciali per gli anni
 * bisestili, a patto di trattare gennaio/febbraio come "mesi 13/14
 * dell'anno precedente" (da cui il decremento di year quando month < 3).
 */
static uint8_t day_of_week(uint16_t year, uint8_t month, uint8_t day) {
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
        year -= 1;
    }
    return (uint8_t)((year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7);
}

/*
 * Converte le macro standard del preprocessore __DATE__ ("Mmm gg aaaa",
 * es. "Jan  1 2026") e __TIME__ ("hh:mm:ss") nella struttura pcf85063_time_t
 * usata per l'auto-impostazione dell'orologio. I nomi dei mesi sono sempre
 * in inglese e a 3 lettere per definizione dello standard C, quindi la
 * tabella di confronto e' fissa indipendentemente dalla lingua del
 * progetto.
 */
static void parse_build_timestamp(pcf85063_time_t *out) {
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char month_str[4] = {0};
    int day, year, hour, minute, second;

    sscanf(__DATE__, "%3s %d %d", month_str, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

    uint8_t month = 1;
    for (uint8_t i = 0; i < 12; i++) {
        if (strncmp(month_str, months[i], 3) == 0) {
            month = (uint8_t)(i + 1);
            break;
        }
    }

    out->year = (uint16_t)year;
    out->month = month;
    out->day = (uint8_t)day;
    out->hour = (uint8_t)hour;
    out->minute = (uint8_t)minute;
    out->second = (uint8_t)second;
    out->weekday = day_of_week(out->year, out->month, out->day);
}

/*
 * Scrive data e ora (registri Seconds..Years) in un'unica transazione I2C:
 * il datasheet raccomanda esplicitamente questo approccio (§8.4) perche'
 * durante l'accesso il chip blocca l'avanzamento dei contatori interni,
 * evitando che un rollover (es. i secondi che arrivano a 60) avvenga a
 * meta' scrittura. Scrivere bit7=0 nel registro Seconds azzera anche il
 * flag "oscillator stop", dichiarando al chip che da questo momento la
 * lettura dell'ora e' da considerarsi attendibile.
 */
static void rtc_write_time(const pcf85063_time_t *t) {
    uint8_t buf[1 + RTC_TIME_REGS_COUNT];
    buf[0] = REG_SECONDS;
    buf[1] = bin_to_bcd(t->second); // bit7 (OS) a 0: azzera il flag oscillatore fermo
    buf[2] = bin_to_bcd(t->minute);
    buf[3] = bin_to_bcd(t->hour); // modalita' 24 ore (default di fabbrica del chip)
    buf[4] = bin_to_bcd(t->day);
    buf[5] = t->weekday & 0x07;
    buf[6] = bin_to_bcd(t->month);
    buf[7] = bin_to_bcd((uint8_t)(t->year % 100)); // il chip memorizza solo le ultime due cifre

    ESP_ERROR_CHECK(i2c_master_transmit(s_rtc_dev, buf, sizeof(buf), -1));
}

void pcf85063_init(void) {
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RTC_I2C_ADDR,
        .scl_speed_hz = 400000, // 400 kHz: velocita' massima dichiarata dal datasheet
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_config, &s_rtc_dev));

    // Leggiamo il registro Seconds per controllare il flag OS (bit7).
    uint8_t reg = REG_SECONDS;
    uint8_t seconds_reg = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(s_rtc_dev, &reg, 1, &seconds_reg, 1, -1));

    if (seconds_reg & SECONDS_OS_FLAG_MASK) {
        pcf85063_time_t build_time;
        parse_build_timestamp(&build_time);
        rtc_write_time(&build_time);
        ESP_LOGW(TAG,
                 "RTC PCF85063A: flag oscillatore fermo rilevato (mai impostato, o batteria "
                 "tampone scarica). Orologio impostato alla data di compilazione: "
                 "%04u-%02u-%02u %02u:%02u:%02u",
                 build_time.year, build_time.month, build_time.day, build_time.hour, build_time.minute,
                 build_time.second);
    } else {
        ESP_LOGI(TAG, "RTC PCF85063A inizializzato (orologio gia' impostato)");
    }
}

bool pcf85063_get_time(pcf85063_time_t *out) {
    uint8_t reg = REG_SECONDS;
    uint8_t raw[RTC_TIME_REGS_COUNT];

    esp_err_t err = i2c_master_transmit_receive(s_rtc_dev, &reg, 1, raw, sizeof(raw), -1);
    if (err != ESP_OK) {
        return false;
    }

    // raw[] = { Seconds, Minutes, Hours, Days, Weekdays, Months, Years }
    out->second = bcd_to_bin(raw[0] & 0x7F);
    out->minute = bcd_to_bin(raw[1] & 0x7F);
    out->hour = bcd_to_bin(raw[2] & 0x3F);   // modalita' 24 ore: bit 5:0
    out->day = bcd_to_bin(raw[3] & 0x3F);    // bit 5:0
    out->weekday = raw[4] & 0x07;            // 0=domenica ... 6=sabato, non e' BCD (0-6 diretto)
    out->month = bcd_to_bin(raw[5] & 0x1F);  // bit 4:0
    out->year = (uint16_t)(2000 + bcd_to_bin(raw[6])); // il chip memorizza solo le ultime due cifre

    return true;
}
