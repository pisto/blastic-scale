# Blastic scale

This is the software for an IoT scale, based on the [Arduino UNO R4 WiFi](https://docs.arduino.cc/hardware/uno-r4-wifi/), for the [Precious Plastic](https://www.preciousplastic.com/) project. The project is financed by the [Precious Plastic Open Source Fund](https://pposf.preciousplastic.com/).

This software is in beta phase.

# What it does

The scale is intended to be used by plastic recycling workshops around the world, to monitor the weights of recycled plastics. The weight data is uploaded to a data collection page, [https://upload.preciousplastic.com/](https://upload.preciousplastic.com/) (or a custom one).

Please check the wiki pages for additional [documentation](https://github.com/pisto/blastic-scale/wiki).

# Binary releases and upload

You can download the latest release from the [Releases](https://github.com/pisto/blastic-scale/releases) section.

The firmware can be uploaded to the board with [`arduino-cli`](https://arduino.github.io/arduino-cli/1.0/installation/), using the following script:
```bash
# from the project main directory
./scripts/arduino-cli/upload.sh [ blastic-scale.ino.bin file path ]
```

If the file path is omitted, the upload scripts look into the build folder created by the [`compile.sh`](#compilation) script.

Please note that there is a tight integration between the WiFi module of the Arduino UNO R4 WiFi and the firmware payload of Arduino. You should flash the latest available firmware for the WiFi module, following instructions from [here](https://support.arduino.cc/hc/en-us/articles/9670986058780-Update-the-connectivity-module-firmware-on-UNO-R4-WiFi). We suggest using the [espflash method](https://support.arduino.cc/hc/en-us/articles/16379769332892-Restore-the-USB-connectivity-firmware-on-UNO-R4-WiFi-with-espflash), which should work on all OS and system configurations.

# Compilation

The project can be built with the `arduino-cli`. Unfortunately, the Arduino IDE does not allow changing compilation flags, which is necessary for the correct compilation of this project.

First install the [Arduino CLI](https://arduino.github.io/arduino-cli/1.0/installation/), then compile the project:
```bash
# from the project main directory
./scripts/arduino-cli/compile.sh
```

## PlatformIO build and development

We suggest developing on Visual Studio Code with the [PlatformIO plugin](https://platformio.org/install/ide?install=vscode).

