#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "diagnostics.h"
#include "lvgl_port.h"
#include "rtc.h"
#include "imu.h"
#include "battery.h"
#include "buzzer.h"
#include "sensor_dashboard.h"

static const char *TAG = "orologio";

// Punto di ingresso del firmware, chiamato da ESP-IDF all'avvio.
void app_main(void) {
    // Stampa una tantum nel log le info sul chip (modello, flash, heap libero).
    diagnostics_print_chip_info();

    // Inizializza LVGL (che a sua volta inizializza il display, il bus I2C
    // condiviso e il touch) e avvia il task dedicato al rendering.
    lvgl_port_init();

    // Driver dell'hardware aggiuntivo della board: RTC e IMU si aggiungono
    // al bus I2C gia' creato da lvgl_port_init() (tramite touch_init()),
    // batteria e buzzer sono indipendenti (ADC e LEDC).
    pcf85063_init();
    imu_init();
    battery_init();
    buzzer_init();

    // Crea la schermata che mostra i valori letti da RTC/IMU/batteria e il
    // bottone di test del buzzer. Il vecchio bottone di test del touch
    // (diagnostics_create_test_button(), usato durante il primo bring-up di
    // LVGL) non viene piu' richiamato qui: occuperebbe lo stesso spazio a
    // schermo della dashboard, sovrapponendosi visivamente. La funzione
    // resta comunque disponibile in diagnostics.h per un test rapido e
    // isolato del touch, se mai servisse di nuovo in futuro.
    sensor_dashboard_create();

    int count = 0;

    // app_main non deve mai fare return; vTaskDelay libera la CPU invece di bloccarla.
    while (1) {
        ESP_LOGI(TAG, "Tick %d", count++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
