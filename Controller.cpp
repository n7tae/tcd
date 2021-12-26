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

CController::CController() : keep_running(true)
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
	c2Future        = std::async(std::launch::async, &CController::ProcessC2Thread,     this);
	return false;
}

void CController::Stop()
{
	keep_running = false;

	if (reflectorFuture.valid())
		reflectorFuture.get();
	if (c2Future.valid())
		c2Future.get();

	reader.Close();
	dstar_device.CloseDevice();
	dmr_device.CloseDevice();
}

bool CController::InitDevices()
{
	std::vector<std::string> deviceset;
	std::string device;

	for (int i=0; i<32; i++) {
		device.assign("/dev/ttyUSB");
		device += std::to_string(i);

		if (access(device.c_str(), R_OK | W_OK))
			break;
		else
			deviceset.push_back(device);
	}

	if (deviceset.empty()) {
		std::cerr << "could not find a device!" << std::endl;
		return true;
	}

	if (2 != deviceset.size())
	{
		std::cerr << "You need exactly two DVSI 3003 devices" << std::endl;
		return true;
	}

	//initialize each device
	if (dstar_device.OpenDevice(deviceset[0], 921600) || dmr_device.OpenDevice(deviceset[1], 921600))
		return true;

	// and start them up!
	dstar_device.Start();
	dmr_device.Start();

	return false;
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
				add_dst_mux.lock();
				dstar_device.AddPacket(packet);
				add_dst_mux.unlock();
#ifdef DEBUG
				if (0 == packet->GetSequence())
					Dump(packet, "DStar from reflect to decode:");
#endif
				break;
			case ECodecType::dmr:
				add_dmr_mux.lock();
				dmr_device.AddPacket(packet);
				add_dmr_mux.unlock();
#ifdef DEBUG
				if (0 == packet->GetSequence())
					Dump(packet, "DMR from reflect to decode:");
#endif
				break;
			case ECodecType::c2_1600:
			case ECodecType::c2_3200:
				c2_mux.lock();
				codec2_queue.push(packet);
				c2_mux.unlock();
#ifdef DEBUG
				if (0 == packet->GetSequence())
					Dump(packet, "M17 from reflect to decode:");
#endif
				break;
			default:
				Dump(packet, "ERROR: Received a reflector packet with unknown Codec:");
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
	// the second half is silent in case this is frame is last.
	uint8_t m17data[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x01, 0x43, 0x09, 0xe4, 0x9c, 0x08, 0x21 };
	if (packet->IsSecond())
	{
		// get the first half from the store
		memcpy(m17data, data_store[packet->GetModule()], 8);
		// and then calculate the second half
		c2_32.codec2_encode(m17data+8, packet->GetAudioSamples());
		packet->SetM17Data(m17data);
	}
	else /* the packet is first */
	{
		// calculate the first half...
		c2_32.codec2_encode(m17data, packet->GetAudioSamples());
		// and then copy the calculated data to the data_store
		memcpy(data_store[packet->GetModule()], m17data, 8);
		// set the m17_is_set flag if this is the last packet
		packet->SetM17Data(m17data);
	}
	// we might be all done...
	if (packet->AllCodecsAreSet())
	{
		send_mux.lock();
		SendToReflector(packet);
		send_mux.unlock();
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
			packet->SetAudioSamples(audio_store[packet->GetModule()], false);
		}
		else /* codec_in is ECodecType::c2_3200 */
		{
			int16_t tmp[160];
			// decode the second 8 data bytes
			// and put it in the packet
			c2_32.codec2_decode(tmp, packet->GetM17Data()+8);
			packet->SetAudioSamples(tmp, false);
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
			packet->SetAudioSamples(tmp, false);
			// and the second half goes into the audio store
			memcpy(audio_store[packet->GetModule()], &(tmp[160]), 320);
		}
		else /* codec_in is ECodecType::c2_3200 */
		{
			int16_t tmp[160];
			c2_32.codec2_decode(tmp, packet->GetM17Data());
			packet->SetAudioSamples(tmp, false);
		}
	}
	// the only thing left is to encode the two ambe, so push the packet onto both AMBE queues
	add_dst_mux.lock();
	dstar_device.AddPacket(packet);
	add_dst_mux.unlock();
	add_dmr_mux.lock();
	dmr_device.AddPacket(packet);
	add_dmr_mux.unlock();
}

void CController::ProcessC2Thread()
{
	while (keep_running)
	{
		c2_mux.lock();
		auto packet = codec2_queue.pop();
		c2_mux.unlock();
		if (packet)
		{
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
		else
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
	if (0 == packet->GetSequence())
		Dump(packet, "Complete:");
#endif
}

void CController::RouteDstPacket(std::shared_ptr<CTranscoderPacket> packet)
{
	if (ECodecType::dstar == packet->GetCodecIn())
	{
		// codec_in is dstar, the audio has just completed, so now calc the M17 and DMR
		c2_mux.lock();
		codec2_queue.push(packet);
		c2_mux.unlock();
		add_dmr_mux.lock();
		dmr_device.AddPacket(packet);
		add_dmr_mux.unlock();
	}
	else if (packet->AllCodecsAreSet())
	{
		send_mux.lock();
		SendToReflector(packet);
		send_mux.unlock();
	}
}

void CController::RouteDmrPacket(std::shared_ptr<CTranscoderPacket> packet)
{
	if (ECodecType::dmr == packet->GetCodecIn())
	{
		c2_mux.lock();
		codec2_queue.push(packet);
		c2_mux.unlock();
		add_dst_mux.lock();
		dstar_device.AddPacket(packet);
		add_dst_mux.unlock();
	}
	else if (packet->AllCodecsAreSet())
	{
		send_mux.lock();
		SendToReflector(packet);
		send_mux.unlock();
#ifdef DEBUG
		AppendWave(packet);
#endif
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
		pcmfile.write(reinterpret_cast<const char *>(packet->GetAudioSamples()), 320);

		pcmfile.close();
	}
	else
		std::cerr << "could not open pcm file " << sstr.str();
}

void CController::AppendM17(const std::shared_ptr<CTranscoderPacket> packet) const
{
	std::stringstream sstr;
	sstr << std::hex << ntohs(packet->GetStreamId()) << ".m17";
	std::ofstream m17file(sstr.str(), std::ofstream::app | std::ofstream::binary);
	if (m17file.good())
	{
		m17file.write(reinterpret_cast<const char *>(packet->GetM17Data()), 16);

		m17file.close();
	}
	else
		std::cerr << "could not open M17 data file " << sstr.str();
}

void CController::Dump(const std::shared_ptr<CTranscoderPacket> p, const std::string &title) const
{
	std::stringstream line;
	line << title << " Mod='" << p->GetModule() << "' SID=" << std::showbase << std::hex << ntohs(p->GetStreamId()) << std::noshowbase << " ET:" << std::setprecision(3) << p->GetTimeMS();

	ECodecType in = p->GetCodecIn();
	if (p->DStarIsSet())
		line << " DStar";
	if (ECodecType::dstar == in)
		line << '*';
	if (p->DMRIsSet())
		line << " DMR";
	if (ECodecType::dmr == in)
		line << '*';
	if (p->M17IsSet())
		line << " M17";
	if (ECodecType::c2_1600 == in)
		line << "**";
	else if (ECodecType::c2_3200 == in)
		line << '*';
	if (p->IsSecond())
		line << " IsSecond";
	if (p->IsLast())
		line << " IsLast";

	std::cout << line.str() << std::dec << std::endl;
}
#endif
