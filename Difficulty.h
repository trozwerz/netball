#pragma once

#include <array>

namespace netball
{
	struct DifficultySettings
	{
		const char* name;
		float userMeterWindow;     // half-width of the guaranteed-shot zone on the meter
		float userScatter;         // scale on miss scatter for the user's shots
		float userMissScale;       // scale on the distance miss chance for the user
		float collisionScale;      // scale on player hit boxes
		float homeDropScale;       // scale on dropped-pass chance for home passers
		float awayDropScale;       // scale on dropped-pass chance for away passers
		float aiMissScale;         // scale on the AI's miss chances
		float aiFixedMissChance;   // when >= 0, AI miss chance is exactly this
	};

	// Medium is the original tuning (with the widened user meter window).
	inline constexpr std::array<DifficultySettings, 4> Difficulties{ {
		{ "Easy",                  0.050f, 0.8f, 0.7f, 0.5f, 0.5f, 1.5f, 1.4f, -1.0f },
		{ "Medium",                0.036f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f },
		{ "Hard",                  0.026f, 1.3f, 1.2f, 1.0f, 1.5f, 0.6f, 0.6f, -1.0f },
		{ "I'm Netball Incarnate", 0.018f, 1.6f, 1.4f, 1.0f, 2.0f, 0.1f, 0.0f, 0.10f }
	} };
}
