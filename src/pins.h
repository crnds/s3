#pragma once

// Pin mapping for the JC3248W535EN (Guition 3.5", ESP32-S3-WROOM-1 N16R8).
// Source: the seller's schematic + demo pincfg.h / esp_bsp.h -- see
// esp32-s3-spec.md's "Onboard pin map". Where community write-ups disagree
// (e.g. touch INT/RST), the seller's files win.

// LCD: AXS15231B over QSPI on SPI2_HOST. No RST pin -- tied to EN.
#define S3_LCD_CS    45
#define S3_LCD_SCK   47
#define S3_LCD_D0    21
#define S3_LCD_D1    48
#define S3_LCD_D2    40
#define S3_LCD_D3    39
#define S3_LCD_TE    38   // tearing-effect pulse, once per panel refresh
#define S3_LCD_BL    1    // AO3402 MOSFET, active high, LEDC PWM

// Touch: the same AXS15231B chip, on I2C0 at 0x3B. No RST (shared with EN).
#define S3_TOUCH_SDA 4
#define S3_TOUCH_SCL 8
#define S3_TOUCH_INT 3
#define S3_TOUCH_ADDR 0x3B

// TF card: SD_MMC 1-bit (D3/CS on GPIO10 is only needed in SPI mode).
#define S3_SD_CLK    12
#define S3_SD_CMD    11
#define S3_SD_D0     13
