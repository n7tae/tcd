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

#include <iostream>

#include "Controller.h"

bool CController::Start()
{
	if (socket.Open("urfd2tcd", this))
	{
		keep_running = false;
		return true;
	}
	future = std::async(std::launch::async, &CController::Processing, this);
	return false;
}

void CController::Stop()
{
	keep_running = false;
	future.get();
	socket.Close();
}

void CController::Processing()
{
	while (keep_running)
	{
		// anything to read?
	}
}

bool CController::InitDevices()
{
	// unpack all the device paths
	std::set<std::string> deviceset;
	CSVtoSet(DEVICES, deviceset);
	if (2 > deviceset.size())
	{
		std::cerr << "You must specify at least two DVSI 3003 devices" << std::endl;
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
		for (uint8_t channel=PKT_CHANNEL0; channel<PKT_CHANNEL2; channel++)
		{
			if (a3003->ConfigureCodec(channel, type))
				return true;
		}

		// add it to the list, according to type
		if (Encoding::dstar == type)
			dstar_devices.push_back(a3003);
		else
			dmr_devices.push_back(a3003);

		// finally, toggle the type for the next device
		type = (type == Encoding::dstar) ? Encoding::dmr : Encoding::dstar;
	}
	return false;
}

void CController::CSVtoSet(const std::string &str, std::set<std::string> &set, const std::string &delimiters)
{
	auto lastPos = str.find_first_not_of(delimiters, 0);	// Skip delimiters at beginning.
	auto pos = str.find_first_of(delimiters, lastPos);	// Find first non-delimiter.

	while (std::string::npos != pos || std::string::npos != lastPos)
	{
		std::string element = str.substr(lastPos, pos-lastPos);
		set.insert(element);

		lastPos = str.find_first_not_of(delimiters, pos);	// Skip delimiters.
		pos = str.find_first_of(delimiters, lastPos);	// Find next non-delimiter.
	}
}
