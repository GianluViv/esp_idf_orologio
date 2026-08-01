#pragma once

#include "driver/i2c_master.h"

/*
 * Modulo i2c_bus.
 *
 * Il touch (CST816T), l'RTC (PCF85063A) e l'IMU (QMI8658) sono tutti
 * collegati agli stessi due pin fisici SCL/SDA (GPIO10/GPIO11, vedi tabella
 * pin in CLAUDE.md): condividono cioe' lo stesso bus I2C, pur avendo
 * indirizzi diversi sul bus. Il driver "nuovo" di ESP-IDF 5.x
 * (driver/i2c_master.h) rappresenta il bus con un unico handle
 * (i2c_master_bus_handle_t) al quale si aggiungono via software i vari
 * device (i2c_master_bus_add_device()): il bus va pero' creato una sola
 * volta, altrimenti si otterrebbe un errore "GPIO gia' in uso" al secondo
 * tentativo.
 *
 * Questo modulo risolve il problema con un'inizializzazione "pigra" (lazy):
 * la prima volta che un modulo driver (touch, rtc, imu, in qualunque
 * ordine vengano inizializzati) chiama i2c_bus_get_handle(), il bus viene
 * creato; tutte le chiamate successive, da qualunque modulo, restituiscono
 * semplicemente lo stesso handle gia' pronto. Questo evita di dover
 * imporre in main.c un ordine di inizializzazione rigido (es. "il bus I2C
 * va creato per primo, prima di touch_init()"): ogni modulo che ha bisogno
 * del bus se lo procura da solo, alla bisogna.
 */

/*
 * Restituisce l'handle del bus I2C condiviso (SCL=GPIO10, SDA=GPIO11),
 * creandolo la prima volta che viene chiamata. Puo' essere chiamata da
 * touch_init(), pcf85063_init(), imu_init() in qualsiasi ordine: solo la prima
 * chiamata in assoluto esegue davvero la configurazione hardware del bus.
 */
i2c_master_bus_handle_t i2c_bus_get_handle(void);
