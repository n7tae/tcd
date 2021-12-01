# tcd

tcd is a hybrid digital voice transcoder for ham radio used by the new URF reflector.

## Introduction

This will build a new kind of hybrid transcoder that uses AMBE DVSI-based hardware for vocoding digital voice streams used in DStar/DMR/YSF *and* David Rowe's open-source Codec2 used in M17. At a minimum, you need a USB-based DVSI device (or multiple devices) based on the DVSI-3003 vocoder. For proper interfacing, each 3003 chip (internally containing three AMBE vocoders) will be detected by your system and assigned a unique /dev/ttyUSBX device. As each ttyUSBX device is assigned to one of the two AMBE codecs, you need at least two 3003 devices.

This software is loosely based on LX3JL's **ambed**, but is easily different enough to be considered an entirely original work. Here are some major differences:

- tcd mixes both hardware-based and software-based vocoders, providing a bridge between the closed source vocoders used in DStar, DMR and YSF and open-source vocoders used in M17.
- tcd uses the standard /dev/ttyUSBX for interfacing with the DVSI hardware. At lease two 3003-based devices are required.
- **UNIX Sockets** are used to communicate between the reflector and this transcoder. This greatly simplifies the code and *significantly* improves transcoding performance.
- tcd only supports DVSI-3003-based devices that use the ttyUSBX interface.

Only systemd-based operating systems are supported. Debian or Ubuntu is recommended. If you want to install this on a non-systemd based OS, you are on your own. Also, by default, tcd is built without gdb support.

## Download the repository

In the same directory where you urfd repository is located:

```bash
git clone https://github.com/n7tae/tcd.git
```

To be perfectly clear, the urfd reflector repository clone and this clone **must be in the same directory**.

## Configuring, compiling, installing and other activities

All other activities will be performed by the ./rconfig and ./radmin scripts in your urfd repo.

## 73

DE N7TAE
