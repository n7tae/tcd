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
#include <thread>

#include "TranscoderPacket.h"
#include "Controller.h"

#define MAX_DEPTH 3

CController::CController() : dmr_vocoder_count(0), current_dmr_vocoder(0), dstar_vocoder_count(0), current_dstar_vocoder(0), dmr_depth(0), dstar_depth(0), keep_running(true)
{
}

bool CController::Start()
{
	if (InitDevices() || reader.Open(REF2TC))
	{
		keep_running = false;
		return true;
	}
	reflectorFuture = std::async(std::launch::async, &CController::ReadReflectorThread, this);
	readambeFuture  = std::async(std::launch::async, &CController::ReadAmbesThread,     this);
	feedambeFuture  = std::async(std::launch::async, &CController::FeedAmbesThread,     this);
	c2Future        = std::async(std::launch::async, &CController::ProcessC2Thread,     this);
	return false;
}

void CController::Stop()
{
	keep_running = false;

	if (reflectorFuture.valid())
		reflectorFuture.get();
	if (readambeFuture.valid())
		readambeFuture.get();
	if (feedambeFuture.valid())
		feedambeFuture.get();
	if (c2Future.valid())
		c2Future.get();

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
			if (a3003->ConfigureVocoder(channel, type))
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

// Encapsulate the incoming STCPacket into a CTranscoderPacket and push it into the appropriate queue
// based on packet's codec_in.
void CController::ReadReflectorThread()
{
	while (keep_running)
	{
		STCPacket tcpack;
		// wait up to 100 ms to read something on the unix port
		if (reader.Receive(&tcpack, 100))
		{
			// create a shared pointer to a new packet
			// there is only one CTranscoderPacket created for each new STCPacket received from the reflector
			auto packet = std::make_shared<CTranscoderPacket>(tcpack);

			switch (packet->GetCodecIn())
			{
			case ECodecType::dstar:
				dstar_queue.push(packet);
				break;
			case ECodecType::dmr:
				dmr_queue.push(packet);
				break;
			case ECodecType::c2_1600:
			case ECodecType::c2_3200:
				c2_mux.lock();
				codec2_queue.push(packet);
				c2_mux.unlock();
				break;
			default:
				Dump(packet, "ERROR: Got a reflector packet with unknown Codec:");
				break;
			}
		}
	}
}

// This is only called when codec_in was dstar or dmr. Obviously, the incoming
// ambe packet was already decoded to audio.
// This might complete the packet. If so, send it back to the reflector
void CController::AudiotoCodec2(std::shared_ptr<CTranscoderPacket> packet)
{
	uint8_t m17data[8];
	c2_32.codec2_encode(m17data, packet->GetAudio());
	if (packet->IsSecond())
	{
		//move the c2_3200 data to the second half of the M17 packet
		packet->SetM17Data(m17data, EAudioSection::secondhalf);
	}
	else /* the packet is first */
	{
		// move it into the packet
		packet->SetM17Data(m17data, EAudioSection::firsthalf);
		if (packet->IsLast())
		{
			// we have an odd number of packets, so we have to finish up the m17 packet
			const uint8_t silence[] = {0x00, 0x01, 0x43, 0x09, 0xe4, 0x9c, 0x08, 0x21 };
			//put codec silence in the second half of the codec
			packet->SetM17Data(silence, EAudioSection::secondhalf);
		}
	}
	// we might be all done...
	if (packet->AllCodecsAreSet())
	{
		SendToReflector(packet);
	}
}

// The original incoming coded was M17, so we will calculate the audio and then
// push the packet onto both the dstar and the dmr queue.
void CController::Codec2toAudio(std::shared_ptr<CTranscoderPacket> packet)
{
	if (packet->IsSecond())
	{
		if (packet->GetCodecIn() == ECodecType::c2_1600)
		{
			// we've already calculated the audio in the previous packet
			// copy the audio from local audio store
			memcpy(packet->GetAudio(), audio_store[packet->GetModule()], 320);
		}
		else /* codec_in is ECodecType::c2_3200 */
		{
			// decode the second 8 data bytes
			// and put it in the packet
			c2_32.codec2_decode(packet->GetAudio(), packet->GetM17Data()+8);
		}
	}
	else /* it's a "first packet" */
	{
		if (packet->GetCodecIn() == ECodecType::c2_1600)
		{
			// c2_1600 encodes 40 ms of audio, 320 points, so...
			// we need some temporary audio storage for decoding c2_1600:
			int16_t tmp[320];
			// decode it into the temporary storage
			c2_16.codec2_decode(tmp, packet->GetM17Data()); // 8 bytes input produces 320 audio points
			// move the first and second half
			// the first half is for the packet
			memcpy(packet->GetAudio(), tmp, 320);
			// and the second half goes into the audio store
			memcpy(audio_store[packet->GetModule()], tmp+160, 320);
		}
		else /* codec_in is ECodecType::c2_3200 */
		{
			c2_32.codec2_decode(packet->GetAudio(), packet->GetM17Data());
		}
	}
	// the only thing left is to encode the two ambe, so push the packet onto both AMBE queues
	dstar_mux.lock();
	dstar_queue.push(packet);
	dstar_mux.unlock();
	dmr_mux.lock();
	dmr_queue.push(packet);
	dmr_mux.unlock();
}

void CController::ProcessC2Thread()
{
	while (keep_running)
	{
		c2_mux.lock();
		auto c2_queue_is_empty = codec2_queue.empty();	// is there a packet avaiable
		c2_mux.unlock();
		if (c2_queue_is_empty)
		{
			// no packet available, sleep for a little while
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		else
		{
			// a packet is available, so get it
			c2_mux.lock();
			auto packet = codec2_queue.pop();
			c2_mux.unlock();
			switch (packet->GetCodecIn())
			{
				case ECodecType::c2_1600:
				case ECodecType::c2_3200:
					// this is an original M17 packet, so decode it to audio
					// Codec2toAudio will send it on for AMBE processing
					Codec2toAudio(packet);
					break;
				case ECodecType::dstar:
				case ECodecType::dmr:
					// codec_in was AMBE, so we need to calculate the the M17 data
					AudiotoCodec2(packet);
					break;
			}
		}
	}
}

void CController::FeedAmbesThread()
{
	while (keep_running)
	{
		bool did_nothing = true;

		// If available, pop a packet from the dstar queue and send it for vocoding
		dstar_mux.lock();
		if ((! dstar_queue.empty()) && (dstar_depth < MAX_DEPTH))
		{
			// encode the audio to dstar
			auto packet = dstar_queue.pop();
			auto devnum = current_dstar_vocoder / 3;
			auto vocnum = current_dstar_vocoder % 3;
			//push the packet onto the vocoder's queue
			dstar_device[devnum]->packet_queue[vocnum].push(packet);
			// send the correct thing to the current dstar vocoder
			if (ECodecType::dstar == packet->GetCodecIn())
				dstar_device[devnum]->SendData(vocnum, packet->GetDStarData());
			else
				dstar_device[devnum]->SendAudio(vocnum, packet->GetAudio());
			dstar_depth++;
			// increment the dstar vocoder index
			IncrementDStarVocoder();
			did_nothing = false;
		}
		dstar_mux.unlock();

		// If available, pop a packet from the dmr queue and send it for vocoding
		dmr_mux.lock();
		if ((! dmr_queue.empty()) && (dmr_depth < MAX_DEPTH))
		{
			// encode the audio to dmr
			auto packet = dmr_queue.pop();
			auto devnum = current_dmr_vocoder / 3;
			auto vocnum = current_dmr_vocoder % 3;
			// push the packet onto the dmr vocoder's queue
			dmr_device[devnum]->packet_queue[vocnum].push(packet);
			// send the correct thing to the corrent dmr vocoder
			if (ECodecType::dmr == packet->GetCodecIn())
				dmr_device[devnum]->SendData(vocnum, packet->GetDMRData());
			else
				dmr_device[devnum]->SendAudio(vocnum, packet->GetAudio());
			dmr_depth++;
			// increment the dmr vocoder index
			IncrementDMRVocoder();
			did_nothing = false;
		}
		dmr_mux.unlock();

		// both the dmr and dstar queue were empty, so sleep for a little while
		if (did_nothing)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

void CController::AddFDSet(int &max, int newfd, fd_set *set) const
{
	if (newfd > max)
		max = newfd;
	FD_SET(newfd, set);
}

// read vocoded (AMBE or audio) data from DVSI hardware
void CController::ReadAmbesThread()
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
// A) These audio packets need to be encoded, by the complimentary AMBE vocoder _and_ M17.
//    Since code_in was AMBE, the audio will also be encoded to c2_3200, and copied to the packet.
// B) If we have read AMBE data from the vocoder, it needs to be put back into the packet.
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
	std::shared_ptr<CTranscoderPacket> packet;
	if (EAmbeType::dstar == type)
	{
		dstar_mux.lock();
		packet = device->packet_queue[devpacket.field_id-PKT_CHANNEL0].pop();
		dstar_mux.unlock();
		dstar_depth--;
	}
	else
	{
		dmr_mux.lock();
		packet = device->packet_queue[devpacket.field_id-PKT_CHANNEL0].pop();
		dmr_mux.unlock();
		dmr_depth--;
	}

	if (is_audio)
	{
		//move the audio to the CTranscoderPacket
		for (unsigned int i=0; i<160; i++)
			packet->GetAudio()[i] = ntohs(devpacket.payload.audio.samples[i]);
		// we need to encode the m17
		// encode the audio to c2_3200 (all ambe input vocodes to ECodecType::c2_3200)
		c2_mux.lock();
		codec2_queue.push(packet);
		c2_mux.unlock();
	}
	else /* the response is ambe data */
	{
		// put the AMBE data in the packet
		if (type == EAmbeType::dmr)
		{
			packet->SetDMRData(devpacket.payload.ambe.data);
		}
		else
		{
			packet->SetDStarData(devpacket.payload.ambe.data);
		}
		// send it off, if it's done
		if (packet->AllCodecsAreSet())
		{
			SendToReflector(packet);
		}
	}
}

void CController::SendToReflector(std::shared_ptr<CTranscoderPacket> packet)
{
	// open a socket to the reflector channel
	CUnixDgramWriter socket;
	std::string name(TC2REF);
	name.append(1, packet->GetModule());
	socket.SetUp(name.c_str());
	// send the packet over the socket
	socket.Send(packet->GetTCPacket());
	// the socket will automatically close after sending
#ifdef DEBUG
	AppendWave(packet);
#endif
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
