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
#include <set>
#include <memory>
#include <atomic>
#include <future>
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
	unsigned int dmr_vocoder_count, current_dmr_vocoder, dstar_vocoder_count, current_dstar_vocoder;
	std::atomic<bool> keep_running;
	std::future<void> reflectorThread, ambeThread;
	std::vector<std::shared_ptr<CDV3003>> dmr_device, dstar_device;
	CUnixDgramReader reader;
	CUnixDgramWriter writer;
	CCodec2 c2_16{false};
	CCodec2 c2_32{true};

	bool InitDevices();
	void IncrementDMRVocoder(void);
	void IncrementDStarVocoder(void);
	// processing threads
	void ReadReflector();
	void ReadAmbeDevices();
	void ReadDevice(std::shared_ptr<CDV3003> dv3003, EAmbeType type);
	void AddFDSet(int &max, int newfd, fd_set *set) const;
#ifdef DEBUG
	void Dump(const std::shared_ptr<CTranscoderPacket> packet, const std::string &title) const;
#endif
};
