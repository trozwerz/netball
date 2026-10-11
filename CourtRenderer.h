#pragma once

#include "olcPixelGameEngine3.h"

namespace netball
{
	void DrawCourt(olc::Draw& draw, olc::Image& courtTexture);
	void DrawGoalStructures(olc::Draw& draw);
	void DrawDebugZoneLines(olc::Draw& draw);
}
