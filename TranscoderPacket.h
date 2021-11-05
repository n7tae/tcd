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

#include <cstring>
#include <stdint.h>

using SCodecData = struct codecdata_tag {
	uint8_t dstar[9];
	uint8_t dmr[9];
	uint8_t m17[16];
};

enum class ECodecType { none, dstar, dmr, m17_1600, m17_3200 };

class CTranscoderPacket
{
public:
	// constructor
	CTranscoderPacket();

	// this packet's refector module;
	char GetModule() const;
	void SetModule(char mod);

	// codec
	const uint8_t *GetDStarData();
	const uint8_t *GetDMRData();
	const uint8_t *GetM17Data();
	void SetDStarData(const uint8_t *dstar);
	void SetDMRData(const uint8_t *dmr );
	void SetM17Data(const uint8_t *m17, bool is_3200);

	// first time load
	void SetCodecIn(ECodecType type, uint8_t *data);

	// state of packet
	ECodecType GetCodecIn() const;
	bool DStarIsSet() const;
	bool DMRIsSet() const;
	bool M17IsSet() const;
	bool M17Is3200() const;
	bool AllAreSet() const;

private:
	char module;
	ECodecType codec_in;
	bool m17_is_3200, dstar_set, dmr_set, m17_set;
	SCodecData data;
};
