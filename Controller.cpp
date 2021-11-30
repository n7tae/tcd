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
	if (InitDevices() || reader.Open(REF2TC))
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
	// local audio storage
	static int16_t audio[160];

	while (keep_running) {
		STCPacket tcpack;
		//wait up to 40 ms to read something on the unix port
		if (reader.Receive(&tcpack, 40)) {
			//create a std::shared_ptr to a new packet
			auto packet = std::make_shared<CTranscoderPacket>(tcpack);
			unsigned int devnum;
			switch (packet->GetCodecIn()) {
			case ECodecType::dstar:
				devnum = current_dstar_vocoder / 3;
				//send it to the next available dstar vocoder
				dstar_device[devnum]->SendData(current_dstar_vocoder%3, packet->GetDStarData());
				//push the packet onto that vocoder's queue
				dstar_device[devnum]->packet_queue.push(packet);
				//increment the dstar vocoder index
				IncrementDStarVocoder();
				break;
			case ECodecType::dmr:
				devnum = current_dmr_vocoder / 3;
				//send it to the next avaiable dmr vocoder
				dmr_device[devnum]->SendData(current_dmr_vocoder%3, packet->GetDMRData());
				//push the packet onto that vocoder's queue
				dmr_device[devnum]->packet_queue.push(packet);
				//increment the dmr vocoder index
				IncrementDMRVocoder();
				break;
			case ECodecType::c2_1600:
			case ECodecType::c2_3200:
				if (packet->IsSecond()) {
					if (packet->GetCodecIn() == ECodecType::c2_1600) {
						//copy the audio from local storage
						memcpy(packet->GetAudio(), audio, 320);
					} else /* codec_in is ECodecType::c2_3200 */ {
						//decode the second 8 data bytes
						//move the 160 audio samples to the packet
						c2_32.codec2_decode(packet->GetAudio(), packet->GetM17Data()+8);
					}
				} else /* it's a "first packet" */ {
					if (packet->GetCodecIn() == ECodecType::c2_1600) {
						//c2_1600 encodes 40 ms of audio, 320 points, so...
						//we need some temprary audio storage:
						int16_t tmp[320];
						//decode it
						c2_16.codec2_decode(tmp, packet->GetM17Data()); // 8 bytes input produces 320 audio points
						//move the first and second half
						memcpy(packet->GetAudio(), tmp, 160*sizeof(int16_t));
						memcpy(audio, tmp+160, 160*sizeof(int16_t));
					} else /* codec_in is ECodecType::c2_3200 */ {
						c2_32.codec2_decode(packet->GetAudio(), packet->GetM17Data());
					}
				}
				// encode the audio to dstar
				unsigned int devnum = current_dstar_vocoder / 3;
				//send the audio to the current dstar vocoder
				dstar_device[devnum]->SendAudio(current_dstar_vocoder%3, packet->GetAudio());
				//push the packet onto the vocoder's queue
				dstar_device[devnum]->packet_queue.push(packet);
				//increment the dstar vocoder index
				IncrementDStarVocoder();
				// encode the audio to dmr
				devnum = current_dmr_vocoder / 3;
				//send the audio to the corrent dmr vocoder
				dmr_device[devnum]->SendAudio(current_dmr_vocoder%3, packet->GetAudio());
				//push the packet onto the dmr vocoder's queue
				dmr_device[devnum]->packet_queue.push(packet);
				//increment the dmr vocoder index
				IncrementDMRVocoder();
			}
		}
	}
}

void CController::AddFDSet(int &max, int newfd, fd_set *set) const
{
	if (newfd > max)
		max = newfd;
	FD_SET(newfd, set);
}
void CController::ReadAmbeDevices()
{
	while (keep_running)
	{
		int maxfd = -1;
		fd_set FdSet;
		FD_ZERO(&FdSet);
		for (const auto &it : dstar_device)
		{
			AddFDSet(maxfd, it->GetFd(), &FdSet);
		}
		for (const auto &it : dmr_device)
		{
			AddFDSet(maxfd, it->GetFd(), &FdSet);
		}
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 40000;
		auto rval = select(maxfd, &FdSet, nullptr, nullptr, &tv);
		if (rval < 1)
		{
			std::cerr << "select() ERROR reading AMBE devices: " << strerror(errno) << std::endl;
		}
		//wait for up to 40 ms to read anthing from all devices
		if (rval > 0) {
			// from the device file descriptor, we'll know if it's dstar or dmr
			for (unsigned int i=0 ; i<dstar_device.size(); i++)
			{
				if (FD_ISSET(dstar_device[i]->GetFd(), &FdSet))
				{
					ReadDevice(dstar_device[i], EAmbeType::dstar);
				}
			}
			for (unsigned int i=0 ; i<dmr_device.size(); i++)
			{
				if (FD_ISSET(dmr_device[i]->GetFd(), &FdSet))
				{
					ReadDevice(dmr_device[i], EAmbeType::dmr);
				}
			}
		}
	}
}

void CController::ReadDevice(std::shared_ptr<CDV3003> device, EAmbeType type)
{
	//save the dmr/dstar type
	const char *device_type = (EAmbeType::dstar==type) ? "D-Star" : "DMR";

	//read the response from the vocoder
	SDV3003_Packet devpacket;
	if (device->GetResponse(devpacket))
	{
		std::cerr << "ERROR: could not get response from " << device_type << " device at " << device->GetDevicePath() << std::endl;
		return;
	}

	// get the response type
	bool is_audio;
	if (2U == devpacket.header.packet_type)
		is_audio = true;
	else if (1U == devpacket.header.packet_type)
		is_audio = false;
	else
	{
		std::string title("Unexpected ");
		title.append(device_type);
		title.append(" response packet");
		device->dump(title.c_str(), &devpacket, packet_size(devpacket));
		return;
	}

	//get the packet from either the dstar or dmr vocoder's queue
	auto spPacket = device->packet_queue.pop();

	if (is_audio) {
		//move the audio to the CTranscoderPacket
		for (unsigned int i=0; i<160; i++)
			spPacket->GetAudio()[i] = ntohs(devpacket.payload.audio.samples[i]);
		// we need to encode the m17
		//encode the audio to c2_3200 (all ambe input vocodes to ECodecType::c2_3200)
		uint8_t m17data[8];
		if (spPacket->IsSecond()) {
			c2_32.codec2_encode(m17data, spPacket->GetAudio());
			//move the c2_3200 data to the second half of the M17 packet
			spPacket->SetM17Data(m17data, 8, 8);
		} else /* the packet is first */ {
			c2_32.codec2_encode(m17data, spPacket->GetAudio());
			// move it into the packet
			spPacket->SetM17Data(m17data, 0, 8);
			if (spPacket->IsLast()) {
				// we have an odd number of packets, so we have to do finish up the m17 packet
				const uint8_t silence[] = {0x00, 0x01, 0x43, 0x09, 0xe4, 0x9c, 0x08, 0x21 };
				//put codec silence in the second half of the codec
				spPacket->SetM17Data(silence, 8, 8);
			}
		}
		// we've received the audio and we've calculated the m17 data, now we just need to
		// calculate the other ambe data
		if (type == EAmbeType::dmr) {
			//send the audio packet to the next available dmr vocoder
			device->SendAudio(current_dmr_vocoder % 3, spPacket->GetAudio());
			//push the packet onto the dmr vocoder's queue
			device->packet_queue.push(spPacket);
			//increment the dmr vocoder index
			IncrementDMRVocoder();
		} else /* the dmr/dstar type is dstar */ {
			//send the audio packet to the next available dstar vocoder
			device->SendAudio(current_dstar_vocoder % 3, spPacket->GetAudio());
			//push the packet onto the dstar vocoder's queue
			device->packet_queue.push(spPacket);
			//increment the dstar vocoder index
			IncrementDStarVocoder();
		}
	} else /* the response is ambe */ {
		if (type == EAmbeType::dmr) {
			spPacket->SetDMRData(devpacket.payload.ambe.data);
		} else {
			spPacket->SetDStarData(devpacket.payload.ambe.data);
		}
		if (spPacket->AllCodecsAreSet()) {
			// open a socket to the reflector channel
			CUnixDgramWriter socket;
			std::string name(TC2REF);
			name.append(1, spPacket->GetModule());
			socket.SetUp(name.c_str());
			// send the packet over the socket
			socket.Send(spPacket->GetTCPacket());
			// the socket will automatically close after sending
		}
	}
}
