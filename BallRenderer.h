#pragma once

#include "olcPixelGameEngine3.h"

namespace netball
{
	void DrawBall(olc::Draw& draw, const olc::vf4d& position,
		olc::Image& texture, float spinAngle, bool drawShadow);

	void DrawPassDestination(olc::Draw& draw, const olc::vf2d& position,
		olc::Pixel colour);
}
