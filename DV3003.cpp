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

void CDV3003::FeedDevice()
{
	const std::string modules(TRANSCODED_MODULES);
	const auto n = modules.size();
	while (keep_running)
	{
		auto packet = input_queue.pop();	// blocks until there is something to pop


		while (keep_running)	// wait until there is room
		{
			if (buffer_depth < 2)
				break;

			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		if (keep_running)
		{
			auto index = modules.find(packet->GetModule());
			// save the packet in the vocoder's queue while the vocoder does its magic
			if (std::string::npos == index)
			{
				std::cerr << "Module '" << packet->GetModule() << "' is not configured on " << description << std::endl;
			}
			else
			{
				waiting_packet[index].push(packet);

				const bool needs_audio = (Encoding::dstar==type) ? packet->DStarIsSet() : packet->DMRIsSet();

				if (needs_audio)
				{
					SendData(index, (Encoding::dstar==type) ? packet->GetDStarData() : packet->GetDMRData());
				}
				else
				{
					SendAudio(index, packet->GetAudioSamples());
				}
				buffer_depth++;
			}
		}
	}
}

void CDV3003::ReadDevice()
{
	while (keep_running)
	{
		// wait for something to read...
		DWORD RxBytes = 0;
		while (0 == RxBytes)
		{
			EVENT_HANDLE eh;
			pthread_mutex_init(&eh.eMutex, NULL);
			pthread_cond_init(&eh.eCondVar, NULL);
			DWORD EventMask = FT_EVENT_RXCHAR;
			auto status = FT_SetEventNotification(ftHandle, EventMask, &eh);
			if (FT_OK != status)
			{
				FTDI_Error("Setting Event Notification", status);
			}

			pthread_mutex_lock(&eh.eMutex);
			pthread_cond_wait(&eh.eCondVar, &eh.eMutex);
			pthread_mutex_unlock(&eh.eMutex);

			DWORD EventDWord, TxBytes, Status;
			status = FT_GetStatus(ftHandle, &RxBytes, &TxBytes, &EventDWord);
			if (FT_OK != status)
			{
				FTDI_Error("Getting Event Status", status);
			}
		}

		SDV_Packet p;
		if (! GetResponse(p))
		{
			unsigned int channel = p.field_id - PKT_CHANNEL0;
			auto packet = waiting_packet[channel].pop();
			if (PKT_CHANNEL == p.header.packet_type)
			{
				if (12!=ntohs(p.header.payload_length) || PKT_CHAND!=p.payload.ambe.chand || 72!=p.payload.ambe.num_bits)
					dump("Improper ambe packet:", &p, packet_size(p));
				buffer_depth--;
				if (Encoding::dstar == type)
					packet->SetDStarData(p.payload.ambe.data);
				else
					packet->SetDMRData(p.payload.ambe.data);

			}
			else if (PKT_SPEECH == p.header.packet_type)
			{
				if (323!=ntohs(p.header.payload_length) || PKT_SPEECHD!=p.payload.audio.speechd || 160!=p.payload.audio.num_samples)
					dump("Improper audio packet:", &p, packet_size(p));
				buffer_depth--;
				packet->SetAudioSamples(p.payload.audio.samples, true);
			}
			else
			{
				dump("ReadDevice() ERROR: Read an unexpected device packet:", &p, packet_size(p));
				continue;
			}
			if (Encoding::dstar == type)	// is this a DMR or a DStar device?
			{
				Controller.dstar_mux.lock();
				Controller.RouteDstPacket(packet);
				Controller.dstar_mux.unlock();
			}
			else
			{
				Controller.dmrst_mux.lock();
				Controller.RouteDmrPacket(packet);
				Controller.dmrst_mux.unlock();
			}
		}
	}
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
