#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "diagnostics.h"
#include "lvgl_port.h"

static const char *TAG = "orologio";

// Punto di ingresso del firmware, chiamato da ESP-IDF all'avvio.
void app_main(void) {
    // Stampa una tantum nel log le info sul chip (modello, flash, heap libero).
    diagnostics_print_chip_info();

    // Inizializza LVGL (che a sua volta inizializza il display) e avvia il
    // task dedicato al rendering.
    lvgl_port_init();

    diagnostics_create_test_button();

    int count = 0;

    // app_main non deve mai fare return; vTaskDelay libera la CPU invece di bloccarla.
    while (1) {
        ESP_LOGI(TAG, "Tick %d", count++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
