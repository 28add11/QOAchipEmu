/*
 *
 * This is the main code for the firmware running on the RP2040 to interface with my QOA audio player ASIC
 * 
*/


/*
 * Code written by Nicholas West, with some parts adapted from TinyUSB example code. The following is the license for tinyUSB
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

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

#include "bsp/board_api.h"
#include "tusb.h"

#include "bitbangSPI.h"

// Clock defines
#define CLK_HZ 1000000
#define CLK_PERIOD_US ((1.0/CLK_HZ)*1000000) // Seconds to microseconds conversion


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

void sendHist(int index, int16_t data) {
	uint8_t instruction = index << 2; // History banksel is zero, thus the whole instruction is just the index << 2

	// Send it!
	spiWrite(instruction);

	// Data portion
	uint8_t dataLow = data & 0xFF;
	uint8_t dataHigh = (data >> 8) & 0xFF;
	spiWrite(dataHigh);
	spiWrite(dataLow);
}

void sendWeights(int index, int16_t data) {
	uint8_t instruction = (index << 2) | 0x2; 

	// Send it!
	spiWrite(instruction);

	// Data portion
	uint8_t dataLow = data & 0xFF;
	uint8_t dataHigh = (data >> 8) & 0xFF;
	spiWrite(dataHigh);
	spiWrite(dataLow);
}

int16_t transmitSample(int sf_quant, int qr) {

	uint8_t instruction = (sf_quant << 4) | (qr << 1) | 0x01;

	// Send
	spiWrite(instruction);

	// Wait adiquate number of clock cycles
	sleep_us(CLK_PERIOD_US * 45);

	// Request data return
	instruction = 0x80;
	spiWrite(instruction);

	// read the data back
	int16_t returnedSample;
	uint8_t returnedByte;
	returnedByte = spiRead();
	returnedSample = returnedByte << 8;
	returnedByte = spiRead();
	returnedSample = returnedSample | returnedByte;

	return returnedSample;
}

static void cdc_task(void);

/*------------- MAIN -------------*/
int main(void) {
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

	tud_init(BOARD_TUD_RHPORT);

	if (board_init_after_tusb) {
		board_init_after_tusb();
	}


	while (1) {
		tud_task(); // tinyusb device task
		cdc_task();
	}
}

// Invoked when device is mounted, but we dont have to do anything
void tud_mount_cb(void) {
	
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
	
}


//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
static void cdc_task(void) {
	static int roundRobinWriteInd = 0;
	static int sf_quant;

	if (tud_cdc_n_available(0)) {
		
		uint8_t buf[8];

		uint32_t count = tud_cdc_n_read(0, buf, 8);
		if(count) {

			
			uint32_t data = 0;
			int index = 0;

			// Decode type of msg at the start
			uint8_t msg = buf[0] & 0x0F;
			switch (msg) {
			case 0x01: // Samples to decode
				gpio_put(PICO_DEFAULT_LED_PIN, 1);
				// Convert the buffer to samples for decoding and shift to get rid of the message
				int16_t outputBuf[20];
				uint64_t slice;
				memcpy(&slice, buf, sizeof(uint64_t));
				slice >>= 4;

				for (int i = 0; i < 20; i++) {
					int qr = (slice >> (i * 3)) & 0x07;
					outputBuf[i] = transmitSample(sf_quant, qr);
				}
				
				tud_cdc_n_write(roundRobinWriteInd, outputBuf, 40);
	
				tud_cdc_n_write_flush(roundRobinWriteInd);

				// XOR makes it flip between 0 and 1
				roundRobinWriteInd ^= 1;
				break;

			case 0x08: // History value fill
				memcpy(&data, buf, sizeof(uint32_t));
				data >>= 4; // Get rid of command
				//Get index
				printf("%i\n", data >> 2);
				index = data & 0x03;
				int16_t hist = data >> 2;
				
				// Send it!
				sendHist(index, hist);
				break;

			case 0x07: // Weights value fill
				memcpy(&data, buf, sizeof(uint32_t));
				data >>= 4; // Get rid of command
				//Get index
				printf("%i\n", data >> 2);
				index = data & 0x03;
				int16_t weight = data >> 2;
				
				// Send it!
				sendWeights(index, weight);
				break;

			case 0x04: // sf_quant data
				memcpy(&data, buf, sizeof(uint32_t));
				data >>= 4;
				sf_quant = (int8_t)(data) & 0x0F;
				break;

			default:
				break;
			}

		}
	}
}

// Invoked when cdc when line state changed e.g connected/disconnected
// Use to reset to DFU when disconnect with 1200 bps
void tud_cdc_line_state_cb(uint8_t instance, bool dtr, bool rts) {
	(void)rts;

	// DTR = false is counted as disconnected
	if (!dtr) {
		// touch1200 only with first CDC instance (Serial)
		if (instance == 0) {
			cdc_line_coding_t coding;
			tud_cdc_get_line_coding(&coding);
			if (coding.bit_rate == 1200) {
				if (board_reset_to_bootloader) {
					board_reset_to_bootloader();
				}
			}
		}
	}
}
