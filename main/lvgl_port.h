#pragma once

/*
 * Modulo lvgl_port.
 *
 * E' il "ponte" fra LVGL e l'hardware: inizializza la libreria LVGL,
 * alloca i draw buffer in PSRAM, collega la callback di flush al modulo
 * display, registra la sorgente del tick, e crea il task FreeRTOS che fa
 * girare lv_timer_handler() in loop.
 *
 * E' anche l'unico punto del firmware che sa come sincronizzare l'accesso
 * a LVGL fra piu' task: LVGL non e' thread-safe (vedi
 * docs/lvgl-architettura.md), quindi ogni codice applicativo che vuole
 * creare o modificare widget DEVE farlo fra lvgl_port_lock() e
 * lvgl_port_unlock(), mai chiamare funzioni lv_* liberamente da un task
 * diverso da quello creato qui dentro.
 */

/*
 * Inizializza LVGL e l'hardware del display (chiama internamente
 * display_init(), quindi non serve chiamarlo separatamente), alloca i
 * draw buffer in PSRAM, e avvia il task FreeRTOS dedicato al rendering.
 *
 * Dopo questa chiamata e' gia' possibile creare widget (lv_label_create,
 * lv_btn_create, ecc.) usando lo screen attivo di default
 * (lv_screen_active()), avendo cura di farlo dentro
 * lvgl_port_lock()/lvgl_port_unlock() se il codice chiamante non e' il
 * task creato da questa funzione.
 *
 * Va chiamata una sola volta, dopo display_init() essere stato reso
 * disponibile (viene chiamato internamente) e prima di qualsiasi uso di
 * funzioni lv_*.
 */
void lvgl_port_init(void);

/*
 * Da chiamare prima di qualunque funzione lv_* invocata da un task diverso
 * da quello interno di lvgl_port. Blocca finche' il mutex non e' libero:
 * il task interno lo rilascia periodicamente fra una chiamata a
 * lv_timer_handler() e la successiva.
 */
void lvgl_port_lock(void);

/*
 * Rilascia il mutex preso con lvgl_port_lock(). Va chiamata sempre in coppia
 * con lvgl_port_lock(), il piu' vicino possibile alle chiamate lv_* che
 * deve proteggere (non tenerlo bloccato piu' del necessario, altrimenti si
 * ritarda il rendering fatto dal task interno).
 */
void lvgl_port_unlock(void);
