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

static int16_t outputBuf[2048];

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

void sendSamples(int count) {
	static size_t bufReadPos = 0;

	int byteCount = count * 2;
	// Check if request is larger than buffer
	if (count > 2048) {
		return;
	}
	
	// Read from the buffer and send, with logic for "circular" buffering
	if (bufReadPos + count > 2047) {
		// No loops needed because requesting more than 2048 samples (i.e. looping twice or more) will return garbage data
		int difference = 2047 - bufReadPos;
		tud_cdc_write(&outputBuf[bufReadPos], difference * 2);
		bufReadPos = 0;
		tud_cdc_write(&outputBuf[bufReadPos], (count - difference) * 2);
	} else {
		tud_cdc_write(&outputBuf[bufReadPos], byteCount);
	}
	tud_cdc_write_flush();
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
	static int bufWritePos = 0;

	if (tud_cdc_available()) {
		uint8_t buf[8];

		uint32_t count = tud_cdc_read(buf, 8);
		uint32_t data;
		int index;

		// Decode type of msg at the start
		uint8_t msg = buf[0] & 0x0F;
		switch (msg) {
		case 0x01: // Samples to decode
			// Convert the buffer to samples for decoding and shift to get rid of the message
			uint64_t slice = *(uint64_t*)(buf) >> 4;

			// Circular buffer logic
			if(bufWritePos + 20 > 2047) {
				int difference = 2047 - bufWritePos;
				decodeSamples(inst, slice, difference, &outputBuf[bufWritePos]);
				bufWritePos = 0;
				decodeSamples(inst, slice, 20 - difference, &outputBuf[bufWritePos]);
				bufWritePos += 20 - difference;
			} else {
				decodeSamples(inst, slice, 20, &outputBuf[bufWritePos]);
				bufWritePos += 20;
			}

			break;

		case 0x08: // History value fill
			data = (uint32_t)(buf) >> 4;
			//Get index
			index = data & 0x03;
			inst.history[index] = (int16_t)(data >> 2);
			break;
		
		case 0x07: // Weights value fill
			data = (uint32_t)(buf) >> 4;
			//Get index
			index = data & 0x03;
			inst.weights[index] = (int16_t)(data >> 2);
			break;

		case 0x04: // sf_quant data
			data = (uint32_t)(buf) >> 4;
			inst.sf_quant = (int8_t)(data);
			break;

		case 0x03: // send back samples
			data = (uint32_t)(buf) >> 4;
			int count = (int)(data);

			break;

		default:
			break;
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
