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

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>

#include "TranscoderPacket.h"

// for holding CTranscoder packets while the vocoders are working their magic
// thread safe
class CPacketQueue
{
public:
	// blocks until there's something to pop
	std::shared_ptr<CTranscoderPacket> pop()
	{
		std::unique_lock<std::mutex> lock(m);
		while (q.empty()) {
			c.wait(lock);
		}
		auto pack = q.front();
		q.pop();
		return pack;
	}

	void push(std::shared_ptr<CTranscoderPacket> packet)
	{
		std::lock_guard<std::mutex> lock(m);
		q.push(packet);
		c.notify_one();
	}

private:
	std::queue<std::shared_ptr<CTranscoderPacket>> q;
	mutable std::mutex m;
	std::condition_variable c;
};
