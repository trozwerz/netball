#include "BallRenderer.h"

#include "GameRules.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;

	void AddTriangle(std::vector<olc::vf4d>& points,
		std::vector<olc::Pixel>& colours,
		const olc::vf4d& a, const olc::vf4d& b, const olc::vf4d& c,
		const olc::Pixel colour)
	{
		points.insert(points.end(), { a, b, c });
		colours.insert(colours.end(), 3, colour);
	}

	void DrawShadow(olc::Draw& draw, const olc::vf4d& position)
	{
		// Light direction matches the player shading (x, y, z).
		constexpr float lightX = 0.45f;
		constexpr float lightY = 0.8f;
		constexpr float lightZ = 0.3f;
		constexpr int segments = 24;
		constexpr float y = 0.03f;
		const float height = std::max(position.y, 0.0f);
		const float radius = netball::BallRadius * (1.0f + height * 0.05f);
		const float centreX = position.x - lightX / lightY * height;
		const float centreZ = position.z - lightZ / lightY * height;
		const float alpha = std::clamp(120.0f - height * 12.0f, 40.0f, 120.0f);
		const olc::Pixel shadow(0, 0, 0, uint8_t(alpha));
		std::vector<olc::vf4d> points;
		std::vector<olc::Pixel> colours;
		for (int i = 0; i < segments; ++i)
		{
			const float a0 = 2.0f * Pi * float(i) / float(segments);
			const float a1 = 2.0f * Pi * float(i + 1) / float(segments);
			points.insert(points.end(), {
				{ centreX, y, centreZ },
				{ centreX + radius * std::cos(a0), y, centreZ + radius * std::sin(a0) },
				{ centreX + radius * std::cos(a1), y, centreZ + radius * std::sin(a1) }
			});
			colours.insert(colours.end(), 3, shadow);
		}
		draw.Mesh(olc::Structure::List, points, colours);
	}
}

namespace netball
{
	void DrawPassDestination(olc::Draw& draw, const olc::vf2d& position,
		const olc::Pixel colour)
	{
		constexpr int segments = 48;
		constexpr float outerRadius = 0.9f;
		constexpr float innerRadius = 0.75f;
		constexpr float y = 0.04f;
		draw.SetBlendMode(olc::BlendMode::Alpha);
		std::vector<olc::vf4d> points;
		for (int i = 0; i < segments; ++i)
		{
			const float a0 = 2.0f * Pi * float(i) / float(segments);
			const float a1 = 2.0f * Pi * float(i + 1) / float(segments);
			const olc::vf4d o0{ position.x + outerRadius * std::cos(a0), y,
				position.y + outerRadius * std::sin(a0) };
			const olc::vf4d o1{ position.x + outerRadius * std::cos(a1), y,
				position.y + outerRadius * std::sin(a1) };
			const olc::vf4d i0{ position.x + innerRadius * std::cos(a0), y,
				position.y + innerRadius * std::sin(a0) };
			const olc::vf4d i1{ position.x + innerRadius * std::cos(a1), y,
				position.y + innerRadius * std::sin(a1) };
			points.insert(points.end(), { o0, i0, o1, o1, i0, i1 });
		}
		draw.Mesh(olc::Structure::List, points,
			std::vector<olc::Pixel>(points.size(),
				olc::Pixel(colour.r, colour.g, colour.b, 230)));
	}

	void DrawBall(olc::Draw& draw, const olc::vf4d& position,
		olc::Image& texture, const float spinAngle, const bool drawShadow)
	{
		constexpr float radius = BallRadius;
		constexpr int latitudeSegments = 48;
		constexpr int longitudeSegments = 96;
		constexpr float tilt = 0.6f;
		const float cosSpin = std::cos(spinAngle);
		const float sinSpin = std::sin(spinAngle);
		const float cosTilt = std::cos(tilt);
		const float sinTilt = std::sin(tilt);
		std::vector<olc::vf4d> points;
		std::vector<olc::vf2d> textureCoordinates;
		points.reserve(latitudeSegments * longitudeSegments * 6);
		textureCoordinates.reserve(latitudeSegments * longitudeSegments * 6);

		const auto spherePoint = [&](const float phi, const float theta)
		{
			const float x = radius * std::sin(phi) * std::cos(theta);
			const float y = radius * std::cos(phi);
			const float z = radius * std::sin(phi) * std::sin(theta);
			const float spunX = x * cosSpin + z * sinSpin;
			const float spunZ = -x * sinSpin + z * cosSpin;
			return olc::vf4d{
				position.x + spunX * cosTilt - y * sinTilt,
				position.y + spunX * sinTilt + y * cosTilt,
				position.z + spunZ
			};
		};
		const auto uv = [&](const float phi, const float theta)
		{
			return olc::vf2d{ theta / (2.0f * Pi), phi / Pi };
		};

		for (int latitude = 0; latitude < latitudeSegments; ++latitude)
		{
			const float phi0 = Pi * float(latitude) / float(latitudeSegments);
			const float phi1 = Pi * float(latitude + 1) / float(latitudeSegments);
			for (int longitude = 0; longitude < longitudeSegments; ++longitude)
			{
				const float theta0 = 2.0f * Pi * float(longitude) / float(longitudeSegments);
				const float theta1 = 2.0f * Pi * float(longitude + 1) / float(longitudeSegments);
				points.insert(points.end(), {
					spherePoint(phi0, theta0), spherePoint(phi1, theta0),
					spherePoint(phi0, theta1),
					spherePoint(phi0, theta1), spherePoint(phi1, theta0),
					spherePoint(phi1, theta1)
				});
				textureCoordinates.insert(textureCoordinates.end(), {
					uv(phi0, theta0), uv(phi1, theta0), uv(phi0, theta1),
					uv(phi0, theta1), uv(phi1, theta0), uv(phi1, theta1)
				});
			}
		}

		draw.SetBlendMode(olc::BlendMode::Alpha);
		if (drawShadow)
			DrawShadow(draw, position);
		draw.Mesh(olc::Structure::List, points,
			std::vector<olc::Pixel>(points.size(), olc::Colour::WHITE),
			textureCoordinates, texture);
	}
}
