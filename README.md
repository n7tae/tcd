# tcd

tcd is a hybrid digital voice transcoder for ham radio used by the new URF reflector.

## Introduction

This will build a new kind of hybrid transcoder that uses AMBE DVSI-based hardware for vocoding digital voice streams used in DStar/DMR/YSF *and* David Rowe's open-source Codec2 used in M17. TCd is optimized for performance by using a highly multi-threaded design that incorporates blocking I/O to make it as efficient as possible.

This is the only transcoder that will work with the [URF reflector](https://github.com/n7tae/urfd).

This software is loosely based on LX3JL's **ambed**, but is easily different enough to be considered an entirely original work. Here are some major differences with ambed:

- tcd uses both hardware-based and software-based vocoders, providing a bridge between the closed source vocoders used in DStar, DMR and YSF and open-source vocoders used in M17.
- **UNIX Sockets** are used to communicate between the reflector and this transcoder. This greatly simplifies the code and *significantly* improves transcoding performance.
- AMBE vocoders are dedicated to an assigned reflector channel. This prevents overloading when processing multiple voice streams and provides the best possible performance for the reflector's clients.

## Constraints and Requirements

Currently, only two AMBE devices are supported. You cannot use a 3003 and a 3000 device together. If you use a pair of 3000 devices, only 460800-baud deivces are supported. When using a pair of 3000 devices, you can only transcode a single channel. If you use a pair of 3003 devices, you can specify up to three transcoded channels. AMBE devices based on LX3JL's [USB-3006 open source design](https://github.com/LX3JL/usb-3006) (sold by Northwest Digital and others) contain a pair of 3003 devices and are ideally suited for *tcd*.

To be clear, a pair of 3000-based devices will support only a single transcoded reflector channel, while a pair of 3003-based devices will support up to three transcoded reflector channels.

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
