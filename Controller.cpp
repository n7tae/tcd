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

#include <sys/select.h>
#include <iostream>

#include "TranscoderPacket.h"
#include "Controller.h"

bool CController::Start()
{
	if (InitDevices() || reader.Open("urfd2tcd"))
	{
		keep_running = false;
		return true;
	}
	reflectorThread = std::async(std::launch::async, &CController::ReadReflector,   this);
	ambeThread      = std::async(std::launch::async, &CController::ReadAmbeDevices, this);
	return false;
}

void CController::Stop()
{
	keep_running = false;
	reflectorThread.get();
	ambeThread.get();
	reader.Close();
}

bool CController::InitDevices()
{
	// unpack all the device paths
	std::set<std::string> deviceset;
	CSVtoSet(DEVICES, deviceset);
	if (2 > deviceset.size())
	{
		std::cerr << "You must specify at least two DVSI 3003 devices" << std::endl;
		return true;
	}

	// now initialize each device

	// the first one will be a dstar device
	Encoding type = Encoding::dstar;
	for (const auto devpath : deviceset)
	{
		// instantiate it
		auto a3003 = std::make_shared<CDV3003>(type);

		// open it
		if (a3003->OpenDevice(devpath, 921600))
			return true;

		// initialize it
			a3003->InitDV3003();

		// set each of the 3 vocoders to the current type
		for (uint8_t channel=PKT_CHANNEL0; channel<PKT_CHANNEL2; channel++)
		{
			if (a3003->ConfigureCodec(channel, type))
				return true;
		}

		// add it to the list, according to type
		if (Encoding::dstar == type)
		{
			dstar_device.push_back(a3003);
			dstar_vocoder_count += 3;
		}
		else
		{
			dmr_device.push_back(a3003);
			dmr_vocoder_count += 3;
		}

		// finally, toggle the type for the next device
		type = (type == Encoding::dstar) ? Encoding::dmr : Encoding::dstar;
	}

	dmr_audio_block.resize(dmr_vocoder_count);
	dmr_packet_queue.resize(dmr_vocoder_count);
	dstar_audio_block.resize(dstar_vocoder_count);
	dstar_packet_queue.resize(dstar_vocoder_count);

	return false;
}

void CController::CSVtoSet(const std::string &str, std::set<std::string> &set, const std::string &delimiters)
{
	auto lastPos = str.find_first_not_of(delimiters, 0);	// Skip delimiters at beginning.
	auto pos = str.find_first_of(delimiters, lastPos);	// Find first non-delimiter.

	while (std::string::npos != pos || std::string::npos != lastPos)
	{
		std::string element = str.substr(lastPos, pos-lastPos);
		set.insert(element);

		lastPos = str.find_first_not_of(delimiters, pos);	// Skip delimiters.
		pos = str.find_first_of(delimiters, lastPos);	// Find next non-delimiter.
	}
}

void CController::IncrementDMRVocoder()
{
	current_dmr_vocoder = (current_dmr_vocoder + 1) % dmr_vocoder_count;
}

void CController::IncrementDStarVocoder()
{
	current_dstar_vocoder = (current_dstar_vocoder + 1) % dstar_vocoder_count;
}

void CController::ReadReflector()
{
	while (keep_running) {
		STCPacket tcpack;
		//wait up to 40 ms to read something on the unix port
		if (reader.Receive(&tcpack, 40)) {
			//create a std::shared_ptr to a new packet
			auto packet = std::unique_ptr<CTranscoderPacket>(new CTranscoderPacket(tcpack));
			switch (packet->GetCodecIn()) {
			case ECodecType::dstar:
				//send it to the next available dstar vocoder
				dstar_device[current_dstar_vocoder/3]->SendData(current_dstar_vocoder%3, packet->GetDStarData());
				//push the packet onto that vocoder's queue
				dstar_packet_queue[current_dstar_vocoder].push(packet);
				//increment the dstar vocoder index
				IncrementDStarVocoder();
				break;
			case ECodecType::dmr:
				//send it to the next avaiable dmr vocoder
				dmr_device[current_dmr_vocoder/3]->SendData(current_dmr_vocoder%3, packet->GetDMRData());
				//push the packet onto that vocoder's queue
				dmr_packet_queue[current_dmr_vocoder].push(packet);
				//increment the dmr vocoder index
				IncrementDMRVocoder();
				break;
			case ECodecType::c2_1600:
			case ECodecType::c2_3200:
				if (packet->IsSecond()) {
					if (packet->GetCodecIn() == ECodecType::c2_3200) {
						//decode the second 8 data bytes
						//move the 160 audio samples to the packet
					} else /* the codec is C2_1600 */ {
						//copy the audio from local storage
					}
					// encode the audio to dstar
					//create a 3003 audio packet
					//save the dstar vocoder index in the packet
					//push the packet onto the dstar vocoder's queue
					//send the 3003 audio packet to the dstar vocoder specified in the packet
					//increment the dstar vocoder index
					// encode the audio to dmr
					//save the dmr vocoder index in the packet
					//push the packet onto the dmr vocoder's queue
					//send the same 3003 audio packet to the dmr vocoder specified in the packet
					//increment the dmr vocoder index
				} else /* it's a "first packet" */ {
					//decode the first 8 bytes of data to get the up to 320 audio samples
					//move the first 160 audio samples to the packet
					// encode the audio to dstar
					//create a 3003 audio packet
					//save the dstar vocoder index in the packet
					//push the packet onto the vocoder's queue
					//send it to the dstar vocoder specified in the packet
					//increment the dstar vocoder index
					// encode the audio to dmr
					//save the dmr vocoder index in the packet
					//push the packet onto the vocoder's queue
					//send the same audio packet to the dmr vocoder specified in the packet
					//increment the dmr vocoder index
					// save the second half of the audio if the m17 packet was c2_1600
					if (packet->GetCodecIn() == ECodecType::c2_1600) {
						//put the second 160 audio samples into local storage
					}
				}
			}
		}
	}
}

void CController::ReadAmbeDevices()
{
	while (keep_running)
	{
		//wait for up to 40 ms to read anthing from all devices
		if (something is ready to be read) {
			// from the device file descriptor, we'll know if it's dstar or dmr
			//save the dmr/dstar type
			//read the response from the vocoder
			//get the packet from either the dstar or dmr vocoder's queue
			if (the response is audio) {
				// if the response is audio, this means that packet.codec_in is an ambe codec
				// so we need to encode the m17
				//encode the audio to c2_3200
				if (packet.IsSecond()) {
					//move the c2_3200 data to the second half of the M17 packet
				} else /* the packet is first */ {
					//move the c2_3200 data do the first half of the M17 packet
					if (packet->IsLast()) {
						// we have an odd number of packets, so we have to do finish up the m17 packet
						const uint8_t silence[] = {0x00, 0x01, 0x43, 0x09, 0xe4, 0x9c, 0x08, 0x21 };
						//put codec silence in the second half of the codec
					}
				}
				//create a 3003 audio packet
				//move the response audio to the packet
				if (the dmr/dstar type == dstar) {
					//send the audio packet to the next available dmr vocoder
					//push the packet onto the dmr vocoder's queue
					//increment the dmr vocoder index
				} else /* the dmr/dstar type is dmr */ {
					//send the audio packet to the next available dstar vocoder
					//push the packet onto the dstar vocoder's queue
					//increment the dstar vocoder index
				}
			} else /* the response is ambe */ {
				if (the dmr/dstar type == dstar) {
					//move the ambe to the packet.dstar
				} else {
					//move the ambe to the packet.dmr
				}
				if (packet.AllCodecsAreSet()) {
					//open a socket to the reflector channel
					//send the packet over the socket
					//close the socket
				}
			}
		}
	}
}
