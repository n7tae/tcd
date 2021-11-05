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

#include "DV3003.h"
#include "configure.h"
#include "UnixPacketSocket.h"

class CController
{
public:
	CController() : keep_running(true) {}
	bool InitDevices();
	bool Start();
	void Stop();
	bool IsRunning() { return keep_running; }

private:
	std::atomic<bool> keep_running;
	std::future<void> future;
	std::vector<std::shared_ptr<CDV3003>> dmr_devices, dstar_devices;
	CUnixPacketClient socket;

	void Processing();

	void CSVtoSet(const std::string &str, std::set<std::string> &set, const std::string &delimiters = ",");
};
