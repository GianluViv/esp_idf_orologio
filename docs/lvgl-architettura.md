# Architettura LVGL su ESP32-S3-Touch-LCD-1.69

> Questo documento descrive **come funziona** l'integrazione di LVGL in
> questo progetto: i livelli coinvolti, il flusso dati, la gestione della
> memoria e della concorrenza. Riflette il piano descritto in
> [piano-lvgl-touch.md](piano-lvgl-touch.md), **completamente implementato e
> verificato su hardware reale**: `display`, `lvgl_port` e `touch` esistono
> tutti in `main/` e sono stati testati sulla board fisica (pattern a
> schermo intero, widget LVGL, bottone reattivo al tocco). Va tenuto
> allineato al codice ogni volta che l'architettura cambia (es. quando si
> aggiungeranno RTC/IMU sullo stesso bus I2C, o si estrarrà la UI in un
> modulo dedicato).

## Cos'è LVGL e che ruolo ha nel firmware

LVGL (Light and Versatile Graphics Library) è una libreria C per interfacce
grafiche embedded: gestisce widget (bottoni, label, grafici...), layout,
stili, animazioni ed eventi di input, ma **non sa nulla** dell'hardware
specifico. Non sa come si scrive un pixel sull'ST7789V2 né come si legge una
coordinata dal CST816T: quel collegamento va scritto da noi tramite due
callback che LVGL chiama quando ne ha bisogno.

Nel firmware, LVGL non è "il programma": è una libreria che gira dentro un
task FreeRTOS come un qualsiasi altro pezzo di codice (allo stesso livello,
concettualmente, del task che in futuro leggerà l'IMU o aggiornerà l'RTC).

## I livelli dell'architettura

```
┌─────────────────────────────────────────────────────────┐
│  main.c                                                  │
│  avvia i moduli, orchestrazione generale                 │
└───────────────┬───────────────────────────┬─────────────┘
                 │                           │
                 ▼                           ▼
   ┌─────────────────────────┐   ┌──────────────────────────┐
   │  lvgl_port.c             │   │  touch.c                 │
   │  - lv_init()             │   │  - init bus I2C           │
   │  - buffer draw (PSRAM)   │   │  - lettura CST816T        │
   │  - task lv_timer_handler │◄──│  - callback lv_indev       │
   │  - esp_timer -> lv_tick  │   └──────────────────────────┘
   └───────────┬──────────────┘
               │ flush callback
               ▼
   ┌─────────────────────────┐
   │  display.c                │
   │  - bus SPI                │
   │  - esp_lcd_panel_handle_t │
   │  - esp_lcd_panel_draw_bitmap │
   └───────────┬──────────────┘
               ▼
        ST7789V2 (hardware)
```

Quattro livelli, dal basso verso l'alto:

1. **Hardware**: bus SPI verso il display, bus I2C condiviso verso il touch
   (oltre che, in futuro, RTC e IMU sullo stesso bus).
2. **Driver hardware (`display.c`, `touch.c`)**: parlano direttamente con i
   periferici tramite i driver ESP-IDF (`esp_lcd_*` per il pannello,
   `driver/i2c_master.h` per il bus I2C, più il parsing dei registri
   specifici del CST816T). Non conoscono LVGL.
3. **Ponte LVGL↔hardware (`lvgl_port.c` + le callback registrate)**:
   traduce le richieste generiche di LVGL ("disegna questo rettangolo di
   pixel", "dammi lo stato del puntatore") in chiamate ai driver del
   livello 2.
4. **LVGL core**: albero dei widget, motore di rendering, gestione eventi.
   Tutto quello che riguarda "cosa" viene disegnato (bottoni, testo, layout)
   vive qui e non deve mai preoccuparsi di SPI o I2C.

Questa separazione è la stessa filosofia già indicata in CLAUDE.md per il
resto del progetto (interfaccia separata dall'implementazione): il codice
che disegna la UI dell'orologio non dovrà mai includere `esp_lcd.h`.

## Flusso dati: come un frame arriva sul display

1. Un `esp_timer` periodico (qualche millisecondo) chiama `lv_tick_inc()`:
   dice a LVGL "è passato questo tempo", usato per animazioni e timeout.
2. Il task dedicato a LVGL chiama in loop `lv_timer_handler()`. Questa
   funzione fa tutto il lavoro interno di LVGL: controlla se qualche widget
   è "sporco" (da ridisegnare), ricalcola layout, e se serve invoca la
   callback di **flush** che noi abbiamo registrato.
3. La callback di flush riceve da LVGL un'area rettangolare e un buffer di
   pixel già renderizzati (formato RGB565, coerente col pannello). Il suo
   unico compito è spedire quei pixel al display, tipicamente chiamando
   `esp_lcd_panel_draw_bitmap()`, che a sua volta usa il bus SPI (spesso in
   DMA, per non bloccare la CPU mentre i dati escono).
4. Quando il trasferimento SPI finisce, la callback deve avvisare LVGL
   (`lv_disp_flush_ready()`) che il buffer è di nuovo libero e può essere
   riusato per il prossimo frame.

Per il touch il flusso è simmetrico ma "in lettura": LVGL, quando ha bisogno
di sapere se l'utente sta toccando lo schermo, chiama la callback di lettura
che abbiamo registrato per il nostro `lv_indev`; quella callback interroga
`touch.c`, che a sua volta legge i registri del CST816T via I2C e restituisce
coordinate e stato (premuto/rilasciato).

### Un'insidia reale: `lv_timer_handler()` e il refresh timer "a scomparsa"

Durante l'implementazione di `lvgl_port.c` ci siamo scontrati con un bug che
vale la pena documentare perché non è affatto ovvio dalla sola lettura delle
API pubbliche di LVGL.

Il task che fa girare LVGL chiama `lv_timer_handler()` in loop, e quella
funzione restituisce quanti millisecondi possono passare prima della
prossima chiamata — l'idea è lasciare che il task dorma (`vTaskDelay`) per
quel tempo invece di occupare la CPU inutilmente. Sembra ragionevole fidarsi
ciecamente di quel valore. **Non bisogna farlo.**

LVGL 9 ottimizza il refresh dello schermo con un "refresh timer" interno che,
subito dopo aver ridisegnato qualcosa, **mette se stesso in pausa**: se non
c'è nulla da ridisegnare non ha senso continuare a controllare ogni 33ms.
Il timer si riattiva da solo, automaticamente, quando qualcosa invalida un
widget (crearlo, cambiargli testo, un'animazione, ecc.). Il problema è che,
finché è in pausa, quel timer **non entra nel calcolo** del valore restituito
da `lv_timer_handler()`: se non ci sono altri timer attivi in quel momento,
il valore restituito diventa `LV_NO_TIMER_READY`, cioè `0xFFFFFFFF` —
letteralmente "puoi aspettare quasi 50 giorni".

Il sintomo osservato in pratica: al boot, il primo `lv_timer_handler()`
disegna correttamente lo sfondo dell'intero schermo (bianco, dal tema di
default), poi restituisce `4294967295`. Il nostro task, fidandosi di quel
numero, andava in `vTaskDelay` per l'equivalente di 50 giorni. Qualunque
widget creato *dopo* quel primo giro (nel nostro caso una semplice label)
veniva regolarmente marcato come "da disegnare" da LVGL, ma il task che
avrebbe dovuto accorgersene ed eseguire il disegno era addormentato — quindi
restava per sempre invisibile, e lo schermo mostrava solo lo sfondo bianco
senza testo.

La correzione in `lvgl_port.c` è imporre un **tetto massimo** al delay,
oltre al minimo già previsto per non consumare CPU inutilmente:

```c
if (sleep_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
    sleep_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
} else if (sleep_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {  // 33ms, come LV_DEF_REFR_PERIOD
    sleep_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
}
```

In questo modo il task ricontrolla comunque lo stato di LVGL con una
regolarità minima (33ms, coerente col periodo di refresh di default), a
prescindere da cosa dica il valore di ritorno. E' il pattern usato dalla
maggior parte dei port LVGL "a polling": non fidarsi del valore di ritorno
come limite superiore assoluto, solo come suggerimento per non sprecare CPU
quando è basso.

## Buffering e memoria

LVGL non richiede necessariamente un frame buffer completo in RAM. Il pattern
comune (ed è quello previsto qui) è avere uno o due **draw buffer parziali**:
porzioni dello schermo (es. 1/10 dell'altezza) che vengono renderizzate e
spedite al display a pezzi, in sequenza, finché l'intero frame non è stato
aggiornato. Vantaggi:

- Consuma molta meno RAM di un frame buffer intero (240×280×2 byte ≈ 134 KB
  per un solo frame completo in RGB565 — troppo per la sola SRAM interna
  insieme al resto del firmware).
- Con **due** buffer parziali (double buffering) si può renderizzare il
  buffer N+1 mentre il buffer N è ancora in trasferimento DMA verso il
  display, sovrapponendo calcolo e I/O.

Questi buffer vanno allocati in **PSRAM** (`heap_caps_malloc` con
`MALLOC_CAP_SPIRAM`), da cui la necessità di abilitarla nel piano di lavoro:
senza PSRAM la SRAM interna (poche centinaia di KB, condivisi con stack,
heap FreeRTOS, buffer WiFi/BT se mai usati, ecc.) non basterebbe.

Il buffer usato per il trasferimento SPI effettivo deve inoltre essere
DMA-capable (allineamento e cache coerenti con i requisiti del driver SPI di
IDF); questo è gestito dal driver `esp_lcd`, non va fatto a mano.

## Concorrenza: perché serve un solo "proprietario" di LVGL

**LVGL non è thread-safe.** Le sue strutture dati interne (albero widget,
liste di eventi, timer) non sono protette da lock. La regola pratica è:

- Un solo task chiama `lv_timer_handler()` in loop: è il task
  "proprietario", tipicamente quello creato da `lvgl_port.c`.
- Qualsiasi altro task che voglia toccare la UI (es. un futuro task che
  aggiorna l'orario mostrato a schermo, o che riceve dati dall'IMU e vuole
  aggiornare un'icona) **non deve chiamare direttamente** funzioni `lv_*`.
  Deve invece passare da un mutex condiviso (tipicamente esposto da
  `lvgl_port.h` come `lvgl_port_lock()` / `lvgl_port_unlock()`), che il task
  proprietario rilascia periodicamente tra un `lv_timer_handler()` e
  l'altro.

Questo vincolo è la ragione per cui il modulo `lvgl_port` è pensato come
punto di accesso centralizzato: espone in `lvgl_port.h` sia l'init sia le
funzioni di lock, così ogni futuro modulo che vuole aggiornare la UI sa
esattamente come farlo in sicurezza.

## Il bus I2C condiviso

Touch (CST816T), RTC (PCF85063A) e IMU (QMI8658) sono tutti sullo stesso bus
I2C (SCL=10, SDA=11). Con il driver I2C "nuovo" di ESP-IDF 5.x
(`driver/i2c_master.h`), il bus si inizializza **una sola volta**
(`i2c_new_master_bus`) e ogni periferica ottiene il proprio
`i2c_master_dev_handle_t` (`i2c_master_bus_add_device`) con il proprio
indirizzo a 7 bit. Il driver gestisce internamente l'arbitraggio degli
accessi concorrenti al bus fisico, quindi non serve un mutex applicativo per
il bus in sé — ma l'inizializzazione del bus deve avvenire in un punto solo
del firmware, altrimenti si rischia di provare a inizializzarlo due volte.

## Struttura dei moduli prevista in questo repository

Coerente con la convenzione "un modulo, un'interfaccia" già in uso per
`diagnostics`:

| File | Responsabilità | Conosce LVGL? | Conosce l'hardware? |
|---|---|---|---|
| `main/display.h/.c` | Init SPI + pannello ST7789V2, backlight | No | Sì (SPI, esp_lcd) |
| `main/touch.h/.c` | Init I2C condiviso, lettura CST816T | No | Sì (I2C) |
| `main/lvgl_port.h/.c` | `lv_init`, buffer, task, tick, lock/unlock, registrazione flush e indev | Sì | Solo tramite `display.h`/`touch.h` |
| `main/main.c` | Orchestrazione: chiama gli init in ordine, eventualmente crea i widget applicativi | Sì (uso, non init) | No |

Man mano che il progetto cresce (l'orologio vero e proprio, schermate
multiple, animazioni), il codice che costruisce i widget applicativi potrà
essere estratto ulteriormente (es. `ui.h/.c`) per non far crescere
`main.c` oltre l'orchestrazione ad alto livello — stessa logica già indicata
in CLAUDE.md per l'estrazione in `components/` quando una parte diventa
sostanziosa.

## Riferimenti

- Documentazione LVGL: struttura widget, sistema di stili ed eventi —
  utile leggerla in parallelo alla fase 6 del piano di lavoro.
- Datasheet ST7789V2 e CST816S in `docs/datasheet/` (schede tecniche del pannello e
  del controller touch di questa board).
- Pacchetto demo ufficiale Waveshare (linkato in CLAUDE.md) come riferimento
  per l'init sequence del pannello, da adattare al pinout di questa board.
