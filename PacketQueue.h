#pragma once

//  Copyright © 2015 Jean-Luc Deltombe (LX3JL). All rights reserved.

// tcd - a hybrid transcoder using DVSI hardware and Codec2 software
// Copyright © 2021 Thomas A. Early N7TAE
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <mutex>
#include <queue>
#include <memory>

#include "TranscoderPacket.h"

// for holding CTranscoder packets while the vocoders are working their magic
class CPacketQueue
{
public:
	// pass thru
	std::shared_ptr<CTranscoderPacket> pop()
	{
		std::shared_ptr<CTranscoderPacket> pack;
		mutex.lock();
		if (! queue.empty()) {
			pack = queue.front();
			queue.pop();
		}
		mutex.unlock();
		return std::move(pack);
	}

	bool empty()
	{
		mutex.lock();
		bool rval = queue.empty();
		mutex.unlock();
		return rval;
	}

	void push(std::shared_ptr<CTranscoderPacket> packet)
	{
		mutex.lock();
		queue.push(packet);
		mutex.unlock();
	}

	std::size_t size()
	{
		mutex.lock();
		auto s = queue.size();
		mutex.unlock();
		return s;
	}

protected:
	std::mutex mutex;
	std::queue<std::shared_ptr<CTranscoderPacket>> queue;
};
