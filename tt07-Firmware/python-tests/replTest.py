import gc
GCThreshold = gc.threshold()
gc.threshold(80000)

import ttboard.log as logging

logging.basicConfig(level=logging.DEBUG, filename='boot.log')

from machine import Pin, SoftSPI
from time import sleep
from ttboard.mode import RPMode
from ttboard.demoboard import DemoBoard

logging.dumpTicksMsDelta('import')

gc.collect()

# get a handle to the board
tt = DemoBoard()

gc.threshold(GCThreshold)

# enable a specific project, e.g.
#tt.shuttle.tt_um_28add11_QOAdecode.enable()
logging.dumpTicksMsDelta('enable')

sleep(0.01)

print(f'Project {tt.shuttle.enabled.name} running')
print("Resetting project...")

# reset
for i in range(10):
	tt.clock_project_once()

tt.reset_project(True)
tt.clock_project_stop()
sleep(0.01)
for i in range(10):
	tt.clock_project_once()
tt.reset_project(False)
logging.dumpTicksMsDelta('reset')

sleep(0.01)
# start automatic project clocking
tt.clock_project_PWM(10000)

print("Driving io...")
cs = Pin(21, mode=Pin.OUT)
cs.value(1)
logging.dumpTicksMsDelta('io')

print("initializing spi...")
spi = SoftSPI(baudrate=100, polarity=0, phase=0, sck=tt.pins.uio3.raw_pin, mosi=tt.pins.uio1.raw_pin, miso=tt.pins.uio2.raw_pin)
print(f"SPI: {spi}\tSCK: {tt.pins.uio3.raw_pin}\tMOSI: {tt.pins.uio1.raw_pin}\tMISO: {tt.pins.uio2.raw_pin}")

# Start testing the project
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

def to_signed_16_bit(n):
    """Convert an unsigned 16-bit integer to a signed 16-bit integer."""
    if n >= 0x8000:  # If the number is greater than or equal to 32768
        return n - 0x10000  # Subtract 65536
    else:
        return n

for line in fileDat:
		print("line: ", line)
		# Determine operation
		if line[0] == 'h' or line[0] == 'w': # Fill history or weights
			instruction = (((int(line[2]) & 0x03) << 2) | (int(line[0] == 'w') << 1)) & 0x3E
			print("instruction: ", instruction)
			data = int(line[4:])

			# Send instruction
			cs.value(0)
			spi.write(instruction.to_bytes(1, "big"))

			# Send data
			databytes = data.to_bytes(2, "big")
			spi.write(databytes)
			cs.value(1)

		else: # Sample
			splitted = line.split()
			sfQuant = int(splitted[0])
			qr = int(splitted[1])
			sample = int(splitted[2])

			# Send sample
			instruction = ((sfQuant << 4) | (qr << 1)) | 0x01
			print("instruction: ", instruction)
			cs.value(0)
			spi.write(instruction.to_bytes(1, "big"))
			cs.value(1)
			sleep(0.1) # Delay for processing

			# Get sample
			instruction = 0x80
			# Send instruction
			cs.value(0)
			spi.write(instruction.to_bytes(1, "big"))
			cs.value(1)
			sleep(0.1)
			cs.value(0)
			# Recive
			returned = 0
			returned = int.from_bytes(spi.read(1))
			returned = int.from_bytes(spi.read(1)) | (returned << 8)
			cs.value(1)
			
			print(f"Returned: {to_signed_16_bit(returned)}\tSample: {sample}")
			assert to_signed_16_bit(returned) == sample