/*
 *
 * A program meant to emulate USB functionality for my QOA decoding ASIC, available at https://github.com/28add11/tt07_qoa_decode
 * This is meant to serve as a way to write programs on the PC side without having actual hardware, which will arrive with the tt07 shuttle.
 * 
*/


/*
 * Code written by Nicholas West, with some parts adapted from TinyUSB example code. The following is the license for that
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

#include "pico/stdlib.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "bsp/board_api.h"
#include "tusb.h"

#include "qoa.h"


static void cdc_task(void);

static struct qoaInstance inst;

/*------------- MAIN -------------*/
int main(void) {
	stdio_init_all();

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

				decodeSamples(&inst, slice, outputBuf);
				tud_cdc_n_write(roundRobinWriteInd, outputBuf, 40);
	
				tud_cdc_n_write_flush(roundRobinWriteInd);

				// XOR makes it flip between 0 and 1!
				roundRobinWriteInd ^= 1;
				break;

			case 0x08: // History value fill
				memcpy(&data, buf, sizeof(uint32_t));
				data >>= 4;
				//Get index
				printf("%i\n", data >> 2);
				index = data & 0x03;
				inst.history[index] = (int16_t)(data >> 2);
				break;

			case 0x07: // Weights value fill
				memcpy(&data, buf, sizeof(uint32_t));
				data >>= 4;
				//Get index
				index = data & 0x03;
				inst.weights[index] = (int16_t)(data >> 2);
				break;

			case 0x04: // sf_quant data
				memcpy(&data, buf, sizeof(uint32_t));
				data >>= 4;
				inst.sf_quant = (int8_t)(data) & 0x0F;
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
