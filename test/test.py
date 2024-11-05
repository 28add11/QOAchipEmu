import serial
import serial.tools.list_ports

import time

def to_signed_16_bit(n):
    """Convert an unsigned 16-bit integer to a signed 16-bit integer."""
    if n >= 0x8000:  # If the number is greater than or equal to 32768
        return n - 0x10000  # Subtract 65536
    else:
        return n

serSend = serial.Serial()
serSend.baudrate = 115200
serSend.timeout = 10.0
serSend.write_timeout = 10.0

print("Ports availible:")
ports_avail = serial.tools.list_ports.comports()
for i in ports_avail:
	print(i.device)

port = input("Please select a port from that list to send data to: ")
serSend.port = port

serSend.open()

print("Reading test data...")
with open("test/qoaTestF SMALL.txt", "r") as debugDat:
	fileDat = debugDat.readlines()

sampleCount = 0
bufferedSamples = 0
expectedSamples = []

# This is very much not a possible value so we will always start by sending it
prevSfQuant = 256

# We send samples in blocks of 20 so this is for that
slicedSample = 0
slice = 0x00000001

prevtime = time.time()

for line in fileDat:
		# Determine operation
		if line[0] == 'h' or line[0] == 'w': # Fill history or weights
			instruction = (ord(line[0].capitalize()) & 0x0F) | ((int(line[2]) & 0x03) << 4) | ((int(line[4:]) & 0xFFFF )<< 6)

			# Send instruction
			serSend.write(instruction.to_bytes(4, "little"))

		else: # Sample
			splitted = line.split()
			sfQuant = int(splitted[0])
			qr = int(splitted[1])
			sample = int(splitted[2])

			# Because sf_quant is it's own instruction we need this (idea, change that because its shit)
			if sfQuant != prevSfQuant:
				instruction = 0b0100 | ((sfQuant & 0x0F) << 4)
				# Send instruction
				serSend.write(instruction.to_bytes(4, "little"))
				prevSfQuant = sfQuant

			# Store sample and add to next instruction
			expectedSamples.append(sample)
			slice = slice | (qr << (4 + (3 * slicedSample)))
			slicedSample += 1

			# If slice is ready, send sample
			if slicedSample == 20:
				instruction = slice
				serSend.write(instruction.to_bytes(8, "little"))
				slice = 0x0000000000000001
				slicedSample = 0

				# Get samples back
				instruction = 0x00000003
				serSend.write(instruction.to_bytes(1, "little"))
				returnedSampleBuf = serSend.read(40)
				# Process all returned samples
				for n in range(0, 20):
					returnedSample = to_signed_16_bit(returnedSampleBuf[0 + (2 * n)] | (returnedSampleBuf[1 + (2 * n)] << 8))
					expected = expectedSamples.pop(0)
					if returnedSample != expected:
						print("Sample " + str(sampleCount - (20 - n)) + " with value " + str(returnedSample) + " is not equal to " + str(expected))
						assert False

			if sampleCount % 1000 == 0 and sampleCount != 0:
				print("Completed sample " + str(sampleCount) + "\tSamples per second: " + str(1000 / (time.time() - prevtime)))
				prevtime = time.time()
			sampleCount += 1

serSend.close()
