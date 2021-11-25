/*
 *   Copyright (C) 2014 by Jonathan Naylor G4KLX and John Hays K7VE
 *   Copyright 2016 by Jeremy McDermond (NH6Z)
 *   Copyright 2021 by Thomas Early N7TAE
 *
 *   Adapted by K7VE from G4KLX dv3000d
 */

// tcd - a hybid transcoder using DVSI hardware and Codec2 software
// Copyright © 2021 Thomas A. Early N7TAE

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <cassert>
#include <iostream>
#include <iomanip>
#include <cerrno>

#include <netinet/in.h>

#include "DV3003.h"

CDV3003::CDV3003(Encoding t) : type(t), fd(-1)
{
}

CDV3003::~CDV3003()
{
	CloseDevice();
}

bool CDV3003::checkResponse(SDV3003_Packet &p, uint8_t response) const
{
	if(p.start_byte != PKT_HEADER || p.header.packet_type != PKT_CONTROL || p.field_id != response)
		return true;

	return false;
}

bool CDV3003::IsOpen() const
{
	return fd >= 0;
}

std::string CDV3003::GetDevicePath() const
{
	return devicepath;
}

std::string CDV3003::GetVersion() const
{
	return version;
}

std::string CDV3003::GetProductID() const
{
	return productid;
}

bool CDV3003::SetBaudRate(int baudrate)
{
	struct termios tty;

	if (tcgetattr(fd, &tty) != 0) {
		std::cerr << devicepath << " tcgetattr: " << strerror(errno) << std::endl;
		close(fd);
		return true;
	}

	//  Input speed = output speed
	cfsetispeed(&tty, B0);

	switch(baudrate) {
		case 230400:
			cfsetospeed(&tty, B230400);
			break;
		case 460800:
			cfsetospeed(&tty, B460800);
			break;
		case 921600:
			cfsetospeed(&tty, B921600);
			break;
		default:
			std::cerr << devicepath << " unsupported baud rate " << baudrate << std::endl;
			close(fd);
			return true;
	}

	tty.c_lflag    &= ~(ECHO | ECHOE | ICANON | IEXTEN | ISIG);
	tty.c_iflag    &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON | IXOFF | IXANY);
	tty.c_cflag    &= ~(CSIZE | CSTOPB | PARENB);
	tty.c_cflag    |= CS8 | CRTSCTS;
	tty.c_oflag    &= ~(OPOST);
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 1;

	if (tcsetattr(fd, TCSANOW, &tty) != 0) {
		std::cerr << devicepath << " tcsetattr: " << strerror(errno) << std::endl;
		close(fd);
		return true;
	}
	return false;
}

bool CDV3003::OpenDevice(const std::string &ttyname, int baudrate)
{
	fd = open(ttyname.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
	if (fd < 0) {
		std::cerr << "error when opening " << ttyname << ": " << strerror(errno) << std::endl;
		return true;
	}
	std::cout << "Opened " << ttyname << std::endl;

	if (SetBaudRate(baudrate))
		return true;

	devicepath.assign(ttyname);

	return false;
}

bool CDV3003::InitDV3003()
{
	char prodId[17];
	char versionstr[49];
	SDV3003_Packet responsePacket, ctrlPacket;

	// ********** hard reset *************
    ctrlPacket.start_byte = PKT_HEADER;
    ctrlPacket.header.payload_length = htons(3);
    ctrlPacket.header.packet_type = PKT_CONTROL;
    ctrlPacket.field_id = PKT_RESET;
	ctrlPacket.payload.ctrl.data.paritymode[0] = PKT_PARITYBYTE;
	ctrlPacket.payload.ctrl.data.paritymode[1] = 0x3U ^ PKT_RESET ^ PKT_PARITYBYTE;
	if (write(fd, &ctrlPacket, packet_size(ctrlPacket)) == -1) {
		std::cerr << "InitDV3003: error writing reset packet: " << strerror(errno) << std::endl;
		return true;
	}

	if (getresponse(responsePacket)) {
		std::cerr << "InitDV3003: error receiving response to reset" << std::endl;
		return true;
	}

	if (checkResponse(responsePacket, PKT_READY)) {
	   std::cerr << "InitDV3003: invalid response to reset" << std::endl;
	   return true;
	}

	// ********** turn off parity *********
	ctrlPacket.header.payload_length = htons(4);
	ctrlPacket.field_id = PKT_PARITYMODE;
	ctrlPacket.payload.ctrl.data.paritymode[0] = 0;
	ctrlPacket.payload.ctrl.data.paritymode[1] = PKT_PARITYBYTE;
	ctrlPacket.payload.ctrl.data.paritymode[2] = 0x4U ^ PKT_PARITYMODE ^ PKT_PARITYBYTE;
	if (write(fd, &ctrlPacket, packet_size(ctrlPacket)) == -1) {
		std::cerr << "InitDV3003: error writing parity control packet: " << strerror(errno) << std::endl;
		return true;
	}

	memset(&responsePacket, 0, sizeof(responsePacket));
	if (getresponse(responsePacket)) {
		std::cerr << "InitDV3003: error receiving response to parity set" << std::endl;
		dump("Parity Ctrl Response Packet", &responsePacket, 4+ntohs(responsePacket.header.payload_length));
		return true;
	}

	if (checkResponse(responsePacket, PKT_PARITYMODE)) {
		std::cerr << "InitDV3003: invalid response to parity control" << std::endl;
		dump("Parity Ctrl Response Packet", &responsePacket, packet_size(responsePacket));
		return true;
	}

	// ********* Product ID and Version *************
	ctrlPacket.header.payload_length = htons(1);
	ctrlPacket.field_id = PKT_PRODID;
	if (write(fd, &ctrlPacket, packet_size(ctrlPacket)) == -1) {
		std::cerr << "InitDV3003: error writing product id packet: " << strerror(errno) << std::endl;
		return true;
	}

	if (getresponse(responsePacket)) {
		std::cerr << "InitDV3003: error receiving response to product id request" << std::endl;
		return true;
	}

	if (checkResponse(responsePacket, PKT_PRODID)) {
	   std::cerr << "InitDV3003: invalid response to product id query" << std::endl;
	   return true;
	}
	strncpy(prodId, responsePacket.payload.ctrl.data.prodid, sizeof(prodId));
	productid.assign(prodId);

	ctrlPacket.field_id = PKT_VERSTRING;
	if (write(fd, &ctrlPacket, packet_size(ctrlPacket)) == -1) {
		std::cerr << "InitDV3003: error writing version packet: " << strerror(errno) << std::endl;
		return true;
	}

	if (getresponse(responsePacket)) {
		std::cerr << "InitDV3003: error receiving response to version request" << std::endl;
		return true;
	}

	if (checkResponse(responsePacket, PKT_VERSTRING)) {
	   std::cerr << "InitDV3003: invalid response to version query" << std::endl;
	   return true;
	}
	strncpy(versionstr, responsePacket.payload.ctrl.data.version, sizeof(version));
	version.assign(versionstr);
	std::cout << "Found " << prodId << " version " << version << std::endl;
	return false;
}

bool CDV3003::ConfigureCodec(uint8_t pkt_ch, Encoding type)
{
	SDV3003_Packet controlPacket, responsePacket;
	const uint8_t  dstar[13] { PKT_RATEP, 0x01U, 0x30U, 0x07U, 0x63U, 0x40U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x48U };
	const uint8_t    dmr[13] { PKT_RATEP, 0x04U, 0x31U, 0x07U, 0x54U, 0x24U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x6FU, 0x48U };
	const uint8_t    init[2] { PKT_INIT, 0x3U };


	controlPacket.start_byte = PKT_HEADER;
	controlPacket.header.payload_length = htons(1 + sizeof(SDV3003_Packet::payload.codec));
	controlPacket.header.packet_type = PKT_CONTROL;
	controlPacket.field_id = pkt_ch;
	memcpy(controlPacket.payload.codec.ratep, (type == Encoding::dstar) ? dstar : dmr, 13);
	memcpy(controlPacket.payload.codec.init, init, 2);

	// write packet
	if (0 > write(fd, &controlPacket, packet_size(controlPacket)) )
	{
		std::cerr << "error writing codec config packet" << strerror(errno) << std::endl;
		return true;
	}

	memset(&responsePacket, 0, sizeof(SDV3003_Packet));
	if (getresponse(responsePacket)) {
		std::cerr << "error reading codec config response packet" << std::endl;
		return true;
	}

	if ((ntohs(responsePacket.header.payload_length) != 6) ||
	    (responsePacket.payload.ctrl.data.resp[1] != PKT_RATEP) ||
		(responsePacket.payload.ctrl.data.resp[2] != 0x00) ||
	    (responsePacket.payload.ctrl.data.resp[3] != PKT_INIT) ||
		(responsePacket.payload.ctrl.data.resp[4] != 0x00) ) {
		std::cerr << "codec config response packet failed" << std::endl;
		return true;
	};
	std::cout << "channel " << (unsigned int)pkt_ch << " is now configured for " << ((Encoding::dstar == type) ? "D-Star" : "DMR") << std::endl;
	return false;
}

void CDV3003::CloseDevice()
{
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}
}

bool CDV3003::getresponse(SDV3003_Packet &packet)
{
	ssize_t bytesRead;

	// get the start byte
	packet.start_byte = 0U;
	const unsigned limit = sizeof(SDV3003_Packet) + 2;
	unsigned got = 0;
	for (unsigned i = 0U; i < limit; ++i) {
		bytesRead = read(fd, &packet.start_byte, 1);
		if (bytesRead == -1) {
			std::cerr << "CDV3003: Error reading from serial port: " << strerror(errno) << std::endl;
			return true;
		}
		if (bytesRead)
			got++;
		if (packet.start_byte == PKT_HEADER)
			break;
	}
	if (packet.start_byte != PKT_HEADER) {
		std::cerr << "CDV3003: Couldn't find start byte in serial data: tried " << limit << " times, got " << got << " bytes" << std::endl;
		return true;
	}

	// get the packet size and type (three bytes)
	ssize_t bytesLeft = sizeof(packet.header);
	ssize_t total = bytesLeft;
	while (bytesLeft > 0) {
		bytesRead = read(fd, ((uint8_t *) &packet.header) + total - bytesLeft, bytesLeft);
		if(bytesRead == -1) {
			std::cout << "AMBEserver: Couldn't read serial data header" << std::endl;
			return true;
		}
		bytesLeft -= bytesRead;
	}

	total = bytesLeft = ntohs(packet.header.payload_length);
    if (bytesLeft > 1 + int(sizeof(packet.payload))) {
        std::cout << "AMBEserver: Serial payload exceeds buffer size: " << int(bytesLeft) << std::endl;
        return true;
    }

    while (bytesLeft > 0) {
        bytesRead = read(fd, ((uint8_t *) &packet.field_id) + total - bytesLeft, bytesLeft);
        if (bytesRead == -1) {
            std::cerr << "AMBEserver: Couldn't read payload: " << strerror(errno) << std::endl;
            return true;
        }

        bytesLeft -= bytesRead;
    }

    return false;
}

bool CDV3003::SendAudio(const uint8_t channel, const int16_t *audio) const
{
	// Create Audio packet based on input int8_ts
	SDV3003_Packet p;
	p.start_byte = PKT_HEADER;
	const uint16_t len = 323;
	p.header.payload_length = htons(len);
	p.header.packet_type = PKT_SPEECH;
	p.field_id = channel;
	p.payload.audio.speechd = 0x0U;
	p.payload.audio.num_samples = 160U;
	for (int i=0; i<160; i++)
		p.payload.audio.samples[i] = htons(audio[i]);

	// send audio packet to DV3000
	int size = packet_size(p);
	if (write(fd, &p, size) != size) {
		std::cerr << "Error sending audio packet" << std::endl;
		return true;
	}
	return false;
}

bool CDV3003::GetData(uint8_t *data)
{
	SDV3003_Packet p;
	// read data packet from DV3000
	p.start_byte = 0U;
	if (getresponse(p))
		return true;
	if (p.start_byte!=PKT_HEADER || htons(p.header.payload_length)!=12 ||
			p.header.packet_type!=PKT_CHANNEL || p.payload.ambe.chand!=1U ||
			p.payload.ambe.num_bits!=72U) {
		std::cerr << "Error receiving audio packet response" << std::endl;
		dump("Received AMBE", &p, packet_size(p));
		return true;
	}

	// copy it to the output
	memcpy(data, p.payload.ambe.data, 9);

	return false;
}

bool CDV3003::SendData(const uint8_t channel, const uint8_t *data) const
{
	// Create data packet
	SDV3003_Packet p;
	p.start_byte = PKT_HEADER;
	p.header.payload_length = htons(12);
	p.header.packet_type = PKT_CHANNEL;
	p.field_id = channel;
	p.payload.ambe.num_bits = 72U;
	p.payload.ambe.chand = 0x1U;
	memcpy(p.payload.ambe.data, data, 9);

	// send data packet to DV3000
	int size = packet_size(p);
	if (write(fd, &p, size) != size) {
		std::cerr << "SendData: error sending data packet" << std::endl;
		dump("Received Data", &p, size);
		return true;
	}
	return false;
}

bool CDV3003::GetAudio(int16_t *audio)
{
	SDV3003_Packet p;
	// read audio packet from DV3000
	p.start_byte = 0U;
	if (getresponse(p))
		return true;
	if (p.start_byte!=PKT_HEADER || htons(p.header.payload_length)!=323 ||
			p.header.packet_type!=PKT_SPEECH || p.payload.audio.speechd!=0U ||
			p.payload.audio.num_samples!=160U) {
		std::cerr << "GetAudio: unexpected audio packet response" << std::endl;
		int size = packet_size(p);
		dump("Received Audio", &p, size);
		return true;
	}

	for (int i=0; i<160; i++)
		audio[i] = ntohs(p.payload.audio.samples[i]);

	return false;
}

void CDV3003::dump(const char *title, void *pointer, int length) const
{
	assert(title != NULL);
	assert(pointer != NULL);

	const uint8_t *data = (const uint8_t *)pointer;

	std::cout << title << std::endl;

	unsigned int offset = 0U;

	while (length > 0) {

		unsigned int bytes = (length > 16) ? 16U : length;

		for (unsigned i = 0U; i < bytes; i++) {
			if (i)
				std::cout << " ";
			std::cout << std::hex << std::setw(2) << std::right << std::setfill('0') << int(data[offset + i]);
		}

		for (unsigned int i = bytes; i < 16U; i++)
			std::cout << "   ";

		std::cout << "   *";

		for (unsigned i = 0U; i < bytes; i++) {
			uint8_t c = data[offset + i];

			if (::isprint(c))
				std::cout << c;
			else
				std::cout << '.';
		}

		std::cout << '*' << std::endl;

		offset += 16U;

		if (length >= 16)
			length -= 16;
		else
			length = 0;
	}
}
