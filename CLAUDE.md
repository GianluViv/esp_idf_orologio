# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP-IDF firmware project (`esp_idf_orologio`) for the **Waveshare ESP32-S3-Touch-LCD-1.69** board: ESP32-S3R8 (dual-core, 8MB PSRAM), 16MB flash, 1.69" round touch LCD (ST7789V2 driver, 240×280), CST816T capacitive touch, QMI8658 6-axis IMU, PCF85063A RTC, Li-battery charging. Datasheets, schematic and pinout diagram for the board are in [docs/](docs/).

### Key pin assignments (see `docs/ESP32-S3-Touch-LCD-1.69-Pinout.webp` for the full diagram)

| Function | GPIO |
|---|---|
| LCD DC / CS / CLK / DIN / RST / BL | 4 / 5 / 6 / 7 / 8 / 15 |
| Touch, IMU, RTC — shared I2C SCL/SDA | 10 / 11 |
| Touch INT / RST | 14 / 13 |
| RTC INT | 39 |
| Battery ADC | 1 |
| Buzzer | 42 |

## Build, flash, monitor

ESP-IDF is not sourced by default in a plain shell. Source it first:

```bash
source /home/gian/.espressif/v5.5.4/esp-idf/export.sh
```

Then, from the project root:

```bash
idf.py build
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor       # Ctrl+] to exit
idf.py -p /dev/ttyACM0 flash monitor # combined
```

Target is `esp32s3` (already set in `sdkconfig` — no need to re-run `idf.py set-target`).

If `/dev/ttyACM0` is busy, an `idf.py monitor` session may already be attached to it in another terminal — check with `fuser /dev/ttyACM0` before flashing.

To read serial output non-interactively (e.g. for a one-off check) without opening a monitor session, `pyserial` is available:

```python
import serial, time
ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
ser.setDTR(False); ser.setRTS(True); time.sleep(0.1); ser.setRTS(False)  # reset via RTS
```

### VS Code / clangd

`compile_commands.json` (needed by clangd for header resolution) is only generated after a build — the IDE will show false "file not found" errors for ESP-IDF headers until `idf.py build` has run at least once.

## Scopo del progetto e convenzioni di codice

Questo è un progetto didattico: l'obiettivo primario è che l'utente impari ad usare ESP-IDF, non solo ottenere firmware funzionante. Questo cambia il modo in cui il codice va scritto e presentato:

- **Separare implementazione da interfaccia**: ogni modulo funzionale (display, touch, RTC, IMU, buzzer, ecc.) deve esporre un header (`.h`) con l'interfaccia pubblica (funzioni, tipi, costanti) tenuto distinto dal `.c` che contiene l'implementazione. Evitare di ammassare tutto in `main.c` man mano che il progetto cresce — quando una funzionalità diventa sostanziosa, va estratta in un component sotto `components/` (vedi sezione Architecture) o quantomeno in coppie `.h`/`.c` separate dentro `main/`.
- **Commenti prolissi**: a differenza delle convenzioni generiche di terseness, in questo progetto i commenti devono spiegare per esteso *cosa fa* il codice e *perché*, non solo il non-ovvio. Il codice è materiale didattico: privilegiare la chiarezza per chi sta imparando rispetto alla concisione.
- **Spiegazione ad ogni aggiunta**: quando si aggiunge codice, spiegare all'utente la funzione del codice stesso nella risposta (non solo nei commenti) — a cosa serve, come si inserisce nell'architettura del progetto, eventuali scelte alternative scartate.
- **Leggibilità prima di tutto**: a parità di risultato, preferire sempre la soluzione più semplice e leggibile rispetto a quella più compatta, furba o performante. Evitare micro-ottimizzazioni premature, macro/trick oscuri, o astrazioni che non aiutano la comprensione: in un progetto didattico un codice che si legge facilmente vale più di uno scritto in poche righe.
- Mantenere comunque le best practice standard di programmazione C/ESP-IDF (gestione errori con `ESP_ERROR_CHECK`/codici di ritorno, naming coerente, uso di `menuconfig`/Kconfig dove sensato, ecc.).

## Architecture

Single-component project: all application code lives in `main/main.c`, registered via `main/CMakeLists.txt` (`idf_component_register`). There are no custom components yet — new functionality should either grow `main` or be split into a component under a top-level `components/` directory following standard ESP-IDF component structure if it becomes substantial (e.g. a dedicated display/touch driver component).

A devcontainer (`.devcontainer/`) based on `espressif/idf` is available for QEMU-based builds without physical hardware, but the primary workflow in this repo is building and flashing to the real board over `/dev/ttyACM0`.

## Notes

- `sdkconfig` flash size must stay `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` — the board has 16MB flash; a mismatched 2MB default causes a bootloader size-mismatch warning and wastes usable flash.
- The official Waveshare ESP-IDF example package (ST7789 driver, ST7789+LVGL, PCF85063, QMI8658 demos) is downloadable from `https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69/ESP32-S3-Touch-LCD-1.69_Demo.zip` — useful as a reference when implementing display/touch/RTC/IMU support, but its examples are pinned to the ESP32-S3-Zero pinout and need adapting to the pin table above.
