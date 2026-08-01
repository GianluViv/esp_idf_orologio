#include "lvgl_port.h"

#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "display.h"
#include "touch.h"

static const char *TAG = "lvgl_port";

/* Altezza (in righe) di ciascun draw buffer parziale. Con 240 colonne e 2
 * byte/pixel (RGB565), 30 righe pesano 240*30*2 = 14400 byte: circa 1/9
 * della superficie totale del pannello (240x280), in linea con la soglia
 * "almeno 1/10 schermo" raccomandata da LVGL per il rendering parziale.
 * Usiamo DUE buffer di questa dimensione cosi' LVGL puo' preparare il
 * prossimo blocco mentre il precedente e' ancora in trasferimento DMA
 * verso il display (vedi docs/lvgl-architettura.md, sezione Buffering). */
#define LVGL_PORT_BUF_HEIGHT 30
#define LVGL_PORT_BUF_SIZE_BYTES (DISPLAY_WIDTH * LVGL_PORT_BUF_HEIGHT * sizeof(uint16_t))

/* Stack e priorita' del task dedicato a LVGL. Lo stack e' abbondante
 * (8KB) perche' il motore di rendering software di LVGL usa una certa
 * quantita' di variabili locali durante il disegno dei widget piu'
 * complessi (es. testo con font grandi, angoli arrotondati). */
#define LVGL_PORT_TASK_STACK_SIZE 8192
#define LVGL_PORT_TASK_PRIORITY   4

/* Se lv_timer_handler() segnala che puo' aspettare pochissimo (es. 1ms)
 * prima della prossima chiamata, imponiamo comunque un delay minimo: senza
 * questo limite il task girerebbe in un loop troppo stretto, sottraendo
 * inutilmente CPU agli altri task invece di lasciarla all'idle task. */
#define LVGL_PORT_TASK_MIN_DELAY_MS 5

/*
 * Limite MASSIMO al delay fra una lv_timer_handler() e la successiva.
 *
 * E' un dettaglio poco intuitivo di LVGL 9, importante da capire: il timer
 * interno che gestisce il refresh dello schermo (disp->refr_timer) si
 * mette in pausa da solo subito dopo ogni esecuzione (per non sprecare CPU
 * quando non c'e' nulla da ridisegnare), e viene "svegliato" di nuovo in
 * automatico solo quando qualcosa invalida un widget (es. creazione di un
 * nuovo oggetto, cambio di testo, animazione). Finche' e' in pausa pero'
 * NON contribuisce al calcolo del valore restituito da lv_timer_handler():
 * quel valore, in assenza di altri timer attivi, diventa allora
 * LV_NO_TIMER_READY (0xFFFFFFFF), che alla lettera significherebbe
 * "puoi aspettare quasi 50 giorni". Se il nostro loop rispettasse quel
 * valore alla lettera, il task andrebbe in vTaskDelay per giorni e
 * qualunque widget creato o modificato DOPO quel momento non verrebbe mai
 * piu' disegnato (verificato sperimentalmente: uno schermo tutto bianco,
 * col testo del primo widget mai comparso). Il rimedio e' semplicemente
 * non fidarsi ciecamente del valore restituito e ricontrollare comunque
 * con una certa regolarita': qui usiamo lo stesso periodo del refresh
 * timer di default di LVGL (LV_DEF_REFR_PERIOD, 33ms). */
#define LVGL_PORT_TASK_MAX_DELAY_MS 33

/* Mutex che protegge l'accesso alle strutture dati interne di LVGL (che
 * NON sono thread-safe). Va preso da qualunque task, diverso da quello
 * creato da questo modulo, che voglia chiamare funzioni lv_*. */
static SemaphoreHandle_t s_lvgl_mutex = NULL;

/*
 * Sorgente del tick per LVGL: invece di incrementare un contatore da un
 * timer periodico (pattern lv_tick_inc()), usiamo la variante piu' semplice
 * offerta da LVGL 9, lv_tick_set_cb(), che chiede direttamente a LVGL di
 * leggere il tempo trascorso da una funzione a nostra scelta ogni volta che
 * gli serve. esp_timer_get_time() restituisce i microsecondi trascorsi dal
 * boot con una risoluzione ben piu' che sufficiente per i millisecondi che
 * servono a LVGL.
 */
static uint32_t lvgl_tick_get_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/*
 * Invocata da esp_lcd in contesto ISR quando il trasferimento SPI/DMA di un
 * blocco di pixel verso il pannello e' realmente concluso. E' il momento
 * giusto per dire a LVGL "il buffer che mi hai dato in flush_cb e' di nuovo
 * libero, puoi riusarlo o iniziare il prossimo": farlo prima (es. subito
 * dopo aver chiamato esp_lcd_panel_draw_bitmap, che accoda il trasferimento
 * ma non aspetta che finisca) rischierebbe di far sovrascrivere a LVGL un
 * buffer i cui dati non sono ancora finiti di uscire sul bus SPI.
 *
 * user_ctx qui e' il puntatore al display LVGL, passato a display_init() da
 * lvgl_port_init() qui sotto: display.c lo inoltra senza doverlo capire.
 */
static bool notify_flush_ready(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    (void)io;
    (void)edata;
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false; // non abbiamo svegliato nessun task a priorita' piu' alta da qui
}

/*
 * Callback di flush registrata su LVGL: riceve un'area rettangolare e un
 * buffer di pixel gia' renderizzati da LVGL (formato RGB565, coerente con
 * LV_COLOR_DEPTH=16), e li spedisce al pannello fisico tramite il modulo
 * display. Nota che qui NON chiamiamo lv_display_flush_ready(): lo fa
 * notify_flush_ready() sopra, quando il trasferimento e' davvero finito.
 */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    // area->x2/y2 in LVGL sono l'ultimo pixel incluso nell'area; esp_lcd_panel_draw_bitmap
    // si aspetta invece un estremo finale escluso, da cui i "+1".
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

/*
 * Callback di lettura registrata sul lv_indev del touch: LVGL la richiama
 * periodicamente (da dentro lv_timer_handler(), quindi gia' nel task e con
 * il mutex gia' preso: non serve lvgl_port_lock() qui dentro). Il compito
 * e' semplicemente tradurre touch_get_point() nel formato che LVGL si
 * aspetta: coordinate correnti + stato premuto/rilasciato.
 */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;

    uint16_t x, y;
    if (touch_get_point(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/*
 * Task dedicato a LVGL: e' l'unico task che chiama lv_timer_handler(), la
 * funzione che fa avanzare il motore di rendering di LVGL (controlla quali
 * widget sono da ridisegnare, aggiorna animazioni, invoca la flush_cb
 * quando serve). Ogni chiamata protetta dal mutex, cosi' un eventuale altro
 * task che sta modificando la UI tramite lvgl_port_lock()/unlock() non puo'
 * mai sovrapporsi a lv_timer_handler() in esecuzione.
 */
static void lvgl_task(void *arg) {
    (void)arg;

    while (1) {
        lvgl_port_lock();
        uint32_t sleep_ms = lv_timer_handler();
        lvgl_port_unlock();

        if (sleep_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            sleep_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        } else if (sleep_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
            sleep_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    }
}

void lvgl_port_init(void) {
    s_lvgl_mutex = xSemaphoreCreateMutex();
    assert(s_lvgl_mutex != NULL);

    lv_init();
    lv_tick_set_cb(lvgl_tick_get_ms);

    // Il display LVGL va creato PRIMA di display_init(): il suo indirizzo
    // ci serve gia' come user_ctx da passare alla callback di fine
    // trasferimento SPI (vedi notify_flush_ready sopra).
    lv_display_t *disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    assert(disp != NULL);

    esp_lcd_panel_handle_t panel = display_init(notify_flush_ready, disp);
    display_backlight_set(true);

    lv_display_set_user_data(disp, panel);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush_cb);

    // I draw buffer vivono in PSRAM (MALLOC_CAP_SPIRAM) e devono essere
    // utilizzabili dal controller DMA che pilota il bus SPI
    // (MALLOC_CAP_DMA): su ESP32-S3 heap_caps_malloc si occupa da solo di
    // rispettare gli allineamenti richiesti dalla cache e dal DMA quando
    // gli si chiedono entrambe le capability insieme.
    void *buf1 = heap_caps_malloc(LVGL_PORT_BUF_SIZE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(LVGL_PORT_BUF_SIZE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    assert(buf1 != NULL && buf2 != NULL);

    lv_display_set_buffers(disp, buf1, buf2, LVGL_PORT_BUF_SIZE_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Touch: inizializza il bus I2C e il controller CST816T (modulo touch),
    // poi registra un lv_indev di tipo "puntatore" che LVGL interroga da
    // solo, periodicamente, tramite touch_read_cb() qui sopra.
    touch_init();
    lv_indev_t *indev = lv_indev_create();
    assert(indev != NULL);
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);

    BaseType_t task_created = xTaskCreate(lvgl_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, NULL,
                                           LVGL_PORT_TASK_PRIORITY, NULL);
    assert(task_created == pdPASS);

    ESP_LOGI(TAG, "LVGL inizializzato: buffer %u byte x2 in PSRAM, task avviato", (unsigned)LVGL_PORT_BUF_SIZE_BYTES);
}

void lvgl_port_lock(void) {
    xSemaphoreTake(s_lvgl_mutex, portMAX_DELAY);
}

void lvgl_port_unlock(void) {
    xSemaphoreGive(s_lvgl_mutex);
}
