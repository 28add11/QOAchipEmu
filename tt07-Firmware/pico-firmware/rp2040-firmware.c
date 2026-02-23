#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hardware/gpio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "tt_pins.h"
#include "tt_setup.h"
#include "testfile.h"

// Clock defines
#define CLK_HZ 1000000
#define CLK_PERIOD_US ((1.0/CLK_HZ)*1000000) // Seconds to microseconds conversion

// SPI Defines
#define SPI_CLK_FREQ 1000
#define SPI_CLK_PERIOD_US ((1.0/SPI_CLK_FREQ)*1000000)
#define PIN_MISO UIO2
#define PIN_CS   UIO0
#define PIN_SCK  UIO3
#define PIN_MOSI UIO1

/* BIT BANGED SPI 
 Turns out the pins we need (uio 0-3) aren't wired to the SPI core on the RP2040 in the right way for the ASIC to accept
 So we get to bit-bang our own version */

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

void clkProjectNTimes(int n) {
	const uint slice_num = pwm_gpio_to_slice_num(CLK);
	pwm_set_enabled(slice_num, false);

	for (int i = 0; i < n; i++) {
		sleep_us(CLK_PERIOD_US / 2);
		gpio_put(CLK, 1);
		sleep_us(CLK_PERIOD_US / 2);
		gpio_put(CLK, 0);
	}
	sleep_us(CLK_PERIOD_US);
	pwm_set_enabled(slice_num, true);
}

int main()
{
    stdio_init_all();

	sleep_ms(5000);

	printf("Press anything to start\n");
	getchar();

	tt_select_design(326);


	// Reset tinytapeout design
	printf("Reset...\n");
	gpio_put(PIN_MISO, 0);
	gpio_put(PIN_CS, 0);
	gpio_put(PIN_SCK, 0);
	gpio_put(PIN_MOSI, 0);
	gpio_put(nRST, 0);
	clkProjectNTimes(10);
	gpio_put(nRST, 1); // Take out of reset

	printf("Testing project...\n");

    // Initialize pins for our bit banged spi
    gpio_set_function(PIN_MISO, GPIO_FUNC_SIO);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SIO);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SIO);
	
	gpio_put(PIN_CS, 1);

	// Start the project clock
	gpio_set_function(CLK, GPIO_FUNC_PWM);

	uint slice_num = pwm_gpio_to_slice_num(CLK);

	pwm_config cfg = pwm_get_default_config();
	pwm_config_set_clkdiv(&cfg, clock_get_hz(clk_sys) / CLK_HZ);
    pwm_init(slice_num, &cfg, false);

	pwm_set_wrap(slice_num, 2);
	pwm_set_chan_level(slice_num, PWM_CHAN_A, 2);
	pwm_set_enabled(slice_num, true);

	bool testGood = true;

    for (int i = 0; i < TESTCOUNT; i++) {
		if (testData[i][0] == 'h' | testData[i][0] == 'w') { // Determine operation
			// Get spi data to send
			uint8_t instruction = (((atoi(&testData[i][2]) & 0x03) << 2) | ((testData[i][0] == 'w') << 1)) & 0x3E;

			// Send it!
			spiWrite(instruction);

			// Data portion
			int16_t data = atoi(&testData[i][4]);
			uint8_t dataLow = data & 0xFF;
			uint8_t dataHigh = (data >> 8) & 0xFF;
			spiWrite(dataHigh);
			spiWrite(dataLow);

		} else { // Actual sample
			// Get sample data and put it in instruction
			// sf_quant always is first number in array, no index needed
			const char *qrPtr = strchr(&testData[i][0], ' ');
			const char *samplePtr = strchr(qrPtr + 1, ' '); // Adding 1 to qrptr to ignore the first character obviously being a space
			uint8_t instruction = (atoi(&testData[i][0]) << 4) | (atoi(qrPtr) << 1) | 0x01;
			int16_t expectedSample = atoi(samplePtr);

			// Send
			gpio_put(PIN_CS, 0);
			spiWrite(instruction);
			gpio_put(PIN_CS, 1);

			// Wait adiquate number of clock cycles
			sleep_us(CLK_PERIOD_US * 50);

			// Request data return
			instruction = 0x80;
			gpio_put(PIN_CS, 0);
			spiWrite(instruction);

			// read the data back
			int16_t returnedSample;
			uint8_t returnedByte;
			returnedByte = spiRead();
			returnedSample = returnedByte << 8;
			returnedByte = spiRead();
			returnedSample = returnedSample | returnedByte;

			// Is it right?
			if (returnedSample == expectedSample) {
				printf("Recived %d just as expected\n", returnedSample);
			} else {
				printf("Wrong sample recived\tReceived: %d\tExpected: %d\tSample #%d\n", returnedSample, expectedSample, i);
				testGood = false;
			}
		}
		
	}

	if (testGood) {
		printf("All clear!!\n");
	} else {
		printf("Test failed :(\n");
	}
	
}
