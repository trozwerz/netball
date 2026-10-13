#pragma once

#include "olcPixelGameEngine3.h"

#include <array>

namespace netball
{
	// One frame of Xbox-style controller input. Face buttons A, B, X, Y map to
	// pass options 0..3 (Z, X, C, V on the keyboard); either trigger shoots.
	struct GamepadState
	{
		bool connected = false;
		bool anyActivity = false;
		olc::vf2d move{};              // x = along court (right +), y = across court (up +)
		std::array<bool, 4> passPressed{};
		bool shootPressed = false;
		bool cyclePressed = false;     // either bumper: cycle receiver
		bool startPressed = false;
		bool confirmHeld = false;      // the confirm face button (A / cross) is down
		bool backPressed = false;
	};

	// Polls the first connected controller. Returns a disconnected state on
	// platforms without XInput.
	GamepadState PollGamepad();
}
