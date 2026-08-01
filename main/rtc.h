#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Modulo rtc.
 *
 * Driver per il Real-Time Clock PCF85063A (indirizzo I2C fisso 0x51),
 * collegato sullo stesso bus I2C condiviso con touch e IMU (vedi
 * i2c_bus.h). Il chip tiene data e ora anche a microcontrollore spento,
 * grazie a una piccola batteria tampone separata da quella principale del
 * dispositivo, e le espone come registri in formato BCD (Binary Coded
 * Decimal: ogni cifra decimale occupa 4 bit, es. il numero 34 e' codificato
 * come 0x34 e non come 0x22).
 *
 * Il chip segnala con un flag interno ("OS", oscillator stop) se
 * l'oscillatore al quarzo non ha mai girato con continuita' da un valore
 * noto: succede al primo utilizzo in assoluto, oppure se la batteria
 * tampone si e' scaricata del tutto. pcf85063_init() controlla questo flag e,
 * se attivo, inizializza l'orologio con la data/ora di compilazione del
 * firmware (macro standard del preprocessore C __DATE__/__TIME__): non e'
 * l'ora esatta, ma e' un valore di partenza ragionevole invece di lasciare
 * il reset di fabbrica (1 gennaio 2000).
 */

/* Rappresentazione "leggibile" (binaria, non BCD) di data e ora lette dal
 * PCF85063A. weekday segue la stessa convenzione usata dal chip: un numero
 * da 0 a 6 il cui significato e' scelto da chi scrive il registro (qui,
 * 0=domenica ... 6=sabato, la convenzione piu' comune). */
typedef struct {
    uint16_t year;   // anno completo, es. 2026 (il chip memorizza solo le ultime due cifre)
    uint8_t month;   // 1-12
    uint8_t day;     // 1-31
    uint8_t weekday; // 0=domenica ... 6=sabato
    uint8_t hour;    // 0-23 (il chip e' configurato in modalita' 24 ore, il default di fabbrica)
    uint8_t minute;  // 0-59
    uint8_t second;  // 0-59
} pcf85063_time_t;

/*
 * Inizializza il device I2C del PCF85063A sul bus condiviso (vedi
 * i2c_bus_get_handle()). Se il chip segnala il flag di oscillatore fermo,
 * imposta automaticamente data e ora alla data/ora di compilazione del
 * firmware. Va chiamata una sola volta, prima di pcf85063_get_time().
 */
void pcf85063_init(void);

/*
 * Legge data e ora correnti dal PCF85063A in un'unica transazione I2C (i
 * sette registri Seconds..Years, indirizzi 04h-0Ah), cosi' come
 * raccomandato dal datasheet per evitare di leggere valori incoerenti se
 * capita un "rollover" (es. i minuti che scattano) a meta' lettura.
 * Restituisce true se la lettura I2C e' andata a buon fine.
 */
bool pcf85063_get_time(pcf85063_time_t *out);
