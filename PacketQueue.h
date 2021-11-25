#pragma once

//  Copyright © 2015 Jean-Luc Deltombe (LX3JL). All rights reserved.

// urfd -- The universal reflector
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

#include "TCPacketDef.h"

class CPacketQueue
{
public:
	// lock
	void Lock()   { mutex.lock(); }
	void Unlock() { mutex.unlock(); }

	// pass thru
	std::unique_ptr<STCPacket> pop()              { auto pack = std::move(queue.front()); queue.pop(); return std::move(pack); }
	bool empty() const                            { return queue.empty(); }
	void push(std::unique_ptr<STCPacket> &packet) { queue.push(std::move(packet)); }

protected:
	std::mutex  mutex;
	std::queue<std::unique_ptr<STCPacket>> queue;
};
