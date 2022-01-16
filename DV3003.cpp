/*
 *   Copyright (C) 2014 by Jonathan Naylor G4KLX and John Hays K7VE
 *   Copyright 2016 by Jeremy McDermond (NH6Z)
 *   Copyright 2021 by Thomas Early N7TAE
 *
 *   Adapted by K7VE from G4KLX dv3000d
 */

// tcd - a hybid transcoder using DVSI hardware and Codec2 software
// Copyright © 2022 Thomas A. Early N7TAE

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

#include <sys/select.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <cerrno>
#include <thread>

#include "DV3003.h"
#include "configure.h"
#include "Controller.h"

extern CController Controller;

CDV3003::CDV3003(Encoding t) : CDVDevice(t) {}

CDV3003::~CDV3003() {}

void CDV3003::PushWaitingPacket(unsigned int channel, std::shared_ptr<CTranscoderPacket> packet)
{
	waiting_packet[channel].push(packet);
}

std::shared_ptr<CTranscoderPacket> CDV3003::PopWaitingPacket(unsigned int channel)
{
	return waiting_packet[channel].pop();
}

bool CDV3003::SendAudio(const uint8_t channel, const int16_t *audio) const
{
	// Create Audio packet based on input int8_ts
	SDV_Packet p;
	p.start_byte = PKT_HEADER;
	p.header.payload_length = htons(1 + sizeof(p.payload.audio));
	p.header.packet_type = PKT_SPEECH;
	p.field_id = channel + PKT_CHANNEL0;
	p.payload.audio.speechd = PKT_SPEECHD;
	p.payload.audio.num_samples = 160U;
	for (int i=0; i<160; i++)
		p.payload.audio.samples[i] = htons(audio[i]);

	// send audio packet to DV3000
	const DWORD size = packet_size(p);
	DWORD written;
	auto status = FT_Write(ftHandle, &p, size, &written);
	if (FT_OK != status)
	{
		FTDI_Error("Error writing audio packet", status);
		return true;
	}
	else if (size != written)
	{
		std::cerr << "Incomplete Speech Packet write on " << description << std::endl;
		return true;
	}

	return false;
}

bool CDV3003::SendData(const uint8_t channel, const uint8_t *data) const
{
	// Create data packet
	SDV_Packet p;
	p.start_byte = PKT_HEADER;
	p.header.payload_length = htons(1 + sizeof(p.payload.ambe));
	p.header.packet_type = PKT_CHANNEL;
	p.field_id = channel + PKT_CHANNEL0;
	p.payload.ambe.chand = PKT_CHAND;
	p.payload.ambe.num_bits = 72U;
	memcpy(p.payload.ambe.data, data, 9);

	// send data packet to DV3000
	const DWORD size = packet_size(p);
	DWORD written;
	auto status = FT_Write(ftHandle, &p, size, &written);
	if (FT_OK != status)
	{
		FTDI_Error("Error writing AMBE Packet", status);
		return true;
	}
	else if (size != written)
	{
		std::cerr << "Incomplete AMBE Packet write on " << description << std::endl;
		return true;
	}

	return false;
}
