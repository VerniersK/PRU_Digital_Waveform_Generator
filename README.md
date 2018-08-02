## Project information

This project realizes a low-cost high speed digital waveform generator by deploying the BeagleBone Black's PRU subsystem and ARM processor. Kumar Abhishek's [BeagleLogic] (https://github.com/abhishek-kakkar/BeagleLogic/wiki) project provided the framework for this work.

The digital waveform generator supports different output configurations which influence the reliability at different sampling rates:

  * 4 output pins
    - 100 % reliability up to 33.33 MSPS
    - 99.9960 % reliability at 50 MSPS
    
  * 8 output pins
    - 99.9960 % reliability at 25 MSPS
    - 99.7633 % reliability at 50 MSPS
    
  * 13 output pins
    - 99.7633 % reliability at 25 MSPS
    - 97.4034 % reliability at 50 MSPS

An external clock pin is available at pin P9_26. 

Currently, the 4 output configuration is available in this repository.

## Project Installation

To install this project:

  - Clone the repository into the folder: opt/
  - sudo chown -R debian:debian opt/PRU_Digital_Waveform_Generator-master/  (or rename  the map)
  - cd opt/PRU_Digital_Waveform_Generator-master/
  - sudo ./install.sh
    - If install.sh is not executable perform: chmod +x install.sh
  - Reboot Beaglebone
  
After these steps, the firmware is loaded and ready to be used with e.g. Python (see python-example)
