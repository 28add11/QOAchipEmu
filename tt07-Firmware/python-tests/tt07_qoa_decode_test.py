# SPDX-FileCopyrightText: © 2024 Nicholas Alan West
# SPDX-License-Identifier: MIT

import microcotb as cocotb
from microcotb.clock import Clock
from microcotb.triggers import ClockCycles

cocotb.set_runner_scope(__name__)

from ttboard.demoboard import DemoBoard, RPMode
from ttboard.cocotb.dut import DUT 

def to_signed_16_bit(n):
    """Convert an unsigned 16-bit integer to a signed 16-bit integer."""
    if n >= 0x8000:  # If the number is greater than or equal to 32768
        return n - 0x10000  # Subtract 65536
    else:
        return n


@cocotb.test()
async def test_project(dut):
	dut._log.info("Start")

    
	# Open and read from the debug data file
	fullFile = """h 0 -186
w 0 -844
h 1 5417
w 1 6376
h 2 12372
w 2 -13616
h 3 17578
w 3 15212
7 4 17680
7 3 12363
7 5 4624
7 2 70
7 0 97
7 1 1929
7 3 2140
7 6 2936
7 4 4823
7 0 5971
7 2 6202
7 6 7665
7 0 9290
7 7 7804
7 0 5391
7 0 4491
7 1 4964
7 1 5460
7 2 6071
7 4 7609
7 2 9528
7 7 8433
7 1 5870
7 4 5765
7 0 7847
7 7 7647
7 0 5930
7 0 4756
7 2 5562
7 4 8370
7 0 10686
7 7 9017
7 0 6178
7 2 5746
7 3 6645
7 1 7233
7 5 5595
7 4 4781
7 7 3071
7 2 2333
8 1 2304
8 2 3509
8 0 4758"""

	fileDat = fullFile.split("\n")

	# Set the clock period to 20 ns (50 MHz)
	clock = Clock(dut.clk, 20, units="ns")
	cocotb.start_soon(clock.start())

	# Reset
	dut._log.info("Reset")
	dut.ena.value = 1
	dut.ui_in.value = 0
	dut.uio_in.value = 0
	dut.rst_n.value = 0
	await ClockCycles(dut.clk, 10)
	dut.rst_n.value = 1

	dut._log.info("Test project behavior")

	# start with chipsel high and pulse clock
	dut.uio_in.value = 0b00000001
	await ClockCycles(dut.clk, 3)
	dut.uio_in.value = 0b00001001
	await ClockCycles(dut.clk, 3)
	dut.uio_in.value = 0b00000001
	await ClockCycles(dut.clk, 3)

	# pull everything low
	dut.uio_in.value = 0
	await ClockCycles(dut.clk, 3)

	# Read file, wait for processing, get internal signals
	for line in fileDat:
		print(line)
		# Determine operation
		if line[0] == 'h' or line[0] == 'w': # Fill history or weights
			instruction = (((int(line[2]) & 0x03) << 2) | (int(line[0] == 'w') << 1)) & 0x3E
			data = int(line[4:])

			# Send instruction
			for bit in range(0, 8):
				await ClockCycles(dut.clk, 3)
				baseInst = (((instruction << bit) & 0x80) >> 6)
				dut.uio_in.value = baseInst
				await ClockCycles(dut.clk, 3)
				dut.uio_in.value = 0x08 | baseInst
			
			# Send data
			for bit in range(0, 16):
				await ClockCycles(dut.clk, 3)
				baseInst = (((data << bit) & 0x8000) >> 14)
				dut.uio_in.value = baseInst
				await ClockCycles(dut.clk, 3)
				dut.uio_in.value = 0x08 | baseInst
			
			# Wait 3 clocks and zero values
			await ClockCycles(dut.clk, 3)
			dut.uio_in.value = 0

		else: # Sample
			splitted = line.split()
			print(splitted)
			sfQuant = int(splitted[0])
			qr = int(splitted[1])
			sample = int(splitted[2])

			# Send sample
			instruction = ((sfQuant << 4) | (qr << 1)) | 0x01
			for bit in range(0, 8):
				await ClockCycles(dut.clk, 3)
				baseInst = (((instruction << bit) & 0x80) >> 6)
				dut.uio_in.value = baseInst
				await ClockCycles(dut.clk, 3)
				dut.uio_in.value = 0x08 | baseInst

			await ClockCycles(dut.clk, 3)
			dut.uio_in.value = 0x0
			await ClockCycles(dut.clk, 3)
			dut.uio_in.value = 0x01 # CS high
			await ClockCycles(dut.clk, 32) # Delay for processing
			# Get sample
			instruction = 0x80
			# Send instruction
			for bit in range(0, 8):
				baseInst = (((instruction << bit) & 0x80) >> 6)
				dut.uio_in.value = baseInst
				await ClockCycles(dut.clk, 3)
				dut.uio_in.value = 0x08 | baseInst
				await ClockCycles(dut.clk, 3)
				
			# Recive
			returned = 0
			for bit in range(0, 16):
				dut.uio_in.value = 0x00
				await ClockCycles(dut.clk, 3)
				dut.uio_in.value = 0x08
				uioVal = int(dut.uio_out.value)
				returned = ((returned << 1) | ((uioVal & 0x04) >> 2))
				await ClockCycles(dut.clk, 3)
			
			assert to_signed_16_bit(returned) == sample

def run():
    
    # get the demoboard singleton
    tt = DemoBoard.get()
    # We are testing a project, check it's on
    # this board
    if not tt.shuttle.has('tt_um_28add11_QOAdecode'):
        print("My project's not here!")
        return False
    
    # enable it
    tt.shuttle.tt_um_28add11_QOAdecode.enable()
    
    # we want to be able to control the I/O
    # set the mode
    if tt.mode != RPMode.ASIC_RP_CONTROL:
        print("Setting mode to ASIC_RP_CONTROL")
        tt.mode = RPMode.ASIC_RP_CONTROL
    

    tt.uio_oe_pico.value = 0b00001011
    
    # get a runner
    runner = cocotb.get_runner(__name__)
    
    # here's our DUT... you could subclass this and 
    # do cool things, like rename signals or access 
    # bits and slices
    dut = DUT() # basic TT DUT, with dut._log, dut.ui_in, etc
    
    # say we want to treat a single bit, ui_in[0], as a signal,
    # to use as a named input, so we can do 
    # dut.input_pulse.value = 1, or use it as a clock... easy:
    #dut.add_bit_attribute('input_pulse', tt.ui_in, 0)
    # now dut.input_pulse can be used like any other dut wire
    # there's also an add_slice_attribute to access chunks[4:2]
    # by name. 
    
    # run all the @cocotb.test()
    runner.test(dut)
