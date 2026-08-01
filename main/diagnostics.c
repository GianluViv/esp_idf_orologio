#include "diagnostics.h"

#include <inttypes.h>
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "lvgl_port.h"

/* Tag separato da quello di main.c, cosi nel log si distingue subito
 * se una riga viene dal modulo applicativo o da quello diagnostico. */
static const char *TAG = "diagnostics";

void diagnostics_print_chip_info(void) {
    /* esp_chip_info() riempie questa struttura con modello, numero di core,
     * revisione del silicio e bitmask delle feature disponibili (WiFi,
     * Bluetooth classico, BLE, tipo di flash). */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    /* La dimensione della flash non fa parte di esp_chip_info_t: va letta
     * a parte tramite il driver esp_flash, passando NULL per interrogare
     * il chip flash primario (quello da cui il firmware viene eseguito). */
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip: %s, %d core(s), rev v%d.%d", CONFIG_IDF_TARGET, chip_info.cores,
             chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Features: %s%s%s%s", (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded-Flash " : "External-Flash ");
    ESP_LOGI(TAG, "Flash size: %" PRIu32 " MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());

    /* Heap libera nella sola PSRAM (capability MALLOC_CAP_SPIRAM): utile per
     * verificare a runtime che la PSRAM esterna sia stata rilevata e
     * mappata correttamente, cosa che ci serve prima di allocare i buffer
     * di LVGL li dentro. Se la PSRAM non e' abilitata/trovata, questo
     * valore e' 0. */
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Free PSRAM: %u bytes", (unsigned)psram_free);
}

/*
 * Callback invocata da LVGL quando il bottone di test viene premuto e
 * rilasciato (LV_EVENT_CLICKED). Gira nel task interno di lvgl_port,
 * chiamata da dentro lv_timer_handler(): il mutex e' gia' preso da chi ci
 * ha chiamati, quindi qui NON si deve chiamare lvgl_port_lock() (altrimenti
 * il task si bloccherebbe aspettando un mutex che ha gia' in mano lui
 * stesso).
 */
static void on_test_button_clicked(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG, "Bottone di test premuto: il touch funziona");
}

void diagnostics_create_test_button(void) {
    /* lvgl_port_lock()/unlock() servono perche' questa funzione viene
     * chiamata da app_main, un task diverso da quello interno di lvgl_port:
     * vedi docs/lvgl-architettura.md, sezione "Concorrenza". */
    lvgl_port_lock();

    lv_obj_t *btn = lv_button_create(lv_screen_active());
    lv_obj_center(btn);
    lv_obj_add_event_cb(btn, on_test_button_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Tocca qui");
    lv_obj_center(label);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Bottone di test LVGL creato");
}
