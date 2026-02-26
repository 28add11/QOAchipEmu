/* BIT BANGED SPI 
 Turns out the pins we need (uio 0-3) aren't wired to the SPI core on the RP2040 in the right way for the ASIC to accept
 So we get to bit-bang our own version */

#include "bitbangSPI.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "tt_pins.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"


void spiWrite(uint8_t data) {
	gpio_put(PIN_CS, 0);

	// 8 bits per byte, sending 1 byte at a time
	for (int i = 0; i < 8; i++) {
		int dataBit = (data >> (7 - i)) & 0x01; // Shift data out MSB first
		gpio_put(PIN_MOSI, dataBit);
		sleep_us(SPI_CLK_PERIOD_US / 2); // /2 is because full period consists of one rising edge and one falling edge
		gpio_put(PIN_SCK, 1);
		sleep_us(SPI_CLK_PERIOD_US / 2);
		gpio_put(PIN_SCK, 0); // Default clock value is zero
	}

	gpio_put(PIN_CS, 1);
	sleep_us(SPI_CLK_PERIOD_US);
}


int spiRead() {
	gpio_put(PIN_CS, 0); // Still need to select the thing to read it
	gpio_put(PIN_MOSI, 0); // send only zeros

	int result = 0;
	// 8 bits per byte, reading 1 byte at a time
	for (int i = 0; i < 8; i++) {
		sleep_us(SPI_CLK_PERIOD_US / 2);
		gpio_put(PIN_SCK, 1); // Sample on rising edge
		result = (result << 1) | gpio_get(PIN_MISO);
		sleep_us(SPI_CLK_PERIOD_US / 2); // /2 is because full period consists of one rising edge and one falling edge
		gpio_put(PIN_SCK, 0); // Default clock value is zero
	}

	gpio_put(PIN_CS, 1);
	sleep_us(SPI_CLK_PERIOD_US);

	return result;
}
