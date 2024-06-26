﻿# tcd

tcd is a hybrid digital voice transcoder for ham radio used by the new URF reflector.

## Introduction

This will build a new kind of hybrid transcoder that uses AMBE DVSI-based hardware for vocoding digital voice streams used in DStar/DMR/YSF *and* David Rowe's open-source Codec2 used in M17 as well as the open-source P25 vocoder, IMBE.

This is the only transcoder that will work with the [URF reflector](https://github.com/nostar/urfd).

This software is loosely based on LX3JL's **ambed**, but is easily different enough to be considered an entirely original work. Here are some major differences with ambed:

- tcd uses both hardware-based and software-based vocoders, providing a bridge between the closed source vocoders used in DStar, DMR NXDN and YSF and open-source vocoders used in M17 (Codec2) and P25 (IMBE).
- *TCP Sockets* are used to communicate between the reflector and this transcoder. This guarantees that packets moving between the reflector and transcoder are never lost and the arrive at their destination in order.
- Each configured module has a dedicated encoding and decoding instance running on a different thread. This prevents overloading when processing multiple voice streams and provides the best possible performance for the reflector's clients.

## Constraints and Requirements

Only systemd-based operating systems are supported. Debian or Ubuntu is recommended. If you want to install this on a non-systemd based OS, you are on your own. Also, by default, *tcd* is built without gdb support.

The P25 IMBE software vocoder library is available [here](https://github.com/nostar/imbe_vocoder). See its README.md file for instructions for compiling and installing this library.

If you are running tcd on an ARM-base processor, you can opt to use a software-based vocoder library available [here](https://github.com/nostar/md380_vocoder) for DMR/YSF vocoding. This library is used for the AMBE+2 (DMR/YSF/NXDN) codec. If you are going to use this library, *tcd* must run on an ARM platform like a RPi. Using this software solution means that you only need one DVSI device to handle D-Star vocoding.

The DVSI devices need an FTDI driver which is available [here](https://ftdichip.com/drivers/d2xx-drivers). It's important to know that this driver will only work if the normal Linux kernel ftdi_sio and usbserial drivers are removed. This is automatically done by the system service file used for starting *tcd*.

## Download the repository

In the parent directory of you *urfd* repository:

```bash
git clone https://github.com/nostar/tcd.git
cd tcd
```

To be perfectly clear, the urfd reflector repository clone and this clone **must be in the same directory**. If your transcoder is a remote installation, you still need to `git clone https://github.com/nostar/urfd.git` even though you won't compile anything in the *urfd* repository. Both *tcd* and *urfd* repositories need to be in the same directory as several of the source files in *tcd* are symbolic links to the adjacent *urfd* reflector source code.

## Compiling and configuring *tcd*

 Copy the three configuration files to the working directory:

```bash
cp config/* .
```

Use your favorite text editor to edit the following files:
- *tcd.mk* defines some compile time options. If you want to use the md380 vocoder, or change the installation directory, specify it here. Once you've set these options, do `make` to compile *tcd*. If you change `BINDIR`, you need to also change the `ExecStart` in your *tcd.service* file.
- *tcd.ini* defines run-time options. It is especially important that the `Modules` line for the tcd.ini file is exactly the same as the same line in the urfd.ini file! The `ServerAddress` is the url of the server. If the transcoder is local, this is usually `127.0.0.1` or `::1`. If the transcoder is remote, this is the IP address of the server. Suggested values for vocoder gains are provided.
- *tcd.service* is the systemd service file. You will need to modify the `ExecStart` line to successfully start *tcd* by specifying the path to your *tcd* executable and your tcd.ini file.

## Installing *tcd* when the transcoder is local

It is easiest to install and uninstall *tcd* using the ./radmin scripts in your urfd repo. If you want to do this manually:

```bash
sudo make install
sudo make uninstall
```

## Installing *tcd when the transcoder is remote

Use:
- `make` to compile *tcd*.
- `sudo make install` to install and run *tcd*.
- `sudo systemctl *something*` is used to manage the running *tcd*, where `*something*` might be `start`, `stop` or other verbs.
- `sudo journalctl -u tcd -f` to monitor the logs.
- `sudo make uninstall` to uninstall *tcd*.

When started, *tcd* will establish a TCP connection for each transcoded reflector module. If the TCP connection is lost, *tcd* will block until the connection is reestablished. A message will be printed every 10 seconds suggesting that the reflector needs to be restarted.
