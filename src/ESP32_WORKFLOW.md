# ESP32 Workflow

## Herramientas instaladas

- `arduino-cli` en `~/.local/bin/arduino-cli` (v1.4.1, instalado manualmente)
- Core ESP32 ya instalado: `esp32:esp32` v3.3.7
- `esptool.py` disponible en `~/miniconda3/bin/esptool.py`
- Arduino IDE AppImage en `~/Desktop/arduino-ide_2.3.7_Linux_64bit.AppImage`

## Compilar un sketch

```bash
~/.local/bin/arduino-cli compile --fqbn esp32:esp32:esp32 "/path/to/sketch_folder"
```

## Subir al ESP32

Puerto USB: `/dev/ttyUSB0`

```bash
~/.local/bin/arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 "/path/to/sketch_folder"
```

## Compilar y subir en un solo comando

```bash
~/.local/bin/arduino-cli compile --fqbn esp32:esp32:esp32 --upload --port /dev/ttyUSB0 "/path/to/sketch_folder"
```

## Ver logs / Serial Monitor (115200 baud)

**Opción 1 — miniterm (Python, ya disponible):**
```bash
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
```
Salir: `Ctrl+]`

**Opción 2 — arduino-cli monitor:**
```bash
~/.local/bin/arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```
Salir: `Ctrl+C`

**Opción 3 — screen:**
```bash
screen /dev/ttyUSB0 115200
```
Salir: `Ctrl+A` luego `K`

## Notas

- Si el puerto no aparece, verificar con `ls /dev/ttyUSB*`
- El ESP32 se resetea automáticamente después de subir el firmware (Hard reset via RTS)
- Los warnings de BT BR/EDR en compilación son normales, no son errores
- Baud rate de todos los sketches de este proyecto: **115200**
