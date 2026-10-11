#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace netball
{
	namespace
	{
		constexpr float Pi = 3.14159265f;
		constexpr float Pitch = 67.5f * Pi / 180.0f;
		constexpr float BaseDistance = 23.0f;
		constexpr float ThirdBoundary = 5.083f;
		constexpr float BlendHalfWidth = 1.8f;
		constexpr float CloseZoom = 0.68f;
		constexpr float ClosePanZ = 7.5f;
		constexpr float SmoothTime = 0.45f;
	}

	float Camera::TargetFocus(const float ballZ)
	{
		const float t = std::clamp(
			(std::fabs(ballZ) - (ThirdBoundary - BlendHalfWidth)) /
			(2.0f * BlendHalfWidth), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	void Camera::Snap(const float ballZ)
	{
		focus = TargetFocus(ballZ);
		panZ = ballZ < 0.0f ? -ClosePanZ * focus : ClosePanZ * focus;
		zoom = 1.0f + (CloseZoom - 1.0f) * focus;
	}

	void Camera::Update(const float elapsedTime, const float ballZ)
	{
		// The side of the court is latched through the blend so the pan
		// never swings across the court while the ball crosses the middle.
		const float target = TargetFocus(ballZ);
		const float k = 1.0f - std::exp(-elapsedTime / SmoothTime);
		focus += (target - focus) * k;

		const float side = ballZ < -0.5f ? -1.0f : (ballZ > 0.5f ? 1.0f :
			(panZ < 0.0f ? -1.0f : 1.0f));
		const float targetPan = side * ClosePanZ * focus;
		panZ += (targetPan - panZ) * k;
		const float targetZoom = 1.0f + (CloseZoom - 1.0f) * focus;
		zoom += (targetZoom - zoom) * k;
	}

	olc::mf4d Camera::ViewMatrix() const
	{
		const float distance = BaseDistance * zoom;
		const float height = distance * std::tan(Pitch);
		olc::mf4d rotateCamera, translateCamera;
		rotateCamera.rotateX(Pi - Pitch);
		translateCamera.translate(-panZ, -height, distance);
		return rotateCamera * translateCamera;
	}
}
