#pragma once

#include "olcPixelGameEngine3.h"

namespace netball
{
	void DrawCourt(olc::Draw& draw, olc::Image& courtTexture);
	void DrawFloor(olc::Draw& draw, olc::Image& floorTexture);
	void DrawSuperShotZone(olc::Draw& draw);
	void DrawGoalStructures(olc::Draw& draw, olc::Image& postTexture);
	void DrawDebugZoneLines(olc::Draw& draw);
}
