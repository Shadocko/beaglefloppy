# beaglefloppy
## Floppy disk preservation using a BeagleBone Black

This is a quick-and-dirty project based on an idea borrowed from [Christian Zietz](https://www.chzsoft.de/site/hardware/preserving-a-floppy-disk-with-a-logic-analyzer/) of using a computer-driven logic analyzer to capture raw flux data from a floppy drive for software preservation.

Instead of using a USB logic analyzer and an FTDI adapter as GPIO, this project is using a BeagleBone, leveraging onboard PRU through [beaglelogic](https://github.com/abhishek-kakkar/BeagleLogic) to acquire flux data, and embedded GPIOs to control the floppy drive.

Raw floppy data is written in [SuperCard Pro image file format](http://www.cbmstuff.com/downloads/scp/scp_image_specs.txt), which can be read by many MFM emulation programs, like e.g. [HxC Floppy Emulator](https://hxc2001.com/download/floppy_drive_emulator/index.html).

I had a stash of Commodore Amiga disks with data I did not want to lose, but no working Amiga to read them, hence this project. For this reason, some values are hard-coded for Amiga DD floppy preservation, but feel free to fork and improve this project!

## Wiring

Connecting BeagleBone outputs to floppy inputs requires a buffer with open-collector outputs; SN7407 is a classic reference.

Connecting floppy drive outputs to BeagleBone logic inputs only requires a pull-up resistor to the 3.3V line, any value around 1000 Ohms should do it.

Everything can be powered with a single 5V DC supply capable of delivering 2-3 Amps. There is normally no need to supply 12V to 3.5" floppy drives.

BeagleBone pins used for data acquisition (all pins on P8 header) must be disconnected at the time of booting the board.

For an IBM/PC floppy drive, wiring will be:
* P9_11 (GPIO_30) ---[buffer]---> floppy pin 32 (SIDE1)
* P9_13 (GPIO_31) ---[buffer]---> floppy pin 18 (DIR)
* P9_15 (GPIO_48) ---[buffer]---> floppy pin 20 (STEP)
* P9_12 (GPIO_60) <---[1K pullup to P9_3]--- floppy pin 26 (TRK00)
* GND ------> floppy pin 10 (MOTEA)
* GND ------> floppy pin 14 (DRVSA)

* P8_39 (GPIO_76) <------ DGND
* P8_40 (GPIO_77) <------ DGND
* P8_41 (GPIO_74) <------ DGND
* P8_42 (GPIO_75) <------ DGND
* P8_43 (GPIO_72) <------ DGND
* P8_44 (GPIO_73) <------ DGND
* P8_45 (GPIO_70) <---[1K pullup to P9_3]--- floppy pin 30 (RDATA)
* P8_46 (GPIO_71) <---[1K pullup to P9_3]--- floppy pin 8 (INDEX)

All odd-numbered floppy drive pins must be connected to ground.
Other pins shall be left floating.

## Building and running

To build the program, just run
```
gcc beaglefloppy.c -o beaglefloppy
```
Beaglelogic will be needed to run the program. The easiest solution is to use a readily available boot image like [this one](https://github.com/abhishek-kakkar/BeagleLogic/wiki/BeagleLogic-%22no-setup-required%22-setup:-Introducing-System-Image!).

To perform a floppy capture, as root, just run
```
./beaglefloppy image.scp
```
Enjoy!