#pragma once

#include "olcPixelGameEngine3.h"

#include <string>

namespace netball
{
	// Bakes a TrueType font into an olc::Font. Returns false on failure,
	// in which case the built-in bitmap font keeps being used.
	bool LoadHudFont(olc::PixelGameEngine& engine, const std::string& path,
		float pixelHeight);

	// Font to draw HUD text with (loaded TTF, or the built-in fallback).
	olc::Font& HudFont();

	// Multiplier that makes HUD text the same size as the built-in 8x8 font
	// at a given scale, so existing layout values keep working.
	float HudFontScale();
}
