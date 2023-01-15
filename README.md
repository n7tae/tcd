# tcd

tcd is a hybrid digital voice transcoder for ham radio used by the new URF reflector.

## Introduction

This will build a new kind of hybrid transcoder that uses AMBE DVSI-based hardware for vocoding digital voice streams used in DStar/DMR/YSF *and* David Rowe's open-source Codec2 used in M17. TCd is optimized for performance by using a highly multi-threaded design that incorporates blocking I/O to make it as efficient as possible.

This is the only transcoder that will work with the [URF reflector](https://github.com/n7tae/urfd).

The imbe_vocoder library is required for P25 and can be found here:

https://github.com/nostar/imbe_vocoder

To use md380_vocoder along with a single DV Dongle on an ARM platform (like RPi) change the line 'swambe2 = false' to 'swambe2 = true'. The md380_vocoder library can be found here:

https://github.com/nostar/md380_vocoder

This software is loosely based on LX3JL's **ambed**, but is easily different enough to be considered an entirely original work. Here are some major differences with ambed:

- tcd uses both hardware-based and software-based vocoders, providing a bridge between the closed source vocoders used in DStar, DMR and YSF and open-source vocoders used in M17.
- **UNIX Sockets** are used to communicate between the reflector and this transcoder. This greatly simplifies the code and *significantly* improves transcoding performance.
- AMBE vocoders are dedicated to an assigned reflector channel. This prevents overloading when processing multiple voice streams and provides the best possible performance for the reflector's clients.

## Constraints and Requirements

This branch uses only one 300x device for the AMBE+(DStar) codec. The md380_vocoder library is used for the AMBE+2 (DMR/YSF/NXDN) codec. This means that this branch of tcd must run on an ARM platform like a RPi.

Currently, this program must be run locally with its paired URF reflector. Remote transcoding is not yet supported.

Only systemd-based operating systems are supported. Debian or Ubuntu is recommended. If you want to install this on a non-systemd based OS, you are on your own. Also, by default, tcd is built without gdb support.

## Download the repository

In the parent directory of you urfd repository:

```bash
git clone https://github.com/n7tae/tcd.git
```

To be perfectly clear, the urfd reflector repository clone and this clone **must be in the same directory**.

## Configuring, compiling, installing and other activities

All other activities will be performed by the ./rconfig and ./radmin scripts in your urfd repo.

## 73

DE N7TAE
