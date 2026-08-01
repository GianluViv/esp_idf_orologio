#pragma once

/*
 * Modulo sensor_dashboard.
 *
 * Crea la schermata LVGL che mostra i valori letti dall'hardware
 * aggiunto in questa fase del progetto (RTC, IMU, batteria) e un bottone
 * per far suonare il buzzer. E' il modulo applicativo che "mette insieme"
 * i driver hardware (rtc, imu, battery, buzzer) e LVGL: i driver non
 * sanno nulla dell'interfaccia grafica, questo modulo non sa nulla dei
 * dettagli dei protocolli I2C/ADC sottostanti.
 */

/*
 * Crea i widget della dashboard sullo schermo attivo e avvia un timer
 * LVGL periodico (ogni 500 ms) che ne aggiorna il contenuto rileggendo i
 * sensori. Va chiamata una sola volta, dopo pcf85063_init(), imu_init(),
 * battery_init() e buzzer_init().
 */
void sensor_dashboard_create(void);
