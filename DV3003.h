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

#include <string>
#include <sstream>

#define USB3XXX_MAXPACKETSIZE   1024        // must be multiple of 64

#define PKT_HEADER              0x61

#define PKT_CONTROL             0x00
#define PKT_CHANNEL             0x01
#define PKT_SPEECH              0x02

#define PKT_SPEECHD             0x00
#define PKT_CHAND               0x01
#define PKT_RATET               0x09
#define PKT_INIT                0x0b
#define PKT_PRODID              0x30
#define PKT_VERSTRING           0x31
#define PKT_PARITYBYTE          0x2F
#define PKT_RESET               0x33
#define PKT_READY               0x39
#define PKT_CHANNEL0            0x40
#define PKT_CHANNEL1            0x41
#define PKT_CHANNEL2            0x42
#define PKT_PARITYMODE          0x3F
#define PKT_ECMODE              0x05
#define PKT_DCMODE              0x06
#define PKT_COMPAND             0x32
#define PKT_RATEP               0x0A
#define PKT_CHANFMT             0x15
#define PKT_SPCHFMT             0x16
#define PKT_GAIN                0x4B

#define packet_size(a) int(1 + sizeof((a).header) + ntohs((a).header.payload_length))

using SDV3003_Packet = struct __attribute__ ((__packed__)) dv3003_packet {
	uint8_t start_byte;
	struct {
		uint16_t payload_length;
		uint8_t packet_type;
	} header;
	uint8_t field_id;	// for audio and ambe, this is channel# 0x40U, 0x41U or 0x42U
	union {
		struct {
			union {
				char prodid[16];
				uint8_t paritymode[3];
				char version[48];
				uint8_t resp[5];	// for the codec config response, RATEP and INIT
			} data;
		} ctrl;
		struct {
			uint8_t ratep[13];
			uint8_t init[2];
		} codec;
		struct {
			uint8_t speechd;		// 0
			uint8_t num_samples;	// 160
			int16_t samples[160];	// 4 + 1 + 2 + 320 = 327 byte
		} audio;
		struct {
			uint8_t chand;		// 1
			uint8_t num_bits;	// 72 (9 bytes)
			uint8_t data[9];	// 4 + 1 + 2 + 9 = 16 bytes
		} ambe;
	} payload;
};

enum class Encoding { dstar, dmr };

class CDV3003 {
public:
	CDV3003(Encoding t);
	~CDV3003();
	bool OpenDevice(const std::string &device, int baudrate);
	bool InitDV3003();
	bool ConfigureCodec(uint8_t pkt_ch, Encoding type);
	bool SendAudio(const uint8_t channel, const int16_t *audio) const;
	bool GetData(uint8_t *data);
	bool SendData(const uint8_t channel, const uint8_t *data) const;
	bool GetAudio(int16_t *audio);
	void CloseDevice();
	bool IsOpen() const;
	void dump(const char *title, void *data, int length) const;
	std::string GetDevicePath() const;
	std::string GetProductID() const;
	std::string GetVersion() const;
private:
	const Encoding type;
	int fd;
	std::string devicepath, productid, version;
	bool SetBaudRate(int baudrate);
	bool getresponse(SDV3003_Packet &packet);
	bool checkResponse(SDV3003_Packet &responsePacket, uint8_t response) const;
};
