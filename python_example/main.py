from fcntl import ioctl
import subprocess

# IOCTL command
IOCTL_BL_START = ord('k') << (4*2) | 0x29

# Set buffer unit size and write to kernel via subprocess
bufunitsize = 640000
subprocess.call(["echo " +str(bufunitsize)+" > /sys/devices/virtual/misc/beaglelogic/bufunitsize"], shell=True)

# The digital waveforms are stored in a file, this is not mandatory.
# Users can create the waveforms in Python. Correct multiplexing of the parallel waveforms is necessary
# to ensure a correct operation.
fData = open("PRUdata.bin", "rb")
pack = fData.read()                    # Read all waveforms
fData.close()
mem = len(pack)                        # Amount of memory to allocate

# Allocate necessary memory to store the data to transmit (mapping of buffers is also executed)
subprocess.call(["echo " +str(mem)+" > /sys/devices/virtual/misc/beaglelogic/memalloc"], shell=True)

# Open device node
fdp = open("/dev/beaglelogic","wb",0)

# Write digital waveforms to RAM memory
fdp.write(pack)

# Start waveform generation process. After this step, enable the external clock.
ioctl(fdp, IOCTL_BL_START)

print "Enable External Clock now."

# Because of blocking IO, the file descriptor is closed after the waveform generation process has finished.
fdp.close()

print "Disable External Clock now."