# JC3248W535EN (ESP32-S3) — Specification

**Sources:**
- **Seller:** the official seller package in `~/JC3248W535EN`, from Shenzhen Jingcai Intelligent (JCZN / Guition): the schematic, spec sheet, user manual and demo code.
- **Read from the board:** `esptool.py` v4.11.0 on 2026-09-23.
- **Datasheet:** anything marked *datasheet* comes from Espressif's ESP32-S3 documentation.

Where the seller's files and community sources disagree, the seller's schematic and demo code are treated as correct.

## Board *(seller spec sheet [S1])*

| Item | Value |
|---|---|
| Device name | JC3248W535EN, a 3.5-inch display module with housing |
| Model / SKU | JC3248W535C_I_Y (capacitive touch). The seller uses the name JC3248W535EN in its package |
| Controller | ESP32-S3-WROOM-1 module (N16R8): dual-core LX7 at 240 MHz, 512 KB SRAM, 384 KB ROM |
| Memory | 8 MB PSRAM (octal), 16 MB flash (matches what esptool read) |
| Display | 3.5" TFT, IPS, 320 × 480, RGB565 (65K colours) |
| Display driver | AXS15231B over QSPI (the same chip also handles touch) |
| Touch | Capacitive, through the AXS15231B (I2C) |
| Active display area | 73.4 × 49.0 mm |
| Module size | 94.5 × 62.0 mm |
| Weight | About 80 g |
| Operating voltage | 5 V (USB-C or the 4-pin power connector) |
| Power consumption | About 150 mA |
| Operating temperature | −20 °C to 70 °C |
| Storage temperature | −30 °C to 80 °C |
| Battery | Lithium battery charging and discharging, with overcharge and over-discharge protection. JST 1.25 2-pin socket, plus a battery on/off button |
| Storage | TF (microSD) card slot |
| Free IO | Up to 12 GPIOs |
| Software | Arduino IDE, ESP-IDF, MicroPython |

## Onboard pin map *(seller schematic [S2] and demo `pincfg.h` [S4])*

| Function | Signal | GPIO |
|---|---|---|
| **LCD (QSPI, SPI2_HOST, 40 MHz)** | CS | 45 |
| | SCK / PCLK | 47 |
| | D0 / D1 / D2 / D3 | 21 / 48 / 40 / 39 |
| | TE (tearing effect) | 38 |
| | RST | none. Tied to the board's EN reset line |
| **Backlight** | BL (through an AO3402 MOSFET, active high, PWM at 5 kHz in the demo) | 1 |
| **Touch (AXS15231B, I2C `0x3B`, 400 kHz)** | SDA | 4 |
| | SCL | 8 |
| | INT | 3 |
| | RST | none. Shared with the LCD reset (EN) |
| **Audio (NS4168 I2S amp)** | BCLK | 42 |
| | LRCLK | 2 |
| | DIN (data from the ESP32) | 41 |
| | Amp mode (SD_MD) | Pulled to battery voltage through 1 MΩ, so the amp is always on. No GPIO |
| **TF / microSD (SD_MMC, 1-bit)** | CLK | 12 |
| | CMD (MOSI) | 11 |
| | D0 (MISO) | 13 |
| | D3 / CS (pulled up; only needed in SPI mode) | 10 |
| **Battery voltage** | ADC, through a 33 k / 100 k divider | 5 |
| **Buttons** | BOOT | 0 |
| | RST | EN |
| **USB-C (native)** | D− / D+ | 19 / 20 |
| **UART0 (JST 1.25 4-pin power connector)** | TX / RX | 43 / 44 |

**Battery reading:** `Vbat = Vadc × 133 / 100` (about 1.33×). A full 4.2 V battery reads about 3.16 V at the pin. Use 11 dB attenuation, which `analogReadMilliVolts()` does by default. GPIO5 is also on the 8-pin IO connector, so don't use it for anything else if you're measuring the battery.

**Touch points:** the seller's driver (`esp_lcd_axs15231b.c`) is **single-touch only**: `AXS_MAX_TOUCH_NUMBER = 1` and `CONFIG_ESP_LCD_TOUCH_MAX_POINTS = 1`.
- **Two fingers:** if a second finger is down, the driver discards the whole reading, so a two-finger touch registers as no touch.
- **Hardware:** a comment in the driver ("1 Point: 8 bytes; 2 Point: 14 bytes") and third-party descriptions of the AXS15231B suggest the chip can report 2 points. Getting that working would mean changing the driver and testing it; it isn't confirmed on this board.
- **Gestures:** the touch report also has a gesture byte, which the seller's driver ignores.

**Not connected on the board:** GPIO35, 36 and 37 (used internally by the module's octal PSRAM).

## Connectors *(seller spec sheet [S1] and schematic [S2])*

| Connector | Type | Pins |
|---|---|---|
| Type-C | USB-C | Power and native USB (flashing and serial) |
| Power / UART (F1) | JST 1.25, 4-pin | 1 VIN (5 V), 2 TXD (U0TXD, GPIO43), 3 RXD (U0RXD, GPIO44), 4 GND |
| IO port (P2) | JST 1.25, 8-pin | 1 GPIO5, 2 GPIO6, 3 GPIO7, 4 GPIO15, 5 GPIO16, 6 GPIO46, 7 GPIO9, 8 GPIO14 |
| Expansion (P3) | JST 1.25, 4-pin | 1 GND, 2 3.3V, 3 GPIO17, 4 GPIO18 |
| Expansion (P4) | HC 1.0, 4-pin | 1 GND, 2 3.3V, 3 GPIO17, 4 GPIO18 (same signals as P3) |
| Speaker (P6, "Speak") | JST 1.25, 2-pin | NS4168 VOP / VON outputs |
| Battery (P5) | JST 1.25, 2-pin | BAT+ / BAT− |
| Buttons | Tactile | BOOT (GPIO0), RST (EN), battery on/off |

**Free GPIOs for your own use:** 5, 6, 7, 9, 14, 15, 16, 46 (8-pin port) and 17, 18 (4-pin ports). Notes:
- **GPIO5** is also the battery-voltage input.
- **GPIO46** is a boot strapping pin; don't hold it high at reset.
- **GPIO17/18** make a good I2C or UART pair for sensors.
- **GPIO43/44** can also be used if you don't need the UART0 connector.

**Speaker:** the board has a built-in NS4168 class-D I2S amp driving the 2-pin **JST 1.25 mm** "Speak" socket, so no external amp is needed. A speaker with a **JST-PH 2.0 plug won't fit directly**; use a PH2.0-to-1.25 adapter lead or re-crimp the plug. The NS4168's output into 8 Ω is below 3 W, so a 3 W 8 Ω speaker is safely within its rating (it just won't reach full volume). The seller's NS4168 datasheet is in `4-Driver_IC_Data_Sheet/` for exact figures. The demo sets volume with `audio.setVolume(17)` (range 0–21).

## Display frame rate

These figures are **calculated, not measured on the board**.

| Limit | Value | Basis |
|---|---|---|
| Bus bandwidth | QSPI at 40 MHz × 4 lines = 160 Mbit/s ≈ **20 MB/s** | `AXS15231B_PANEL_IO_QSPI_CONFIG` (`pclk_hz = 40 MHz`) [S4] |
| Full frame | 320 × 480 × 2 bytes (RGB565) = **307,200 bytes** | |
| Bus ceiling (full-screen updates) | **≈ 65 fps** in theory; about 50–60 fps in practice after command and DMA overhead | 20 MB/s ÷ 307 KB |
| Panel refresh rate | **Unknown.** The AXS15231B produces a TE (tearing-effect) pulse on GPIO38 once per panel refresh; count those pulses to find the rate | Not in the seller's docs |
| Video (MJPEG demo) | **≤ 25 fps** (40 ms per frame, fixed in the code); lower if JPEG decoding takes longer | [S4] |
| LVGL UI | Below the bus ceiling. The demo redraws with a full-screen buffer and rotates the image 90° in software, which uses CPU time. Partial redraws (small widgets) are much faster than full-screen updates | [S4] |

The practical ceiling is the **40 MHz QSPI bus, at about 60 fps for full-screen updates**. The CPU work (JPEG decoding, LVGL rendering, software rotation) usually limits the frame rate well before the bus does. Running QSPI faster than 40 MHz is untested on this board.

## Media playback (video, pictures, audio)

**Sources:**
- the seller's `DEMO_MJPEG`, `DEMO_PIC` and `DEMO_MP3` code and `MjpegClass.h` [S4]
- the seller's sample files in `TF file/`, checked with `ffprobe`
- strings in the firmware that shipped on the board

All media plays from the **TF (microSD) card**, using fixed folder names at the card root.

### Video

| Item | Spec |
|---|---|
| Container | **Raw MJPEG stream**: back-to-back JPEG frames (`FFD8 … FFD9`), no container. **Not** AVI, MP4 or MOV |
| File extension | **`.mjpeg`** (the shipped firmware looks for `/mjpeg/<name>.mjpeg`) |
| Folder | **`/mjpeg`** |
| Codec | Baseline JPEG per frame, decoded in software by Espressif `esp_jpeg` (`ESP32_JPEG` library). Progressive JPEG is **not** supported |
| Resolution | **480 × 320, exactly** (landscape). The decoder doesn't scale or rotate, so other sizes display wrongly. All seller samples are 480 × 320 |
| Chroma | 4:2:0 (`yuvj420p`), as in the seller's samples; 4:4:4 also decodes |
| Output | RGB565 big-endian into a 300 KB PSRAM frame buffer (480 × 320 × 2) |
| Frame rate | **25 fps maximum**. The demo waits for 40 ms per frame. If a frame takes longer than 40 ms to decode, the video plays slower (no frames are dropped) |
| Largest single frame | **300 KB** (307,200 bytes, the `mjpeg_buf` size). Real frames are far smaller; the seller's samples average 8–26 KB per frame |
| Suggested bitrate | About 10–25 KB per frame, or roughly 2–5 Mbit/s at 25 fps. Bigger frames decode slower and lower the frame rate |
| Audio | **None.** The MJPEG stream carries no audio, and the demo doesn't play video and sound together. MP3 is a separate demo |
| Maximum file size | **4 GB per file** (FAT32 limit). The player reads frames one at a time, so file length has no other limit |
| Card | FAT32; SD_MMC 1-bit mode at 20 MHz (up to about 2.5 MB/s). That's enough for 25 fps at the suggested bitrate |
| Required setting | PSRAM at **120 MHz** (the seller's replacement `esp32s3` SDK folder) for smooth MJPEG playback, according to the seller's notes |

**Seller sample videos** (`TF file/mjpeg`, all 480 × 320, 4:2:0):

| File | Size | Frames | Average per frame | Length at 25 fps |
|---|---|---|---|---|
| `极乐净土480320.mjpeg` | 67.3 MB | 3,968 | 16.6 KB | about 2 min 39 s |
| `ABC.mjpeg` | 41.5 MB | 1,623 | 25.0 KB | about 1 min 5 s |
| `my0.mjpeg` | 50.5 MB | 6,252 | 7.9 KB | about 4 min 10 s |

**Converting a video with ffmpeg:**
```sh
ffmpeg -i input.mp4 -an \
  -vf "fps=25,scale=480:320:force_original_aspect_ratio=decrease,pad=480:320:(ow-iw)/2:(oh-ih)/2" \
  -pix_fmt yuvj420p -q:v 4 -f mjpeg output.mjpeg
```
- `-q:v` sets quality, from 2 (best, biggest) to 31 (worst). In a test re-encoding a seller sample, **3–5** gave about 9–14 KB per frame, similar to the seller's samples; 7 and above gave about 7 KB. Frame size depends on the content, so check the output file size.
- If playback runs slow, lower `fps` to 20 or raise `-q:v` to make frames smaller.
- To fill the screen and crop instead of adding black bars, use `scale=480:320:force_original_aspect_ratio=increase,crop=480:320`.
- `-an` removes the audio. To play the soundtrack, export it separately as MP3. The demo can't sync it with the video.

### Pictures

| Item | Spec |
|---|---|
| Format | Baseline JPEG (`.jpg`) |
| Folder | **`/pic`** |
| Resolution | **480 × 320, exactly** (no scaling) |
| Chroma | 4:2:0 or 4:4:4 (the seller's samples use both) |
| Maximum file size | **300 KB** (307,200 bytes, `MAX_PIC_FILE_SIZE`). Larger files are cut off and won't decode. The seller's samples are 65–118 KB |

To convert an image: `ffmpeg -i in.png -vf "scale=480:320:force_original_aspect_ratio=increase,crop=480:320" -q:v 3 out.jpg`

### Audio

| Item | Spec |
|---|---|
| Format | **MP3** (the demo and the shipped firmware look for `.mp3`). The `ESP32-audioI2S` library can also play AAC, WAV, FLAC, M4A, OGG and Opus if you write your own code |
| Folder | **`/music`** |
| Sample rate / bitrate | The seller's samples are 44.1 or 48 kHz stereo at 128–320 kbps. All of these play |
| Output | I2S to the NS4168 amp, then to the 2-pin JST 1.25 speaker socket. The amp is mono: it plays the left channel, the right channel or a mix, depending on its mode pin (not confirmed for this board) |
| Volume | `audio.setVolume(0–21)`; the demo uses 17 |

### TF card layout

```
/            (FAT32)
├── mjpeg/   *.mjpeg   480×320, ≤25 fps
├── pic/     *.jpg     480×320, ≤300 KB
└── music/   *.mp3
```

The shipped firmware also loads `/fonts/UI_FONT_16.bin` for its UI. It isn't clear whether that file is read from the TF card or from the internal flash partition.

## Identified hardware *(read from the board)*

| Item | Value |
|---|---|
| Chip | ESP32-S3, QFN56 package, revision v0.2 |
| Variant | N16R8 (16 MB flash, 8 MB PSRAM) |
| Crystal | 40 MHz |
| Wireless | Wi-Fi and Bluetooth LE |
| Flash | 16 MB, quad mode (QIO), 3.3 V (set by eFuse) |
| Flash chip | JEDEC manufacturer `0x68` (BoyaMicro), device `0x4018` (128 Mbit) |
| PSRAM | 8 MB, built into the chip package, 3.3 V (AP_3v3) |
| USB | Native USB-Serial/JTAG, VID `0x303A`, PID `0x1001` |
| USB link speed | **USB 2.0 Full Speed, 12 Mbit/s** (`bcdUSB` 0x0200, macOS `Device Speed = 1`, 64-byte packets) |
| Serial port (macOS) | `/dev/cu.usbmodem1101` |
| MAC | `28:84:85:49:DB:10` |

### Firmware on the board when received

- **Partition table:**
  - `nvs` at 0x9000, 20K
  - `otadata` at 0xE000, 8K
  - `app0` at 0x10000, 2M
  - `app1` at 0x210000, 2M (empty)
  - `ffat` at 0x410000, 12160K
  - `coredump` at 0xFF0000, 64K
- **App in `app0`:** Arduino-built, compiled 27 Aug 2024, ESP-IDF v5.1.4-497.
- **Seller's factory image** (`8-Burn operation/Burn files/JC3248W535C_I_Y_EN-80M.bin`, 1.7 MB, DIO 80 MHz): a **different build** (compiled 1 Aug 2024, ESP-IDF v5.1.4-586) with the same partition table.
- **To get back the exact original state,** use the full backup at `~/esp32-backups/esp32s3_28848549db10_20260923.bin` (SHA-256 `690aec26…c486ca`), not the seller's image.

## Core specs *(datasheet)*

- **CPU:** dual-core Xtensa LX7, up to 240 MHz, vector instructions for AI/DSP work
- **Memory:** 512 KB SRAM, 384 KB ROM, 16 KB RTC SRAM
- **Wi-Fi:** 2.4 GHz 802.11 b/g/n, up to 150 Mbps
- **Bluetooth:** LE 5.0 (long range, 2M PHY, advertising extensions). **No Classic Bluetooth**, so A2DP and Serial Port Profile won't work. The seller's user manual mentions "Classic Bluetooth/BLE 4.2", but that text is copied from the original ESP32 and is wrong for the S3
- **Low power:** ULP coprocessor (RISC-V and FSM), deep sleep at roughly 7–8 µA (chip only; the board draws more)

## Peripherals *(datasheet)*

| Peripheral | Details |
|---|---|
| GPIO | 45 pins on the chip; only about 12 are free on this board (see Connectors) |
| ADC | 20 channels, 12-bit. ADC1: GPIO1–10. ADC2: GPIO11–20 (stops working while Wi-Fi is on) |
| Touch | 14 channels (GPIO1–14) |
| UART | 3 |
| I2C | 2 (I2C0 is used for touch) |
| I2S | 2 (I2S0 is used for the speaker amp) |
| SPI | SPI2 is used for the LCD (QSPI); SPI3 is free. SPI0/1 are taken by flash and PSRAM |
| SD/MMC | SDIO host, 2 slots (one is used for the TF card) |
| TWAI | CAN 2.0 compatible |
| PWM | LEDC (8 channels; the backlight uses one), MCPWM (2 units) |
| Other | RMT, pulse counter, GDMA, temperature sensor, USB OTG 1.1 |
| Security | Secure Boot v2, flash encryption, digital signature, HMAC, AES/SHA/RSA hardware |

## Pins to avoid

| Pins | Reason |
|---|---|
| GPIO26–32 | Used by the flash chip (inside the module) |
| GPIO35, 36, 37 | Used by the octal PSRAM (not connected on the board) |
| GPIO19, 20 | USB D− / D+. Using them cuts off the USB serial connection |
| GPIO0 | BOOT button. Low at reset enters download mode |
| GPIO3, 45, 46 | Boot strapping pins (3 = touch INT, 45 = LCD CS, 46 = IO port). Don't hold them at a fixed level at reset |
| 1, 2, 3, 4, 8, 10–13, 21, 38–42, 45, 47, 48 | Used by the display, touch, audio and TF card on this board |
| 43, 44 | UART0, on the 4-pin power connector (usable only if you don't need that UART) |

## Development setup

### Arduino IDE *(seller user manual [S3] and demo notes [S4])*

- **Arduino-ESP32 core:** **v3.0.2**. The demos were built against this version, which uses ESP-IDF 5.1.
- **LVGL:** **v8.3.x** (the demo needs ≥ 8.3.9 and < v9; the manual shows 8.3.3).
- **Libraries:** copy the bundled libraries from `1-Demo/Demo_Arduino/libraries` into your Arduino libraries folder: `lvgl`, `ESP32-audioI2S-3.0.12`, `JPEGDEC-1.6.1` and `ESP32_JPEG`.
- **Tools menu settings:**
  - Board: **ESP32S3 Dev Module**
  - USB CDC On Boot: **Enabled**
  - Flash Mode: **QIO 120MHz** (as in the manual; QIO 80MHz is the safe choice)
  - Flash Size: **16MB (128Mb)**
  - Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
  - PSRAM: **OPI PSRAM**
  - Programmer: **esptool**
- **MJPEG demo:** it needs PSRAM at 120 MHz. That means replacing the Arduino core's `esp32s3` SDK folder with the seller's version, `1-Demo/Demo_Arduino/esp32s3`, at `…/Arduino15/packages/esp32/tools/esp32-arduino-libs/idf-release_v5.1-bd2b9390ef/esp32s3`. Back up the original folder first.
- **PIC, MJPEG and MP3 demos:** these need a TF card containing the `pic`, `mjpeg` and `music` folders (copy them from `1-Demo/Demo_Arduino/TF file`). Without the card the screen stays black.
- **Display driver:** the demos use the ESP-IDF `esp_lcd` panel API with the seller's own `esp_lcd_axs15231b.c/.h` and `esp_lcd_touch.c/.h`, not TFT_eSPI (which doesn't support the AXS15231B). Reuse those files from `DEMO_LVGL` in your own projects.
- **Screen rotation:** set with `LVGL_PORT_ROTATION_DEGREE` (0, 90, 180 or 270) in `DEMO_LVGL.ino`. The demo defaults to 90.

### PlatformIO

Local tooling: PlatformIO at `~/.platformio` (espressif32 platform, Arduino framework). esptool is at `~/.platformio/packages/tool-esptoolpy/esptool.py`.

```ini
[env:jc3248w535]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = qio
board_upload.flash_size = 16MB
board_build.partitions = huge_app.csv      ; or default_16MB.csv for OTA
build_flags =
  -DBOARD_HAS_PSRAM
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_USB_MODE=1
upload_port = /dev/cu.usbmodem1101
monitor_port = /dev/cu.usbmodem1101
monitor_speed = 115200
```

The seller's demos target Arduino-ESP32 v3.0.x (ESP-IDF 5.1). The stock PlatformIO `espressif32` platform may ship the older 2.x core, which doesn't have the `esp_lcd` QSPI API the demos rely on. If the build fails, use a platform package that provides core 3.x (for example the community `pioarduino` platform).

### ESP-IDF

Set these in `menuconfig`:
- **Flash size:** 16 MB, QIO
- **PSRAM:** Component config → ESP PSRAM → Support for external SPI RAM, mode **Octal**, 80 MHz (120 MHz needs the experimental option)
- **Console:** USB Serial/JTAG

Use the `esp_lcd_axs15231b` component (the seller supplies it in the demos, and it's also available from the Espressif component registry).

## Development notes

- **USB speed and baud rate:** the board connects at USB 2.0 **Full Speed (12 Mbit/s)** through the ESP32-S3's built-in USB. There's no USB-to-serial chip (unlike the CYD's CH340), so **the baud rate is ignored**: `Serial.begin(115200)` or any other value works, and the serial monitor shows the right text at any setting. Measured: esptool's flash read ran at only ~92 kbit/s (a limit of esptool's read command, not of USB). The seller's manual shows an upload at ~943 kbit/s. Baud rate only matters on the UART0 connector (GPIO43/44), which is a real serial port (default 115200).
- **USB mode:** the board runs in Serial/JTAG mode by default. For it to act as a keyboard, mouse or MSC device, switch to USB-OTG (TinyUSB) mode (`ARDUINO_USB_MODE=0`).
- **Debugging:** the built-in USB-JTAG works with OpenOCD (`board/esp32s3-builtin.cfg`), so no external probe is needed. Use `debug_tool = esp-builtin` in PlatformIO.
- **Board won't upload:** hold BOOT, tap RST, then release BOOT to force download mode. The seller's manual says to power-cycle the board before re-uploading if the running program stops upload from starting.
- **Serial port disappears:** the port can vanish briefly after a reset or if the firmware crashes early. Re-plug the board or use the BOOT/RST sequence above.
- **Using PSRAM:** check it at runtime with `psramFound()` and `ESP.getPsramSize()`. Allocate from it with `ps_malloc()` or `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`. Put LVGL draw buffers in PSRAM.
- **Tearing:** the demo uses the TE pin (GPIO38) to sync frame updates. Keep it configured to avoid tearing.
- **Seller's flashing tool:** Espressif Flash Download Tool 3.9.3 (Windows) is in `8-Burn operation/`. On macOS use esptool instead (see below).

## Useful commands

```sh
ESPTOOL=~/.platformio/packages/tool-esptoolpy/esptool.py
PORT=/dev/cu.usbmodem1101

python3 $ESPTOOL --port $PORT flash_id                     # chip and flash info
python3 $ESPTOOL --port $PORT read_flash 0 0x1000000 backup.bin   # full 16 MB backup (about 25 min)
python3 $ESPTOOL --port $PORT erase_flash                  # erase the whole flash

# Restore the exact original state from the full backup
python3 $ESPTOOL --port $PORT write_flash 0x0 ~/esp32-backups/esp32s3_28848549db10_20260923.bin

# Flash the seller's factory image (a different build from what shipped on this board)
python3 $ESPTOOL --port $PORT write_flash 0x0 ~/JC3248W535EN/8-Burn\ operation/Burn\ files/JC3248W535C_I_Y_EN-80M.bin

pio device monitor -p $PORT -b 115200                      # serial monitor
```

## References

### Seller documentation (primary, in `~/JC3248W535EN`)

- **[S1]** `2-Specification/JC3248W535 Specifications-EN.pdf`: the product spec sheet (parameters, interface layout, dimensions)
- **[S2]** `5-IO pin distribution/JC3248W535-1.png` and `JC3248W535-2.png`: the schematic (JC3248W535 V1.0)
- **[S3]** `6-User_Manual/Getting started JC3248W535.pdf`: Arduino IDE setup and board settings
- **[S4]** `1-Demo/Demo_Arduino/`: `Must see for use.txt`, `DEMO_MP3/pincfg.h`, `DEMO_LVGL/esp_bsp.h`, `esp_lcd_axs15231b.h` (pins, QSPI clock, touch address)
- **[S5]** `4-Driver_IC_Data_Sheet/`: AXS15231B datasheet (V0.5), NS4168 datasheet, ESP32-S3 and ESP32-S3-WROOM-1 datasheets
- **[S6]** `3-Structure_Diagram/`: housing photos and DXF panel cut-outs
- **[S7]** `8-Burn operation/`: factory firmware image and flashing instructions
- Seller website: <http://www.jczn1688.com/>

### Community sources (secondary)

These agree with the seller's files except where noted.

1. atomic14 — [Guition JC3248W535 (3.5" ESP32-S3): specs, gotchas & where to buy](https://www.atomic14.com/esp32/boards/guition-jc3248w535/)
2. LVGL Forum — [JC3248W535 – axs15231b – driver](https://forum.lvgl.io/t/jc3248w535-axs15231b-driver/18707)
3. Home Assistant Community — [JC3248W535 (Guition 3.5") ESPHome config](https://community.home-assistant.io/t/jc3248w535-guition-3-5-config/791363)
4. F1ATB — [ESP32-S3 3.5 inch Capacitive Touch IPS Display – Setup](https://f1atb.fr/home-automation/esp32-s3/esp32-s3-3-5-inch-capacitive-touch-ips-display-setup/). *Note:* its touch INT 11 / RST 12 and its UART connector pin order don't match the seller's schematic; the seller's values are used above.
5. ESPHome issue #6653 — [I²S Audio Speaker on JC3248W535 (NS4168)](https://github.com/esphome/issues/issues/6653)
6. LovyanGFX issue #699 — [Support for JC3248W535 (3.5", ESP32-S3 N16R8)](https://github.com/lovyan03/LovyanGFX/issues/699)
7. Let's Control It forum — [AXS15231 on JC3248W535EN](https://www.letscontrolit.com/forum/viewtopic.php?t=10480)
8. Arduino Forum — [Need help with the new module](https://forum.arduino.cc/t/need-help-with-the-new-module/1327022)

### Espressif

9. [ESP32-S3 Series Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
10. [ESP32-S3-WROOM-1 / WROOM-1U Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
11. [ESP32-S3 Hardware Design Guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/esp-hardware-design-guidelines-en-master-esp32s3.pdf)
12. [ESPHome I²S Audio Speaker component](https://esphome.io/components/speaker/i2s_audio/)
13. [ESP32-S3 pinouts (community)](https://github.com/mcuw/esp32-s3-pinouts)
