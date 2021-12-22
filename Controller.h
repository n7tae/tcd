#pragma once

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

#include <map>
#include <memory>
#include <atomic>
#include <future>
#include <mutex>

#include "codec2.h"
#include "DV3003.h"
#include "UnixDgramSocket.h"
#include "configure.h"

enum class EAmbeType { dstar, dmr };

class CController
{
public:
	std::mutex dstar_mux, dmr_mux;

	CController();
	bool Start();
	void Stop();
	void RouteDstPacket(std::shared_ptr<CTranscoderPacket> packet);
	void RouteDmrPacket(std::shared_ptr<CTranscoderPacket> packet);
	void Dump(const std::shared_ptr<CTranscoderPacket> packet, const std::string &title) const;

protected:
	std::atomic<bool> keep_running;
	std::future<void> reflectorFuture, c2Future;
	std::map<char, int16_t[160]> audio_store;
	std::map<char, uint8_t[8]> data_store;
	CUnixDgramReader reader;
	CUnixDgramWriter writer;
	CCodec2 c2_16{false};
	CCodec2 c2_32{true};
	CDV3003 dstar_device{Encoding::dstar};
	CDV3003 dmr_device{Encoding::dmr};

	CPacketQueue codec2_queue;
	std::mutex add_dst_mux, add_dmr_mux, c2_mux, send_mux;

	bool InitDevices();
	// processing threads
	void ReadReflectorThread();
	void ProcessC2Thread();
	void Codec2toAudio(std::shared_ptr<CTranscoderPacket> packet);
	void AudiotoCodec2(std::shared_ptr<CTranscoderPacket> packet);
	void SendToReflector(std::shared_ptr<CTranscoderPacket> packet);
#ifdef DEBUG
	void AppendWave(const std::shared_ptr<CTranscoderPacket> packet) const;
	void AppendM17(const std::shared_ptr<CTranscoderPacket> packet) const;
#endif
};
