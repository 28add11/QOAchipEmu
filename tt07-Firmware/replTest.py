from machine import Pin, SoftSPI
from ttboard.mode import RPMode
from ttboard.demoboard import DemoBoard

# get a handle to the board
tt = DemoBoard()

# enable a specific project, e.g.
tt.shuttle.tt07_qoa_decode.enable()

print(f'Project {tt.shuttle.enabled.name} running')

# reset
tt.reset_project(True)
tt.clock_project_stop()
tt.clock_project_once()
tt.reset_project(False)

# start automatic project clocking
tt.clock_project_PWM(2e6) # clocking projects @ 2MHz

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
8 0 4758
8 0 5221
8 1 4441
8 1 3073
8 2 3348
8 6 7449
8 2 12237
8 5 12412
8 3 8886
8 3 5225
8 3 3421
8 2 4937
8 2 8307
8 1 9878
8 3 8000
8 1 5012
8 1 3317
8 0 3894
8 6 8197
8 0 12113
8 2 14070
8 7 11374
8 1 7703
8 3 5538
8 3 4714
8 5 2896
8 1 746
8 6 2576
8 6 8465"""

fileDat = fullFile.split("\n")

cs = Pin(21, mode=Pin.OUT)
mosi = Pin(22, mode=Pin.OUT)
miso = Pin(23, mode=Pin.IN)
sck = Pin(24, mode=Pin.OUT)

spi = SoftSPI(baudrate=1000, polarity=0, phase=0, sck=sck, mosi=mosi, miso=miso)

def to_signed_16_bit(n):
    """Convert an unsigned 16-bit integer to a signed 16-bit integer."""
    if n >= 0x8000:  # If the number is greater than or equal to 32768
        return n - 0x10000  # Subtract 65536
    else:
        return n

for line in fileDat:
		# Determine operation
		if line[0] == 'h' or line[0] == 'w': # Fill history or weights
			instruction = (((int(line[2]) & 0x03) << 2) | (int(line[0] == 'w') << 1)) & 0x3E
			data = int(line[4:])

			# Send instruction
			spi.write(instruction.to_bytes(1, "big"))

			# Send data
			databytes = data.to_bytes(2, "big", signed=True)
			spi.write(databytes)

		else: # Sample
			splitted = line.split()
			sfQuant = int(splitted[0])
			qr = int(splitted[1])
			sample = int(splitted[2])

			# Send sample
			instruction = ((sfQuant << 4) | (qr << 1)) | 0x01
			spi.write(instruction.to_bytes(1, "big"))

			# Get sample
			instruction = 0x80
			# Send instruction
			spi.write(instruction.to_bytes(1, "big"))
			
			# Recive
			returned = 0
			returned = int.from_bytes(spi.read(1), byteorder='big')
			returned = int.from_bytes(spi.read(1), byteorder='big') | (returned << 8)
			
			assert to_signed_16_bit(returned) == sample