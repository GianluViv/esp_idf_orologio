#pragma once

#include <stdbool.h>

/*
 * Modulo imu.
 *
 * Driver per l'IMU (Inertial Measurement Unit) a 6 assi QMI8658C: un
 * accelerometro a 3 assi piu' un giroscopio a 3 assi nello stesso chip,
 * collegato sullo stesso bus I2C condiviso con touch e RTC (vedi
 * i2c_bus.h).
 *
 * Il QMI8658C offre anche una modalita' avanzata ("AttitudeEngine") che
 * calcola internamente al chip l'orientamento come quaternione e usa un
 * protocollo di comandi dedicato (CTRL9). Qui usiamo invece la modalita'
 * "sensore semplice": accelerometro e giroscopio vengono abilitati in
 * lettura diretta, ed emettono normalmente in ciascun registro dati il
 * valore grezzo dell'ultimo campione. E' la scelta piu' semplice quando
 * serve solo mostrare i valori istantanei letti dai sensori, senza fare
 * fusione sensoriale o calcolare un orientamento 3D.
 */

/* Un campione completo letto dal QMI8658C: accelerazione in "g" (multipli
 * dell'accelerazione di gravita' terrestre), velocita' angolare in gradi
 * al secondo (dps, degrees per second), temperatura del chip in gradi
 * Celsius (utile anche solo come controllo di plausibilita' dei dati). */
typedef struct {
    float accel_g[3];   // [0]=X, [1]=Y, [2]=Z
    float gyro_dps[3];  // [0]=X, [1]=Y, [2]=Z
    float temperature_c;
} imu_data_t;

/*
 * Inizializza il device I2C del QMI8658C sul bus condiviso (provando
 * prima l'indirizzo 0x6A, poi 0x6B come fallback: il pin SA0 del chip puo'
 * essere cablato in un modo o nell'altro a seconda della revisione della
 * board), verifica l'identita' del chip leggendo il registro WHO_AM_I, e
 * configura accelerometro (fondo scala +-4g) e giroscopio (fondo scala
 * +-512 dps) a 250 campioni al secondo. Va chiamata una sola volta, prima
 * di imu_read().
 */
void imu_init(void);

/*
 * Legge in un'unica transazione I2C temperatura, accelerazione e
 * velocita' angolare correnti, e li converte dai valori grezzi a 16 bit
 * restituiti dal chip alle unita' fisiche (g, dps, gradi Celsius) usando i
 * fattori di sensibilita' del fondo scala configurato in imu_init().
 * Restituisce true se la lettura I2C e' andata a buon fine.
 */
bool imu_read(imu_data_t *out);
