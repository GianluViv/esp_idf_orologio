#include "sensor_dashboard.h"

#include <stdio.h>
#include "lvgl.h"
#include "esp_log.h"
#include "lvgl_port.h"
#include "rtc.h"
#include "imu.h"
#include "battery.h"
#include "buzzer.h"

static const char *TAG = "sensor_dashboard";

/* Ogni quanto rileggere i sensori e aggiornare le label, in millisecondi.
 * 500 ms e' un compromesso ragionevole: abbastanza reattivo da vedere
 * l'accelerometro/giroscopio muoversi in tempo (quasi) reale muovendo la
 * board, ma senza ridisegnare lo schermo cosi' spesso da sprecare CPU. */
#define DASHBOARD_REFRESH_PERIOD_MS 500

/* Handle dei widget che vengono aggiornati periodicamente, salvati come
 * variabili statiche del modulo: refresh_cb() ne ha bisogno per cambiare
 * il testo, ma nessun altro modulo deve poterli toccare direttamente. */
static lv_obj_t *s_label_datetime = NULL;
static lv_obj_t *s_label_accel = NULL;
static lv_obj_t *s_label_gyro = NULL;
static lv_obj_t *s_label_battery = NULL;

/* Nomi dei giorni della settimana, nello stesso ordine (0=domenica) usato
 * dal campo weekday di pcf85063_time_t. */
static const char *const WEEKDAY_NAMES[7] = {
    "Domenica", "Lunedi", "Martedi", "Mercoledi", "Giovedi", "Venerdi", "Sabato",
};

/*
 * Callback del timer periodico LVGL: a differenza del bottone di test in
 * diagnostics.c (che reagisce a un evento, il click), qui serve un
 * aggiornamento a intervalli regolari indipendente dall'interazione
 * dell'utente. lv_timer_create() (vedi sensor_dashboard_create() sotto)
 * la registra per essere richiamata gia' dal task interno di lvgl_port,
 * dentro lv_timer_handler(): il mutex e' quindi gia' preso da chi ci ha
 * chiamati, non serve lvgl_port_lock() qui dentro (esattamente come per
 * on_test_button_clicked() in diagnostics.c).
 */
static void refresh_cb(lv_timer_t *timer) {
    (void)timer;

    pcf85063_time_t time;
    if (pcf85063_get_time(&time)) {
        const char *weekday_name = (time.weekday < 7) ? WEEKDAY_NAMES[time.weekday] : "?";
        lv_label_set_text_fmt(s_label_datetime, "%s %04u-%02u-%02u\n%02u:%02u:%02u", weekday_name, time.year,
                               time.month, time.day, time.hour, time.minute, time.second);
    }

    // Nota su %f: sdkconfig di questo progetto usa l'implementazione
    // "builtin" di sprintf di LVGL (CONFIG_LV_USE_BUILTIN_SPRINTF), pensata
    // per essere piccola in flash, che pero' NON supporta il formato %f dei
    // numeri in virgola mobile (lv_label_set_text_fmt(...,"%.2f",...)
    // stampa solo la lettera "f", senza numero). La libreria C standard
    // (snprintf(), da <stdio.h>) invece supporta %f regolarmente: qui
    // formattiamo quindi ogni valore float per conto nostro in una stringa
    // con snprintf(), e la passiamo a lv_label_set_text_fmt() con %s.
    imu_data_t imu;
    if (imu_read(&imu)) {
        char accel_x[8], accel_y[8], accel_z[8];
        snprintf(accel_x, sizeof(accel_x), "%.2f", (double)imu.accel_g[0]);
        snprintf(accel_y, sizeof(accel_y), "%.2f", (double)imu.accel_g[1]);
        snprintf(accel_z, sizeof(accel_z), "%.2f", (double)imu.accel_g[2]);
        lv_label_set_text_fmt(s_label_accel, "Accel (g)\nX:%s  Y:%s  Z:%s", accel_x, accel_y, accel_z);

        char gyro_x[8], gyro_y[8], gyro_z[8], imu_temp[8];
        snprintf(gyro_x, sizeof(gyro_x), "%.0f", (double)imu.gyro_dps[0]);
        snprintf(gyro_y, sizeof(gyro_y), "%.0f", (double)imu.gyro_dps[1]);
        snprintf(gyro_z, sizeof(gyro_z), "%.0f", (double)imu.gyro_dps[2]);
        snprintf(imu_temp, sizeof(imu_temp), "%.1f", (double)imu.temperature_c);
        lv_label_set_text_fmt(s_label_gyro, "Giroscopio (dps)\nX:%s  Y:%s  Z:%s\nTemp IMU: %s C", gyro_x, gyro_y,
                               gyro_z, imu_temp);
    }

    float voltage = battery_read_voltage();
    uint8_t percent = battery_read_percent();
    char battery_voltage[8];
    snprintf(battery_voltage, sizeof(battery_voltage), "%.2f", (double)voltage);
    lv_label_set_text_fmt(s_label_battery, "Batteria: %sV (%u%%)", battery_voltage, (unsigned)percent);
}

/* Callback del bottone "Beep": gira anch'essa gia' nel task interno di
 * lvgl_port (chiamata da dentro lv_timer_handler() quando LVGL processa
 * l'evento di click), quindi non serve lvgl_port_lock() qui. buzzer_beep()
 * e' pensata apposta per essere non bloccante, cosi' anche se venisse
 * chiamata da qui non fermerebbe il rendering. */
static void on_beep_button_clicked(lv_event_t *e) {
    (void)e;
    buzzer_beep(2000, 100);
}

void sensor_dashboard_create(void) {
    // lvgl_port_lock()/unlock() servono perche' questa funzione viene
    // chiamata da app_main, un task diverso da quello interno di
    // lvgl_port (vedi docs/lvgl-architettura.md, sezione "Concorrenza").
    lvgl_port_lock();

    // Contenitore verticale che dispone le label una sotto l'altra
    // (LV_FLEX_FLOW_COLUMN), distribuendole con spazio uniforme
    // sull'altezza disponibile: utile su un display rotondo come questo,
    // dove il contenuto va tenuto vicino al centro per non finire tagliato
    // dalla cornice tonda.
    lv_obj_t *container = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(container); // niente sfondo/bordo: si fonde con lo schermo
    lv_obj_set_size(container, 210, 260);
    lv_obj_center(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER);

    s_label_datetime = lv_label_create(container);
    lv_obj_set_style_text_align(s_label_datetime, LV_TEXT_ALIGN_CENTER, 0);

    s_label_accel = lv_label_create(container);
    lv_obj_set_style_text_align(s_label_accel, LV_TEXT_ALIGN_CENTER, 0);

    s_label_gyro = lv_label_create(container);
    lv_obj_set_style_text_align(s_label_gyro, LV_TEXT_ALIGN_CENTER, 0);

    s_label_battery = lv_label_create(container);
    lv_obj_set_style_text_align(s_label_battery, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *beep_btn = lv_button_create(container);
    lv_obj_add_event_cb(beep_btn, on_beep_button_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *beep_label = lv_label_create(beep_btn);
    lv_label_set_text(beep_label, "Beep");
    lv_obj_center(beep_label);

    // Prima lettura immediata: senza questa chiamata le label resterebbero
    // vuote per i primi 500 ms, fino al primo giro del timer periodico
    // creato qui sotto.
    refresh_cb(NULL);
    lv_timer_create(refresh_cb, DASHBOARD_REFRESH_PERIOD_MS, NULL);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Dashboard sensori creata (refresh ogni %d ms)", DASHBOARD_REFRESH_PERIOD_MS);
}
