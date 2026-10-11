#pragma once

#include "olcPixelGameEngine3.h"

namespace netball
{
	enum class ButtonPlatform { PC, PlayStation, Xbox, Switch };

	// Pass-button icons cut from the sprite sheet; option 0..3 = Z X C V / A B X Y.
	bool LoadButtonIcons(olc::PixelGameEngine& engine, const std::string& path);
	olc::Image* ButtonIcon(ButtonPlatform platform, std::size_t option);
}
