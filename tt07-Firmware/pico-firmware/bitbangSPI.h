#include <stdint.h>

#pragma once

#define SPI_CLK_FREQ 1000
#define SPI_CLK_PERIOD_US ((1.0/SPI_CLK_FREQ)*1000000)
#define PIN_MISO UIO2
#define PIN_CS   UIO0
#define PIN_SCK  UIO3
#define PIN_MOSI UIO1

void spiWrite(uint8_t data);

int spiRead();