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
#include <sys/types.h>

#include "Controller.h"

class CUnixPacket
{
public:
	CUnixPacket();
	virtual bool Open(const char *name, CController *host) = 0;
	virtual void Close() = 0;
	bool Write(const void *buffer, const ssize_t size);
	bool Receive(std::vector<uint8_t> &buf, unsigned timeout);
	int GetFD();
protected:
	bool Restart();
	int m_fd;
	CController *m_host;
	char m_name[108];
};

class CUnixPacketServer : public CUnixPacket
{
public:
	CUnixPacketServer();
	~CUnixPacketServer();
	bool Open(const char *name, CController *host);
	void Close();
protected:
	int m_server;
};

class CUnixPacketClient : public CUnixPacket
{
public:
	~CUnixPacketClient();
	bool Open(const char *name, CController *host);
	void Close();
};
