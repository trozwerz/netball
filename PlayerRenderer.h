#pragma once

#include "Players.h"
#include "ButtonIcons.h"
#include "olcPixelGameEngine3.h"

#include <array>

namespace netball
{
	void DrawPlayers(olc::Draw& draw, const Teams& players, std::size_t selectedPlayer,
		std::size_t zPassTarget, std::size_t xPassTarget, std::size_t cPassTarget,
		olc::Image* textures = nullptr, 		const std::vector<Player>& extras = {},
				const olc::Pixel* hairColours = nullptr);
			void DrawPassTargetLabels(olc::Draw& draw, const Teams& players,
		const std::array<std::size_t, 4>& targets, ButtonPlatform platform,
		const olc::vi2d& screenSize);
}
