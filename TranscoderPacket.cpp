
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

#include "TranscoderPacket.h"

CTranscoderPacket::CTranscoderPacket() : module(' '), codec_in(ECodecType::none), dstar_set(false), dmr_set(false), m17_set(false)
{
}

char CTranscoderPacket::GetModule() const
{
	return module;
}

void CTranscoderPacket::SetModule(char mod)
{
	module = mod;
}

const uint8_t *CTranscoderPacket::GetDStarData()
{
	return data.dstar;
}

const uint8_t *CTranscoderPacket::GetDMRData()
{
	return data.dmr;
}

const uint8_t *CTranscoderPacket::GetM17Data()
{
	return data.m17;
}

void CTranscoderPacket::SetDStarData(const uint8_t *dstar)
{
	memcpy(data.dstar, dstar, 9);
	dstar_set = true;
}
void CTranscoderPacket::SetDMRData(const uint8_t *dmr )
{
	memcpy(data.dmr, dmr, 9);
	dmr_set = true;
}

void CTranscoderPacket::SetM17Data(const uint8_t *m17, bool is_3200)
{
	memcpy(data.m17, m17, is_3200 ? 16 : 8);
	m17_set = true;
	m17_is_3200 = is_3200;
}

void CTranscoderPacket::SetCodecIn(ECodecType type, uint8_t *data)
{
	switch (type) {
		case ECodecType::dstar:
			SetDStarData(data);
			break;
		case ECodecType::dmr:
			SetDMRData(data);
			break;
		case ECodecType::m17_1600:
			SetM17Data(data, false);
			break;
		case ECodecType::m17_3200:
			SetM17Data(data, true);
			true;
	}
	if (type != ECodecType::none)
		codec_in = type;
}

ECodecType CTranscoderPacket::GetCodecIn() const
{
	return codec_in;
}

bool CTranscoderPacket::DStarIsSet() const
{
	return dstar_set;
}

bool CTranscoderPacket::DMRIsSet() const
{
	return dmr_set;
}

bool CTranscoderPacket::M17IsSet() const
{
	return m17_set;
}

bool CTranscoderPacket::M17Is3200() const
{
	return m17_is_3200;
}

bool CTranscoderPacket::AllAreSet() const
{
	return (dstar_set && dmr_set && m17_set);
}
