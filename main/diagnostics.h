#pragma once

/*
 * Modulo diagnostica.
 *
 * Raccoglie le funzioni di supporto usate per verificare che il firmware e
 * l'hardware sottostante funzionino correttamente: stampe sulla console
 * seriale (info sul chip, stato della memoria, eventuali altre diagnostiche
 * che aggiungeremo in futuro: stato batteria, letture sensori a scopo di
 * debug, ecc.) ed elementi di UI usati solo per verificare a schermo che
 * una catena di moduli (es. LVGL + touch) funzioni, senza far parte della
 * UI applicativa vera e propria (l'orologio).
 *
 * Tenere queste funzioni separate da main.c serve a mantenere app_main()
 * leggibile: main.c descrive "cosa fa" il firmware ad alto livello,
 * mentre qui dentro finisce il dettaglio implementativo di "come" si
 * raccolgono i dati diagnostici o si verifica che i moduli rispondano.
 */

/*
 * Stampa sul log (tag "diagnostics") un riepilogo delle caratteristiche
 * del chip in uso: versione di ESP-IDF, modello e numero di core, revisione
 * del silicio, feature disponibili (WiFi/BT/BLE, flash embedded o esterna),
 * dimensione della flash e heap libero al momento della chiamata.
 *
 * Va chiamata tipicamente una sola volta, all'avvio (in app_main), per
 * avere subito nel log un quadro dell'hardware su cui gira il firmware.
 */
void diagnostics_print_chip_info(void);

/*
 * Crea a schermo un bottone di test LVGL ("Tocca qui") che logga un
 * messaggio quando viene toccato: e' il test end-to-end usato per
 * verificare che l'intera catena display + LVGL + touch funzioni insieme
 * (vedi fase 9 in docs/piano-lvgl-touch.md). Non fa parte della UI
 * applicativa dell'orologio: e' pensato per essere tolto (o sostituito con
 * un test analogo) quando la UI reale prendera' il suo posto.
 *
 * Va chiamata dopo lvgl_port_init(), da un task diverso da quello interno
 * di lvgl_port (usa lvgl_port_lock()/unlock() al proprio interno).
 */
void diagnostics_create_test_button(void);
