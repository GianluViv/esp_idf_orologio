#pragma once

#include <stdint.h>

/*
 * Modulo buzzer.
 *
 * Pilota il cicalino passivo collegato a GPIO42 (vedi tabella pin in
 * CLAUDE.md) tramite il periferico LEDC dell'ESP32-S3: normalmente usato
 * per dimmerare un LED con un segnale PWM, LEDC va benissimo anche per
 * generare un'onda quadra a una frequenza scelta (una nota) su un
 * cicalino passivo, che riproduce il suono seguendo esattamente quella
 * frequenza.
 *
 * A differenza degli altri moduli hardware di questo progetto, il buzzer
 * e' un attuatore (non ha un "valore" da leggere): espone quindi solo una
 * funzione di comando, buzzer_beep(), pensata per essere non bloccante in
 * modo da poter essere chiamata anche da una callback LVGL (es. il click
 * di un bottone) senza fermare per tutta la durata del suono il task che
 * disegna l'interfaccia.
 */

/*
 * Inizializza il canale LEDC collegato al buzzer. Va chiamata una sola
 * volta, prima di buzzer_beep().
 */
void buzzer_init(void);

/*
 * Fa suonare il buzzer alla frequenza freq_hz per duration_ms
 * millisecondi, poi lo spegne da solo. Non blocca: la funzione ritorna
 * subito dopo aver avviato il suono, e lo spegnimento avviene in background
 * tramite un timer software (esp_timer). Chiamate ravvicinate interrompono
 * il beep precedente e ne iniziano uno nuovo, invece di accavallarsi.
 */
void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms);
