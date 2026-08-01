#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Modulo touch.
 *
 * Inizializza il bus I2C (SCL=10/SDA=11, condiviso in futuro anche da RTC e
 * IMU: per ora questo e' l'unico modulo che lo usa) e il driver del
 * controller touch capacitivo CST816T, ed espone un'unica funzione di
 * lettura semplificata pensata per essere chiamata dalla callback di
 * lettura di un lv_indev di LVGL.
 *
 * Nota sul driver usato: il componente ufficiale Espressif nel Component
 * Registry si chiama "esp_lcd_touch_cst816s" (per il chip "gemello"
 * CST816S), ma CST816S/T/D condividono la stessa mappa di registri I2C e lo
 * stesso protocollo di comunicazione: e' la scelta comune anche in
 * numerosi progetti di riferimento per board che montano il CST816T come
 * questa.
 */

/*
 * Inizializza il bus I2C e il controller touch (reset hardware su GPIO13,
 * indirizzo I2C 0x15). Va chiamata una sola volta, prima di
 * touch_get_point().
 */
void touch_init(void);

/*
 * Interroga il controller touch e restituisce true se il pannello e'
 * attualmente toccato, scrivendo in *x e *y le coordinate del punto di
 * contatto nello stesso sistema di coordinate del display (0..239 per x,
 * 0..279 per y). Restituisce false se non c'e' nessun tocco in corso, nel
 * qual caso *x e *y non vengono modificati.
 *
 * Pensata per essere chiamata periodicamente (tipicamente dalla callback
 * di lettura di un lv_indev, che LVGL richiama gia' a intervalli
 * regolari): non serve un meccanismo a interrupt per un touchscreen
 * reattivo quanto basta per un'interfaccia utente.
 */
bool touch_get_point(uint16_t *x, uint16_t *y);
