#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hardware/gpio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "tt_pins.h"
#include "tt_setup.h"
#include "testfile.h"

// Clock defines
#define CLK_HZ 1000000
#define CLK_PERIOD_US ((1.0/CLK_HZ)*1000000) // Seconds to microseconds conversion

// SPI Defines
#define PIN_MISO UIO2
#define PIN_CS   UIO0
#define PIN_SCK  UIO3
#define PIN_MOSI UIO1

void clkProjectNTimes(int n) {
	const uint slice_num = pwm_gpio_to_slice_num(CLK);
	pwm_set_enabled(slice_num, false);

	for (int i = 0; i < n; i++) {
		sleep_us(CLK_PERIOD_US);
		gpio_put(CLK, 1);
		sleep_us(CLK_PERIOD_US);
		gpio_put(CLK, 0);
	}
	sleep_us(CLK_PERIOD_US);
	pwm_set_enabled(slice_num, true);
}

int main()
{
    stdio_init_all();

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

    // SPI initialisation, we use 1khz clock for now
    spi_init(spi_default, 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

	spi_set_format(spi_default, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
	spi_set_slave(spi_default, false);

	// Start the project clock
	gpio_set_function(CLK, GPIO_FUNC_PWM);

	uint slice_num = pwm_gpio_to_slice_num(CLK);

	pwm_config cfg = pwm_get_default_config();
	pwm_config_set_clkdiv(&cfg, clock_get_hz(clk_sys) / CLK_HZ);
    pwm_init(slice_num, &cfg, false);

	pwm_set_wrap(slice_num, 2);
	pwm_set_chan_level(slice_num, PWM_CHAN_A, 1);
	pwm_set_enabled(slice_num, true);


    for (int i = 0; i < TESTCOUNT; i++) {
		if (testData[i][0] == 'h' | testData[i][0] == 'w') { // Determine operation
			// Get spi data to send
			uint8_t instruction = (((atoi(&testData[i][2]) & 0x03) << 2) | (testData[i][0] == 'w' << 1)) & 0x3E;

			// Send it!
			gpio_put(PIN_CS, 0);
			spi_write_blocking(spi_default, &instruction, 1);

			// Data portion
			int16_t data = atoi(&testData[i][4]);
			uint8_t dataLow = data & 0xFF;
			uint8_t dataHigh = (data >> 8) & 0xFF;
			spi_write_blocking(spi_default, &(dataLow), 1);
			spi_write_blocking(spi_default, &(dataHigh), 1);

			gpio_put(PIN_CS, 1);

		} else { // Actual sample
			// Get sample data and put it in instruction
			// sf_quant always is first number in array, no index needed
			const char *qrPtr = strchr(&testData[i][0], ' ');
			const char *samplePtr = strchr(qrPtr + 1, ' '); // Adding 1 to qrptr to ignore the first character obviously being a space
			uint8_t instruction = (atoi(&testData[i][0]) << 4) | (atoi(qrPtr) << 1) | 0x01;
			int16_t expectedSample = atoi(samplePtr);

			// Send
			gpio_put(PIN_CS, 0);
			spi_write_blocking(spi_default, &instruction, 1);
			gpio_put(PIN_CS, 1);

			// Wait adiquate number of clock cycles
			sleep_us(CLK_HZ * 50);

			// Request data return
			instruction = 0x80;
			gpio_put(PIN_CS, 0);
			spi_write_blocking(spi_default, &instruction, 1);

			// read the data back
			int16_t returnedSample;
			uint8_t returnedByte;
			spi_read_blocking(spi_default, 0, &returnedByte, 1);
			returnedSample = returnedByte << 8;
			spi_read_blocking(spi_default, 0, &returnedByte, 1);
			returnedSample = returnedSample | returnedByte;

			// Is it right?
			if (returnedSample == expectedSample) {
				printf("Recived %d just as expected\n", returnedSample);
			} else {
				printf("Wrong sample recived\tReceived: %d\tExpected: %d\tSample #%d\n", returnedSample, expectedSample, i);
			}
		}
		
	}
	
}
