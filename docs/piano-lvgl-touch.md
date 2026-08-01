# Piano di lavoro — Integrazione LVGL con supporto touch

> **Stato: piano completato.** Tutte le 10 fasi sono state implementate e
> verificate su hardware reale (build, flash, e verifica visiva/tattile
> diretta sulla board). Il documento resta come riferimento storico del
> percorso seguito; per lo stato attuale dell'architettura vedere
> [lvgl-architettura.md](lvgl-architettura.md) e la sezione Architecture di
> [../CLAUDE.md](../CLAUDE.md).

Stato del progetto alla stesura di questo piano: `main/main.c` conteneva solo
il loop di avvio e il modulo `diagnostics`. Non esisteva ancora nessun driver
per display, touch, I2C, RTC o IMU. Questo piano partiva quindi da zero e
arrivava a un'interfaccia LVGL funzionante e reattiva al tocco sul display
rotondo 1.69" della board.

Il documento [lvgl-architettura.md](lvgl-architettura.md) spiega **come**
funzionano i pezzi descritti qui sotto (thread, buffer, callback). Questo
piano descrive invece **in che ordine** costruirli.

## Obiettivo finale

Un task FreeRTOS dedicato che fa girare LVGL, disegna almeno un widget di
prova (es. una label o un bottone) sul display ST7789V2, e riceve in modo
affidabile gli eventi di tocco dal controller CST816T, senza bloccare il
resto del firmware (RTC, IMU, buzzer che verranno aggiunti in seguito).

## Prerequisiti da verificare prima di iniziare

| # | Prerequisito | Perché | Stato |
|---|---|---|---|
| 1 | PSRAM abilitata in sdkconfig (`CONFIG_SPIRAM`) | LVGL a 240×280 con buffer anche solo parziali (es. 1/10 di frame in RGB565) usa più RAM di quanta ne abbia la SRAM interna insieme al resto del firmware; i buffer vanno allocati in PSRAM | ✅ Fatto (8MB Octal PSRAM confermati a runtime) |
| 2 | Componente ESP-IDF Component Manager attivo (`idf_component.yml` in `main/`) | Serve per scaricare `lvgl/lvgl` e i driver `esp_lcd_*` dal Component Registry invece di vendorizzarli a mano | ✅ Fatto |
| 3 | Verifica indirizzo I2C e collegamento fisico di touch/IMU/RTC sullo stesso bus (SCL=10, SDA=11) | I tre device condividono il bus: il nuovo driver `i2c_master` di IDF 5.x permette più `i2c_master_dev_handle_t` sullo stesso bus senza mutex manuali, ma va inizializzato una volta sola e riutilizzato | ✅ Fatto (touch verificato; RTC/IMU non ancora implementati, riutilizzeranno lo stesso bus) |

## Fasi

| # | Fase | Stato |
|---|---|---|
| 1 | Abilitare PSRAM e verificare il boot con PSRAM attiva | ✅ Fatto |
| 2 | Aggiungere Component Manager e dipendenze (`lvgl/lvgl`, driver pannello ST7789) | ✅ Fatto — `lvgl/lvgl@9.5.0`, `espressif/esp_lcd_touch_cst816s@1.1.1`; driver ST7789V2 incluso nativamente in `esp_lcd`, nessuna dipendenza extra necessaria |
| 3 | Modulo `display` — init bus SPI e panel handle ST7789V2, controllo backlight | ✅ Fatto (`main/display.h/.c`) |
| 4 | Modulo `lvgl_port` — init LVGL, buffer, flush callback, tick timer, task dedicato | ✅ Fatto (`main/lvgl_port.h/.c`) |
| 5 | Test di fumo: pattern statico a schermo intero senza ancora usare LVGL | ✅ Fatto — bug reale trovato e corretto: serviva `data_endian = LCD_RGB_DATA_ENDIAN_LITTLE` sul panel config, altrimenti i canali colore arrivavano permutati |
| 6 | Test LVGL: un widget semplice (label "Hello") a schermo | ✅ Fatto — bug reale trovato e corretto: il loop del task LVGL doveva avere un tetto massimo al delay fra chiamate a `lv_timer_handler()`, altrimenti il "refresh timer" a scomparsa di LVGL 9 lasciava il task addormentato per ~50 giorni dopo il primo render (vedi [lvgl-architettura.md](lvgl-architettura.md)) |
| 7 | Modulo `touch` — init I2C condiviso, driver CST816T, lettura coordinate | ✅ Fatto (`main/touch.h/.c`) |
| 8 | Integrazione touch → LVGL (`lv_indev`) | ✅ Fatto |
| 9 | Test end-to-end: bottone LVGL che reagisce al tocco (log su pressione) | ✅ Fatto — verificato sia via log seriale (evento `LV_EVENT_CLICKED`) sia visivamente (feedback del bottone) |
| 10 | Pulizia, gestione errori, aggiornamento CLAUDE.md/architettura con lo stato reale del codice | ✅ Fatto |

## Dettaglio delle fasi

### Fase 1 — PSRAM

- `idf.py menuconfig` → `Component config → ESP PSRAM → Support for external, SPI-connected RAM`, abilitare e impostare la modalità corretta per l'ESP32-S3R8 (Octal SPI PSRAM, 8MB).
- Verificare a runtime con `esp_psram_get_size()` o osservando l'heap PSRAM nel log di `diagnostics_print_chip_info` (da estendere per stampare anche l'heap PSRAM, non solo quello interno).
- Criterio di completamento: il chip riporta 8MB di PSRAM disponibili al boot.

### Fase 2 — Component Manager

- Creare `main/idf_component.yml` con dipendenza `lvgl/lvgl` (versione 9.x, coerente con IDF 5.5.4) e il componente driver per il pannello ST7789 (`espressif/esp_lcd_st7789` o equivalente sul Component Registry — da verificare il nome esatto al momento dell'implementazione, la disponibilità dei pacchetti cambia nel tempo).
- `idf.py build` scarica i pacchetti in `managed_components/` (da tenere fuori da git, va in `.gitignore`).
- Criterio di completamento: build pulita che scarica le dipendenze senza errori.

### Fase 3 — Modulo `display` (`main/display.h` / `main/display.c`)

- Inizializzazione del bus SPI (`spi_bus_initialize`) sui pin DC/CS/CLK/DIN/RST (4/5/6/7/8).
- Creazione dell'`esp_lcd_panel_io_handle_t` e dell'`esp_lcd_panel_handle_t` per il driver ST7789V2, con reset, init sequence, e `esp_lcd_panel_disp_on_off`.
- Controllo backlight sul pin 15: partire con un semplice `gpio_set_level` on/off; il dimming via LEDC/PWM è un miglioramento rimandabile a dopo.
- Espone una funzione tipo `esp_lcd_panel_handle_t display_init(void)` che il modulo `lvgl_port` userà per registrare la callback di flush.

### Fase 4 — Modulo `lvgl_port` (`main/lvgl_port.h` / `main/lvgl_port.c`)

- `lv_init()`, allocazione di uno o due draw buffer in PSRAM (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`), dimensione tipica: una frazione del frame (es. 1/10) è sufficiente e riduce la pressione sulla RAM rispetto a un frame buffer intero.
- Registrazione della callback di flush che scrive il buffer LVGL sul pannello tramite `esp_lcd_panel_draw_bitmap`.
- Un `esp_timer` periodico (es. ogni 5 ms) che chiama `lv_tick_inc()`.
- Un task FreeRTOS dedicato (priorità media, stack abbondante, es. 8KB) il cui unico compito è chiamare `lv_timer_handler()` in loop con un piccolo delay; è il thread "proprietario" di LVGL (vedi documento di architettura per il perché serve un singolo thread).

### Fase 5-6 — Test progressivi

- Prima un pattern manuale (riempi lo schermo di un colore via `esp_lcd_panel_draw_bitmap` diretto) per validare bus SPI e init pannello **senza** la complessità di LVGL nel mezzo: se qualcosa non va, si sa già che non è colpa di LVGL.
- Poi un widget LVGL minimo (`lv_label_create` con testo statico) per validare l'intera catena flush/buffer/task.

### Fase 7 — Modulo `touch` (`main/touch.h` / `main/touch.c`)

- Init del bus I2C condiviso con il nuovo driver `driver/i2c_master.h` (SCL=10, SDA=11); questo stesso bus handle verrà riusato in futuro da RTC (PCF85063A) e IMU (QMI8658), quindi va pensato come risorsa condivisa fin da subito (es. esposta da un piccolo modulo `i2c_bus` invece che creata dentro `touch.c`).
- Driver per il controller CST816T: lettura registri via I2C per ottenere coordinate X/Y e stato di pressione. Il pin INT (GPIO14) può essere usato per un interrupt "dati pronti" invece del polling continuo — da valutare in base a quanto vogliamo approfondire gli interrupt GPIO in questa fase didattica.
- Il pin RST (GPIO13) del touch va gestito nell'init (reset hardware del controller).

### Fase 8 — Integrazione con LVGL

- Registrazione di un `lv_indev` di tipo `LV_INDEV_TYPE_POINTER` la cui callback di lettura interroga il modulo `touch` e traduce le coordinate raw nel sistema di coordinate del display (attenzione a eventuale rotazione/mirroring tra pannello e touch, molto comune su questi moduli round).

### Fase 9 — Test end-to-end

- Un bottone LVGL a schermo che logga un messaggio quando premuto: verifica visiva (rendering) + funzionale (touch) insieme.

### Fase 10 — Consolidamento

- Aggiornare `CLAUDE.md` (sezione Architecture) per riflettere i nuovi moduli.
- Rivedere il documento di architettura LVGL con eventuali scostamenti tra il progettato e l'implementato.

## Rischi e punti di attenzione

- **Bus I2C condiviso**: sbagliare la gestione del bus tra touch/RTC/IMU è il rischio più concreto di bug intermittenti; va centralizzato in un solo punto di init.
- **Memoria**: senza PSRAM il progetto non parte nemmeno con un buffer LVGL minimo; verificare la fase 1 per prima.
- **Thread safety di LVGL**: qualunque chiamata a funzioni `lv_*` da un task diverso da quello che gira `lv_timer_handler()` deve passare da un mutex (vedi documento di architettura). Va deciso da subito, prima di aggiungere altri task che vogliono aggiornare la UI (es. un task orologio che aggiorna l'ora a schermo).
- **Rotazione display round**: il pannello è tondo montato su un frame 240×280 rettangolare; va verificato se serve un offset di rendering (gap) coerente con il datasheet ST7789V2 in `docs/datasheet/`.

## Criteri di completamento del piano

Il piano si considera concluso quando la fase 9 passa: un bottone disegnato da LVGL risponde a un tocco fisico sul display, confermato via log seriale, senza crash o memory leak visibili durante un funzionamento continuativo di almeno 10 minuti.
