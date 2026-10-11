#pragma once

#include "olcPixelGameEngine3.h"

namespace netball
{
	// Side-view camera that eases between a full-court view and a zoomed
	// view of the third the ball is in.
	class Camera
	{
	public:
		void Snap(float ballZ);
		void Update(float elapsedTime, float ballZ);
		olc::mf4d ViewMatrix() const;

	private:
		static float TargetFocus(float ballZ);

		float focus = 0.0f;  // 0 = full court, 1 = attacking-third close-up
		float panZ = 0.0f;   // court-length position the camera centres on
		float zoom = 1.0f;   // 1 = full court, <1 = closer
	};
}
