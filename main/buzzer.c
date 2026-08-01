#include "buzzer.h"

#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "buzzer";

#define BUZZER_GPIO 42

#define BUZZER_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER   LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0

/* Risoluzione del duty cycle: con 10 bit il duty va da 0 a 1023. Un duty
 * al 50% (512) produce un'onda quadra pulita, la forma d'onda piu' comune
 * per pilotare un cicalino passivo. */
#define BUZZER_LEDC_DUTY_RESOLUTION LEDC_TIMER_10_BIT
#define BUZZER_LEDC_DUTY_50_PERCENT (1 << (LEDC_TIMER_10_BIT - 1))

/* Timer software che rimette a zero il duty (spegne il suono) allo scadere
 * della durata richiesta in buzzer_beep(), senza bisogno di bloccare il
 * chiamante con un vTaskDelay(). */
static esp_timer_handle_t s_stop_timer = NULL;

static void stop_timer_cb(void *arg) {
    (void)arg;
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

void buzzer_init(void) {
    ledc_timer_config_t timer_config = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_LEDC_DUTY_RESOLUTION,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = 2000, // valore di partenza qualsiasi: buzzer_beep() lo cambia ad ogni chiamata
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0, // buzzer spento all'avvio
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

    esp_timer_create_args_t stop_timer_args = {
        .callback = stop_timer_cb,
        .name = "buzzer_stop",
    };
    ESP_ERROR_CHECK(esp_timer_create(&stop_timer_args, &s_stop_timer));

    ESP_LOGI(TAG, "Buzzer inizializzato (LEDC su GPIO%d)", BUZZER_GPIO);
}

void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms) {
    // Se un beep precedente e' ancora in corso, il suo timer di spegnimento
    // e' ancora attivo: lo fermiamo prima di ripartire, cosi' non rischia
    // di spegnere a meta' il nuovo beep che stiamo per avviare.
    esp_timer_stop(s_stop_timer);

    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_LEDC_DUTY_50_PERCENT);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);

    ESP_ERROR_CHECK(esp_timer_start_once(s_stop_timer, (uint64_t)duration_ms * 1000));
}
