#pragma once

// Provisional controller: 480x320 V1.0 alone does not identify the hardware.
// Replace with ILI9486_DRIVER or ST7796_DRIVER if the module label confirms it.
#define ILI9488_DRIVER
#define TFT_MISO 33
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS 27
#define TFT_DC 26
#define TFT_RST 25

// VSPI is TFT_eSPI's default on ESP32. SD uses a separate HSPI instance.
// Do not enable USE_HSPI_PORT or use the default MISO=19 (GPS RX).
#define SPI_FREQUENCY 20000000
#define SPI_READ_FREQUENCY 5000000
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT7
