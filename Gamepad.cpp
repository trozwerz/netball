#include "Gamepad.h"

#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "winmm.lib")

namespace netball
{
	namespace
	{
		constexpr float StickDeadZone = 0.25f;
		constexpr BYTE TriggerThreshold = 100;
		// XInput is polled slowly when nothing is plugged in, to avoid a stall.
		constexpr int DisconnectedPollInterval = 120;
	}

	namespace
	{
		// DualShock 4 / DualSense appear as generic HID joysticks, not XInput.
		bool PollJoystick(GamepadState& state)
		{
			static DWORD previousButtons = 0;
			static bool previousTrigger = false;

			JOYINFOEX info{};
			info.dwSize = sizeof(info);
			info.dwFlags = JOY_RETURNALL;
			if (joyGetPosEx(JOYSTICKID1, &info) != JOYERR_NOERROR)
			{
				previousButtons = 0;
				previousTrigger = false;
				return false;
			}

			state.connected = true;
			const DWORD pressed = info.dwButtons & ~previousButtons;
			previousButtons = info.dwButtons;

			// Button order: 0 square, 1 cross, 2 circle, 3 triangle, 4 L1, 5 R1,
			// 6 L2, 7 R2, 8 share, 9 options.
			state.passPressed[0] = (pressed & (1u << 1)) != 0;
			state.passPressed[1] = (pressed & (1u << 2)) != 0;
			state.passPressed[2] = (pressed & (1u << 0)) != 0;
			state.passPressed[3] = (pressed & (1u << 3)) != 0;
			state.cyclePressed = (pressed & ((1u << 4) | (1u << 5))) != 0;
			state.backPressed = (pressed & (1u << 8)) != 0;
			state.startPressed = (pressed & (1u << 9)) != 0;

			const bool trigger = (info.dwButtons & ((1u << 6) | (1u << 7))) != 0;
			state.shootPressed = trigger && !previousTrigger;
			previousTrigger = trigger;

			olc::vf2d stick{
				(float(info.dwXpos) - 32767.5f) / 32767.5f,
				-(float(info.dwYpos) - 32767.5f) / 32767.5f };
			if (stick.mag() < StickDeadZone)
				stick = {};
			olc::vf2d dpad{};
			if (info.dwPOV != JOY_POVCENTERED)
			{
				const float angle = float(info.dwPOV) / 100.0f * 3.14159265f / 180.0f;
				dpad = { std::round(std::sin(angle)), std::round(std::cos(angle)) };
			}
			state.move = dpad.mag2() > 0.0f ? dpad : stick;
			state.anyActivity = pressed != 0 || state.shootPressed ||
				state.move.mag2() > 0.0f;
			return true;
		}
	}

	GamepadState PollGamepad()
	{
		static WORD previousButtons = 0;
		static bool previousTrigger = false;
		static int framesUntilPoll = 0;

		GamepadState state;
		if (framesUntilPoll > 0)
		{
			--framesUntilPoll;
			PollJoystick(state);
			return state;
		}

		XINPUT_STATE xinput{};
		if (XInputGetState(0, &xinput) != ERROR_SUCCESS)
		{
			framesUntilPoll = DisconnectedPollInterval;
			previousButtons = 0;
			previousTrigger = false;
			PollJoystick(state);
			return state;
		}

		state.connected = true;
		const XINPUT_GAMEPAD& pad = xinput.Gamepad;
		const WORD pressed = pad.wButtons & ~previousButtons;
		previousButtons = pad.wButtons;

		constexpr WORD faceButtons[4] = {
			XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y };
		for (std::size_t i = 0; i < 4; ++i)
			state.passPressed[i] = (pressed & faceButtons[i]) != 0;
		state.cyclePressed = (pressed & (XINPUT_GAMEPAD_LEFT_SHOULDER |
			XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0;
		state.startPressed = (pressed & XINPUT_GAMEPAD_START) != 0;
		state.backPressed = (pressed & XINPUT_GAMEPAD_BACK) != 0;

		const bool trigger = pad.bLeftTrigger > TriggerThreshold ||
			pad.bRightTrigger > TriggerThreshold;
		state.shootPressed = trigger && !previousTrigger;
		previousTrigger = trigger;

		olc::vf2d stick{
			std::max(-1.0f, float(pad.sThumbLX) / 32767.0f),
			std::max(-1.0f, float(pad.sThumbLY) / 32767.0f) };
		if (stick.mag() < StickDeadZone)
			stick = {};
		olc::vf2d dpad{};
		if (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) dpad.x += 1.0f;
		if (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) dpad.x -= 1.0f;
		if (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) dpad.y += 1.0f;
		if (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) dpad.y -= 1.0f;
		state.move = dpad.mag2() > 0.0f ? dpad : stick;

		state.anyActivity = pressed != 0 || state.shootPressed ||
			state.move.mag2() > 0.0f;
		return state;
	}
}
