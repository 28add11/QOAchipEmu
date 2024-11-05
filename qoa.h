/*
 * QOA implementation for emulating my ASIC decoder, written by Nicholas West
 *
 * The QOA format specification is available at https://qoaformat.org/
 * Please show them some love!
 *
*/

#pragma once


// Pre calculated table of dequantized values with scale factor, from the QOA reference implementation
static const int qoa_dequant_tab[16][8] = {
	{   1,    -1,    3,    -3,    5,    -5,     7,     -7},
	{   5,    -5,   18,   -18,   32,   -32,    49,    -49},
	{  16,   -16,   53,   -53,   95,   -95,   147,   -147},
	{  34,   -34,  113,  -113,  203,  -203,   315,   -315},
	{  63,   -63,  210,  -210,  378,  -378,   588,   -588},
	{ 104,  -104,  345,  -345,  621,  -621,   966,   -966},
	{ 158,  -158,  528,  -528,  950,  -950,  1477,  -1477},
	{ 228,  -228,  760,  -760, 1368, -1368,  2128,  -2128},
	{ 316,  -316, 1053, -1053, 1895, -1895,  2947,  -2947},
	{ 422,  -422, 1405, -1405, 2529, -2529,  3934,  -3934},
	{ 548,  -548, 1828, -1828, 3290, -3290,  5117,  -5117},
	{ 696,  -696, 2320, -2320, 4176, -4176,  6496,  -6496},
	{ 868,  -868, 2893, -2893, 5207, -5207,  8099,  -8099},
	{1064, -1064, 3548, -3548, 6386, -6386,  9933,  -9933},
	{1286, -1286, 4288, -4288, 7718, -7718, 12005, -12005},
	{1536, -1536, 5120, -5120, 9216, -9216, 14336, -14336},
};

struct qoaInstance {
	int16_t history[4];
	int16_t weights[4];
	int8_t sf_quant;

};

// This function is borrowed verbatim from the QOA reference implementation because they are smarter than me
static inline int qoa_clamp_s16(int v) {
	if ((unsigned int)(v + 32768) > 65535) {
		if (v < -32768) { return -32768; }
		if (v >  32767) { return  32767; }
	}
	return v;
}

void decodeSamples(struct qoaInstance *inst, uint64_t slice, int16_t output[]) {

	// Decode all requested samples from a slice
	for (size_t i = 0; i < 20; i++) {

		// Predict sample from history and weights
		int p = 0;
		for (size_t n = 0; n < 4; n++) {
			p += inst->history[n] * inst->weights[n];
		}
		p >>= 13;

		// Get and decode quantized residual
		uint qr = (slice >> (i * 3)) & 0x07;
		int dequantized = qoa_dequant_tab[inst->sf_quant][qr];

		// Add new value at proper spot in output (reverse order because QOA is big endian)
		output[i] = qoa_clamp_s16(dequantized + p);

		// Update history and weights
		int delta = dequantized >> 4;
    	for (size_t n = 0; n < 4; n++) {
        	inst->weights[n] += (inst->history[n] < 0) ? -delta : delta;
		}
		for (size_t n = 0; n < 3; n++)
        	inst->history[n] = inst->history[n+1];
    	inst->history[3] = output[i];
	}
	
	return;
}
