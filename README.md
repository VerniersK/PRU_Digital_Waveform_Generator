## Project information

This project realizes a low-cost high speed digital waveform generator by deploying the BeagleBone Black's PRU subsystem and ARM processor. Kumar Abhishek's [BeagleLogic] (https://github.com/abhishek-kakkar/BeagleLogic/wiki) project provided the framework for this work.

The digital waveform generator supports different output configurations which influence the reliability at different sampling rates (tested by using the internal PRU subsystem's clock):

  * 4 output pins
    - 100 % reliability up to 33.33 MSPS
    - 99.9960 % reliability at 50 MSPS
    
  * 8 output pins
    - 99.9960 % reliability at 25 MSPS
    - 99.7633 % reliability at 50 MSPS
    
  * 13 output pins
    - 99.7633 % reliability at 25 MSPS
    - 97.4034 % reliability at 50 MSPS

Furthermore, an external sampling clock pin is available at pin P9_26. The digital waveform generator has a 300 MB RAM memory available to store the modulation waveforms. 
Currently, the 4 output configuration is available in this repository.

## Project Installation

To install this project:

  - Clone the repository into the following folder: opt/
  - Command: sudo chown -R debian:debian opt/PRU_Digital_Waveform_Generator-master/
  - Command: cd opt/PRU_Digital_Waveform_Generator-master/
  - Install: sudo ./install.sh
    - If install.sh is not an executable, perform: chmod +x install.sh
  - If everything went well, reboot the Beaglebone
  
After these steps, the firmware is loaded and ready to be used with e.g. Python (see python-example).
To verify if the module is loaded, following commands can be entered:

  - lsmod
  - modinfo beaglelogic
  - journalctl |grep beaglelogic

## Python Example

The Python example contains two files:

  - Main.py
  - PRUdata.bin

The first file shows a basic framework to reconstruct 4 arbitrary square waveforms (at pins P8_43 to P8_46) described by the provided binary file. Furthermore, the necessary buffer size and memory allocation is performed as well as the digital waveform generator's enable.
The latter (binary file) is not specific necessary, modulation waveforms can also be generated and multiplexed in Python before writing them to the device via its file descriptor.
