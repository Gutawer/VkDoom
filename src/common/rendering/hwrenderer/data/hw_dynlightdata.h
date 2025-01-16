// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2005-2016 Christoph Oelckers
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//

#ifndef __GLC_DYNLIGHT_H
#define __GLC_DYNLIGHT_H

#include "tarray.h"

enum FDynLightInfoFlags
{
	LIGHTINFO_ATTENUATED = 1,
	LIGHTINFO_SHADOWMAPPED = 2,
	LIGHTINFO_SPOT = 4,
};

struct FDynLightInfo
{
	float x;
	float y;
	float z;
	float padding0; // 4
	float r;
	float g;
	float b;
	float padding1; // 8
	float spotDirX;
	float spotDirY;
	float spotDirZ;
	float padding2; // 12
	float radius;
	float linearity;
	float softShadowRadius;
	float strength; // 16
	float spotInnerAngle;
	float spotOuterAngle;
	int shadowIndex;
	int flags; // 20
};

enum FDynLightDataArrays
{
	LIGHTARRAY_NORMAL,
	LIGHTARRAY_SUBTRACTIVE,
	LIGHTARRAY_ADDITIVE,
};

#define MAX_LIGHT_DATA 65536

struct FDynLightData
{
	TArray<FDynLightInfo> arrays[3];

	void Clear()
	{
		arrays[LIGHTARRAY_NORMAL].Clear();
		arrays[LIGHTARRAY_SUBTRACTIVE].Clear();
		arrays[LIGHTARRAY_ADDITIVE].Clear();
	}

};

extern thread_local FDynLightData lightdata;


#endif
