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

#include <unistd.h>
#include <sys/select.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>

#include "TranscoderPacket.h"
#include "Controller.h"

CController::CController() : dmr_vocoder_count(0), current_dmr_vocoder(0), dstar_vocoder_count(0), current_dstar_vocoder(0), keep_running(true)
{
}

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
	if (reflectorThread.valid())
		reflectorThread.get();
	if (ambeThread.valid())
		ambeThread.get();
	reader.Close();
	for (auto &it : dstar_device)
	{
		it->CloseDevice();
		it.reset();
	}
	dstar_device.clear();
	for (auto &it : dmr_device)
	{
		it->CloseDevice();
		it.reset();
	}
	dmr_device.clear();
}

bool CController::InitDevices()
{
	std::set<std::string> deviceset;
	std::string device;

	for (int i=0; i<32; i++) {
		device.assign("/dev/ttyUSB");
		device += std::to_string(i);

		if (access(device.c_str(), R_OK | W_OK))
			break;
		else
			deviceset.insert(device);
	}

	if (deviceset.empty()) {
		std::cerr << "could not find a device!" << std::endl;
		return true;
	}

	if (2 > deviceset.size())
	{
		std::cerr << "You need at least two DVSI 3003 devices" << std::endl;
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
		for (uint8_t channel=PKT_CHANNEL0; channel<=PKT_CHANNEL2; channel++)
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

	std::cout << "Device count: DStar=" << dstar_device.size() << " DMR=" << dmr_device.size() << std::endl;

	return false;
}

void CController::IncrementDMRVocoder()
{
	current_dmr_vocoder = (current_dmr_vocoder + 1) % dmr_vocoder_count;
}

void CController::IncrementDStarVocoder()
{
	current_dstar_vocoder = (current_dstar_vocoder + 1) % dstar_vocoder_count;
}

// DMR and YSF use the exact same codec, both AMBE.
// Incoming packets from clients have different starting codecs, AMBE or M17.
// The transcoder's task is to fill in the missing data.
// Incoming AMBE codecs go to the DVSI hardware for decoding to audio. The ambeThread will finish up.
// Incoming M17 codecs are decoded in software and the audio data send to the two different AMBE encoders.
// There is no need to transcode between M17 codecs.
void CController::ReadReflector()
{
	while (keep_running)
	{
		STCPacket tcpack;
		// wait up to 40 ms to read something on the unix port
		if (reader.Receive(&tcpack, 40))
		{
			// create a shared pointer to a new packet
			// there is only one CTranscoderPacket created for each new STCPacket received from the reflector
			auto packet = std::make_shared<CTranscoderPacket>(tcpack);
#ifdef DEBUG
			Dump(packet, "Incoming TC Packet:");
#endif
			unsigned int devnum, vocnum;
			switch (packet->GetCodecIn())
			{
			case ECodecType::dstar:
				devnum = current_dstar_vocoder / 3;
				vocnum = current_dstar_vocoder % 3;
				//send it to the next available dstar vocoder
				dstar_device[devnum]->SendData(vocnum, packet->GetDStarData());
				//push the packet onto that vocoder's queue
				dstar_device[devnum]->packet_queue[vocnum].push(packet);
				//increment the dstar vocoder index
				IncrementDStarVocoder();
				break;
			case ECodecType::dmr:
				devnum = current_dmr_vocoder / 3;
				vocnum = current_dmr_vocoder % 3;
				//send it to the next avaiable dmr vocoder
				dmr_device[devnum]->SendData(vocnum, packet->GetDMRData());
				//push the packet onto that vocoder's queue
				dmr_device[devnum]->packet_queue[vocnum].push(packet);
				//increment the dmr vocoder index
				IncrementDMRVocoder();
				break;
			case ECodecType::c2_1600:
			case ECodecType::c2_3200:
				if (packet->IsSecond())
				{
					if (packet->GetCodecIn() == ECodecType::c2_1600)
					{
						//copy the audio from local storage
						memcpy(packet->GetAudio(), audio_store[packet->GetModule()], 320);
					}
					else /* codec_in is ECodecType::c2_3200 */
					{
						//decode the second 8 data bytes
						//move the 160 audio samples to the packet
						c2_32.codec2_decode(packet->GetAudio(), packet->GetM17Data()+8);
					}
				}
				else /* it's a "first packet" */
				{
					if (packet->GetCodecIn() == ECodecType::c2_1600)
					{
						//c2_1600 encodes 40 ms of audio, 320 points, so...
						//we need some temprary audio storage:
						int16_t tmp[320];
						//decode it
						c2_16.codec2_decode(tmp, packet->GetM17Data()); // 8 bytes input produces 320 audio points
						//move the first and second half
						memcpy(packet->GetAudio(), tmp, 320);
						memcpy(audio_store[packet->GetModule()], tmp+160, 320);
					}
					else /* codec_in is ECodecType::c2_3200 */
					{
						c2_32.codec2_decode(packet->GetAudio(), packet->GetM17Data());
					}
				}
				// encode the audio to dstar
				devnum = current_dstar_vocoder / 3;
				vocnum = current_dstar_vocoder % 3;
				// send the audio to the current dstar vocoder
				dstar_device[devnum]->SendAudio(vocnum, packet->GetAudio());
				//push the packet onto the vocoder's queue
				dstar_device[devnum]->packet_queue[vocnum].push(packet);
				// increment the dstar vocoder index
				IncrementDStarVocoder();
				// encode the audio to dmr
				devnum = current_dmr_vocoder / 3;
				vocnum = current_dmr_vocoder % 3;
				// send the audio to the corrent dmr vocoder
				dmr_device[devnum]->SendAudio(vocnum, packet->GetAudio());
				// push the packet onto the dmr vocoder's queue
				dmr_device[devnum]->packet_queue[vocnum].push(packet);
				// increment the dmr vocoder index
				IncrementDMRVocoder();
				break;
			case ECodecType::none:
			default:
				std::cerr << "ERROR: Got a reflector packet with unknown Codec" << std::endl;
#ifdef DEBUG
				Dump(packet, "This is what's in it:");
#endif
				break;
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

// read transcoded (AMBE or audio) data from DVSI hardware
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
		//wait for up to 40 ms to read something from any devices
		auto rval = select(maxfd+1, &FdSet, nullptr, nullptr, &tv);
		if (rval < 0)
		{
			std::cerr << "select() ERROR reading AMBE devices: " << strerror(errno) << std::endl;
		}
		if (rval > 0) {
			// from the device file descriptor, we'll know if it's dstar or dmr
			for (unsigned int i=0 ; i<dstar_device.size(); i++)
			{
				if (FD_ISSET(dstar_device[i]->GetFd(), &FdSet))
				{
					ReadDevice(dstar_device[i], EAmbeType::dstar);
					FD_CLR(dstar_device[i]->GetFd(), &FdSet);
				}
			}
			for (unsigned int i=0 ; i<dmr_device.size(); i++)
			{
				if (FD_ISSET(dmr_device[i]->GetFd(), &FdSet))
				{
					ReadDevice(dmr_device[i], EAmbeType::dmr);
					FD_CLR(dmr_device[i]->GetFd(), &FdSet);
				}
			}
		}
	}
}

// Any audio packet recevied from the DVSI vocoders means that the original codec was AMBE (DStar or DMR).
// These audio packets need to be encoded, by the complimentary AMBE vocoder _and_ M17.
// Since code_in was AMBE, the audio will be encoded to c2_3200, and copied to the packet.
// If we have read AMBE data from the vocoder, it needs to be put back into the packet.
// Finally if the packet is complete, it can be sent back to the reflector.
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
	if (PKT_SPEECH == devpacket.header.packet_type)
		is_audio = true;
	else if (PKT_CHANNEL == devpacket.header.packet_type)
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
	auto spPacket = device->packet_queue[devpacket.field_id-PKT_CHANNEL0].pop();

	if (is_audio)
	{
		//move the audio to the CTranscoderPacket
		for (unsigned int i=0; i<160; i++)
			spPacket->GetAudio()[i] = ntohs(devpacket.payload.audio.samples[i]);
		// we need to encode the m17
		// encode the audio to c2_3200 (all ambe input vocodes to ECodecType::c2_3200)
		uint8_t m17data[8];
		c2_32.codec2_encode(m17data, spPacket->GetAudio());
		if (spPacket->IsSecond())
		{
			//move the c2_3200 data to the second half of the M17 packet
			spPacket->SetM17Data(m17data, EAudioSection::secondhalf);
		}
		else /* the packet is first */
		{
			// move it into the packet
			spPacket->SetM17Data(m17data, EAudioSection::firsthalf);
			if (spPacket->IsLast())
			{
				// we have an odd number of packets, so we have to finish up the m17 packet
				const uint8_t silence[] = {0x00, 0x01, 0x43, 0x09, 0xe4, 0x9c, 0x08, 0x21 };
				//put codec silence in the second half of the codec
				spPacket->SetM17Data(silence, EAudioSection::secondhalf);
			}
		}
		// we've received the audio and we've calculated the m17 data, now we just need to
		// calculate the other ambe data
		if (type == EAmbeType::dmr)
		{
			const unsigned int devnum = current_dstar_vocoder / 3;
			const unsigned int vocnum = current_dstar_vocoder % 3;
			//send the audio packet to the next available dstar vocoder
			dstar_device[devnum]->SendAudio(vocnum, spPacket->GetAudio());
			//push the packet onto the dstar vocoder's queue
			dstar_device[devnum]->packet_queue[vocnum].push(spPacket);
			//increment the dmr vocoder index
			IncrementDStarVocoder();
		}
		else /* the dmr/dstar type is dstar */
		{
			const unsigned int devnum = current_dmr_vocoder / 3;
			const unsigned int vocnum = current_dmr_vocoder % 3;
			//send the audio packet to the next available dmr vocoder
			dmr_device[devnum]->SendAudio(vocnum, spPacket->GetAudio());
			//push the packet onto the dmr vocoder's queue
			dmr_device[devnum]->packet_queue[vocnum].push(spPacket);
			//increment the dmr vocoder index
			IncrementDMRVocoder();
		}
	}
	else /* the response is ambe data */
	{
		// put the AMBE data in the packet
		if (type == EAmbeType::dmr)
		{
			spPacket->SetDMRData(devpacket.payload.ambe.data);
		}
		else
		{
			spPacket->SetDStarData(devpacket.payload.ambe.data);
		}

		// send it off, if it's done
		if (spPacket->AllCodecsAreSet())
		{
			// open a socket to the reflector channel
			CUnixDgramWriter socket;
			std::string name(TC2REF);
			name.append(1, spPacket->GetModule());
			socket.SetUp(name.c_str());
			// send the packet over the socket
			socket.Send(spPacket->GetTCPacket());
			// the socket will automatically close after sending
#ifdef DEBUG
			Dump(spPacket, "Completed Transcoder packet");
			AppendWave(spPacket);
#endif
		}
	}
}

#ifdef DEBUG
void CController::AppendWave(const std::shared_ptr<CTranscoderPacket> packet) const
{
	std::stringstream sstr;
	sstr << std::hex << ntohs(packet->GetStreamId()) << ".raw";
	std::ofstream pcmfile(sstr.str(), std::ofstream::app | std::ofstream::binary);
	if (pcmfile.good())
	{
		pcmfile.write(reinterpret_cast<char *>(packet->GetAudio()), 320);

		pcmfile.close();
	}
	else
		std::cerr << "could not open pcm file " << sstr.str();
}

void CController::Dump(const std::shared_ptr<CTranscoderPacket> p, const std::string &title) const
{
	std::string codec;
	switch (p->GetCodecIn())
	{
	case ECodecType::dstar:
		codec.assign("DStar");
		break;
	case ECodecType::dmr:
		codec.assign("DMR");
		break;
	case ECodecType::c2_1600:
		codec.assign("C2-1600");
		break;
	case ECodecType::c2_3200:
		codec.assign("C2-3200");
		break;
	default:
		codec.assign("NONE");
		break;
	}
	std::cout << title << ": Module='" << p->GetModule() << "' Stream ID=" << std::showbase << std::hex << ntohs(p->GetStreamId()) << std::noshowbase << " Codec in is " << codec;
	if (p->IsSecond())
		std::cout << " IsSecond";
	if (p->IsLast())
		std::cout << " IsLast";
	std::cout << std::endl;

	// if (p->DStarIsSet())
	// {
	// 	std::cout << "DStar data: ";
	// 	for (unsigned int i=0; i<9; i++)
	// 		std::cout << std::setw(2) << std::setfill('0') << unsigned(*(p->GetDStarData()+i));
	// 	std::cout << std::endl;
	// }
	// if (p->DMRIsSet())
	// {
	// 	std::cout << "DMR   Data: ";
	// 	for (unsigned int i=0; i<9; i++)
	// 		std::cout << std::setw(2) << std::setfill('0') << unsigned(*(p->GetDMRData()+i));
	// 	std::cout << std::endl;
	//  }
	//  if (p->M17IsSet())
	//  {
	// 	std::cout << "M17   Data: ";
	// 	for (unsigned int i=0; i<16; i++)
	// 		std::cout << std::setw(2) << std::setfill('0') << unsigned(*(p->GetM17Data()+i));
	// 	std::cout << std::endl;
	// }

	std::cout << std::dec;
}
#endif
