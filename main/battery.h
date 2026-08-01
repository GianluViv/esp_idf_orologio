#pragma once

#include <stdint.h>

/*
 * Modulo battery.
 *
 * Legge la tensione della batteria Li-ion tramite l'ADC interno
 * dell'ESP32-S3, sul pin GPIO1 (vedi tabella pin in CLAUDE.md). La
 * batteria non e' collegata direttamente al pin ADC (la tensione massima
 * di una cella carica, ~4.2V, supererebbe il fondo scala dell'ADC): sullo
 * schematico della board e' presente un partitore resistivo fra VBAT e
 * GPIO1 (due resistenze da 100k(ohm), R7 e R24) che dimezza la tensione
 * prima di presentarla al pin. Questo modulo legge quindi la tensione sul
 * pin e la moltiplica per il rapporto del partitore per risalire alla
 * tensione reale della batteria.
 *
 * Nota per chi lavora su questo codice: il rapporto 1:2 e' dedotto dallo
 * schematico (due resistenze dello stesso valore), ma non e' stato ancora
 * validato con una misura diretta sulla batteria reale. Se il valore
 * mostrato a schermo risultasse sistematicamente troppo alto o troppo
 * basso rispetto a una misura con un multimetro, va corretta la costante
 * BATTERY_DIVIDER_RATIO in battery.c.
 */

/*
 * Inizializza l'unita' ADC1 e il canale collegato a GPIO1, insieme alla
 * calibrazione hardware dell'ADC (se disponibile su questo chip: l'ESP32-S3
 * la supporta). Va chiamata una sola volta, prima delle funzioni di
 * lettura.
 */
void battery_init(void);

/*
 * Restituisce la tensione stimata della batteria, in volt, gia'
 * compensata per il partitore resistivo (vedi sopra).
 */
float battery_read_voltage(void);

/*
 * Restituisce una stima percentuale (0-100) della carica residua,
 * ottenuta mappando linearmente la tensione fra una soglia di batteria
 * scarica (3.3V) e una di batteria carica (4.2V), valori tipici per una
 * singola cella Li-ion. E' una stima grezza: la curva di scarica reale di
 * una cella Li-ion non e' lineare (resta quasi piatta per gran parte della
 * scarica e crolla velocemente solo verso la fine), ma per un'indicazione
 * di massima a schermo e' sufficiente.
 */
uint8_t battery_read_percent(void);
