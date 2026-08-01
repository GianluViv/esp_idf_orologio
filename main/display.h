#pragma once

#include "esp_lcd_types.h"

/*
 * Modulo display.
 *
 * Si occupa esclusivamente dell'hardware del pannello LCD: inizializza il
 * bus SPI verso il controller ST7789V2 e il relativo "panel handle" di
 * esp_lcd, e gestisce l'accensione/spegnimento del backlight.
 *
 * Questo modulo NON sa nulla di LVGL: espone solo un esp_lcd_panel_handle_t
 * su cui si puo' chiamare esp_lcd_panel_draw_bitmap() per scrivere pixel.
 * Sara' il modulo lvgl_port a usare questo handle dentro la callback di
 * flush di LVGL. Tenerli separati permette di testare il pannello da solo
 * (fase 5 del piano LVGL) prima di introdurre la complessita' di LVGL.
 *
 * Il driver per il controller ST7789V2 non e' un componente esterno: fa
 * parte del componente "esp_lcd" incluso di serie in ESP-IDF
 * (esp_lcd_panel_st7789.c), quindi non richiede dipendenze aggiuntive nel
 * Component Manager.
 */

/* Risoluzione fisica del pannello (in pixel). Il controller ST7789V2 ha
 * internamente una GRAM da 240x320, ma il vetro montato su questa board e'
 * solo 240x280: la differenza (40 righe) va compensata con un offset
 * verticale ("gap"), gestito internamente da display_init(). */
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 280

/*
 * Inizializza il bus SPI (pin CLK/DIN/CS/DC/RST secondo il pinout della
 * board), crea e configura il panel handle per il controller ST7789V2, e
 * accende il display (senza ancora aver disegnato nulla: lo schermo si
 * illumina di uno sfondo indefinito finche' non si chiama
 * esp_lcd_panel_draw_bitmap() o non si avvia LVGL).
 *
 * Il pin del backlight (GPIO15) viene configurato come uscita digitale ma
 * NON viene acceso qui: usare display_backlight_set() esplicitamente dopo
 * aver verificato che l'inizializzazione del pannello sia andata a buon
 * fine, cosi' non si rischia di mostrare per una frazione di secondo
 * un'immagine spazzatura durante il boot.
 *
 * @param on_color_trans_done callback opzionale (puo' essere NULL) invocata
 *        in contesto ISR quando il trasferimento SPI/DMA di un blocco di
 *        pixel e' realmente terminato. Questo modulo non sa e non deve
 *        sapere chi la usa: e' semplicemente un modo generico per far
 *        sapere al chiamante "il buffer che mi hai passato a
 *        esp_lcd_panel_draw_bitmap() e' di nuovo libero". Il modulo
 *        lvgl_port la usera' per segnalare a LVGL che puo' riusare il
 *        draw buffer, senza che display.c debba includere lvgl.h.
 * @param user_ctx puntatore opaco passato invariato come terzo argomento
 *        della callback qui sopra.
 *
 * Va chiamata una sola volta, prima di qualunque altra funzione di questo
 * modulo o di lvgl_port.
 *
 * @return handle del pannello, da passare a esp_lcd_panel_draw_bitmap() o
 *         al modulo lvgl_port per registrare la callback di flush.
 */
esp_lcd_panel_handle_t display_init(esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done, void *user_ctx);

/*
 * Accende (on = true) o spegne (on = false) il backlight del display,
 * pilotando direttamente il pin GPIO15 come uscita digitale on/off.
 *
 * Nota didattica: questo e' il controllo piu' semplice possibile. Un
 * dimming graduale della luminosita' richiederebbe di pilotare lo stesso
 * pin con un segnale PWM tramite il periferico LEDC invece che con un
 * semplice gpio_set_level(); e' un miglioramento rimandabile a quando il
 * resto della UI funziona.
 */
void display_backlight_set(bool on);
