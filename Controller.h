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

#include <vector>
#include <map>
#include <set>
#include <memory>
#include <atomic>
#include <future>
#include <mutex>
#include <sys/select.h>

#include "codec2.h"
#include "DV3003.h"
#include "UnixDgramSocket.h"
#include "configure.h"

enum class EAmbeType { dstar, dmr };

class CController
{
public:
	CController();
	bool Start();
	void Stop();

protected:
	unsigned int dmr_vocoder_count, current_dmr_vocoder, dstar_vocoder_count, current_dstar_vocoder, dmr_depth, dstar_depth;
	std::atomic<bool> keep_running;
	std::future<void> reflectorFuture, readambeFuture, feedambeFuture, c2Future;
	std::vector<std::shared_ptr<CDV3003>> dmr_device, dstar_device;
	std::map<char, int16_t[160]> audio_store;
	CUnixDgramReader reader;
	CUnixDgramWriter writer;
	CCodec2 c2_16{false};
	CCodec2 c2_32{true};
	CPacketQueue codec2_queue, dmr_queue, dstar_queue;
	std::mutex dstar_mux, dmr_mux, c2_mux;

	bool InitDevices();
	void IncrementDMRVocoder(void);
	void IncrementDStarVocoder(void);
	// processing threads
	void ReadReflectorThread();
	void ReadAmbesThread();
	void FeedAmbesThread();
	void ProcessC2Thread();
	void SendToReflector(std::shared_ptr<CTranscoderPacket> packet);
	void Codec2toAudio(std::shared_ptr<CTranscoderPacket> packet);
	void AudiotoCodec2(std::shared_ptr<CTranscoderPacket> packet);
	void ReadDevice(std::shared_ptr<CDV3003> dv3003, EAmbeType type);
	void AddFDSet(int &max, int newfd, fd_set *set) const;
#ifdef DEBUG
	void AppendWave(const std::shared_ptr<CTranscoderPacket> packet) const;
	void Dump(const std::shared_ptr<CTranscoderPacket> packet, const std::string &title) const;
#endif
};
