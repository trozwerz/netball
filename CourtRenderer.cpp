
#include "CourtRenderer.h"

#include "GameRules.h"

#include <array>
#include <cmath>
#include <vector>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;
	constexpr float HoopHeight = netball::HoopHeight;
	constexpr float HoopRadius = netball::HoopRadius;
	constexpr float HoopTubeRadius = netball::HoopTubeRadius;
	constexpr float PoleHalfWidth = netball::HoopPoleHalfWidth;

	void AddQuad(std::vector<olc::vf4d>& points, const olc::vf4d& a,
		const olc::vf4d& b, const olc::vf4d& c, const olc::vf4d& d,
		std::vector<olc::Pixel>& colours, const olc::Pixel colour)
	{
		points.insert(points.end(), { a, b, c, a, c, d });
		colours.insert(colours.end(), 6, colour);
	}

	void DrawHoopPost(olc::Draw& draw, olc::Image& postTexture, const float goalLine)
	{
		constexpr int sides = 24;
		constexpr float radius = PoleHalfWidth;
		constexpr float bottom = 0.05f;
		const float top = HoopHeight;
		// Each 90 degree face shows the lettered middle of the image at its
		// natural proportions, repeating up the pole.
		constexpr float uCentre = 0.5f, uHalfSpan = 0.18f;
		constexpr float tileV = 1.0f;
		constexpr float tileHeight = 1.77f;
		std::vector<olc::vf4d> points;
		std::vector<olc::vf2d> uvs;
		std::vector<olc::Pixel> colours;
		const olc::vf4d capCentreTop{ 0.0f, top, goalLine };
		// Letters appear on all four faces of the pole, centred on the x and z
		// axes, so they are readable from any camera direction.
		const auto uAt = [&](const float angle, const float faceMid)
		{
			const float face = std::round(faceMid / (0.5f * Pi));
			return uCentre - 2.0f * uHalfSpan * (angle - face * 0.5f * Pi) / (0.5f * Pi);
		};
		for (int i = 0; i < sides; ++i)
		{
			const float a0 = 2.0f * Pi * float(i) / float(sides);
			const float a1 = 2.0f * Pi * float(i + 1) / float(sides);
			const float x0 = radius * std::cos(a0), z0 = goalLine + radius * std::sin(a0);
			const float x1 = radius * std::cos(a1), z1 = goalLine + radius * std::sin(a1);
			const float mid = 0.5f * (a0 + a1);
			const float light = std::clamp(
				0.75f + 0.25f * (0.8f * std::cos(mid) + 0.5f * std::sin(mid)),
				0.5f, 1.0f);
			const uint8_t shade = uint8_t(255 * light);
			const olc::Pixel side(shade, shade, shade);
			float u0 = uAt(a0, mid), u1 = uAt(a1, mid);
			for (float y = bottom; y < top; y += tileHeight)
			{
				const float y1 = std::min(y + tileHeight, top);
				const float vTop = tileV * (1.0f - (y1 - y) / tileHeight);
				const olc::vf4d a{ x0, y, z0 }, b{ x0, y1, z0 },
					c{ x1, y1, z1 }, d{ x1, y, z1 };
				points.insert(points.end(), { a, b, c, a, c, d });
				uvs.insert(uvs.end(), { { u0, tileV }, { u0, vTop }, { u1, vTop },
					{ u0, tileV }, { u1, vTop }, { u1, tileV } });
				colours.insert(colours.end(), 6, side);
			}
			points.insert(points.end(), { capCentreTop, { x1, top, z1 }, { x0, top, z0 } });
			uvs.insert(uvs.end(), 3, olc::vf2d{ 0.1f, 0.1f });
			colours.insert(colours.end(), 3, olc::Pixel(200, 200, 200));
		}
		draw.Mesh(olc::Structure::List, points, colours, uvs, postTexture);
	}

	// A fake chrome look: a bright sky above, a dark horizon band and a mid-grey
	// floor below, plus a sharp highlight from the same light as the players.
	olc::Pixel MetalShade(const float nx, const float ny, const float nz)
	{
		float environment;
		if (ny >= 0.0f)
			environment = 0.62f + 0.55f * ny;
		else
			environment = 0.62f - 0.30f * -ny;
		const float horizon = std::clamp(1.0f - std::abs(ny - 0.05f) / 0.22f,
			0.0f, 1.0f);
		environment -= 0.40f * horizon;
		constexpr float lx = 0.45f, ly = 0.8f, lz = 0.3f;
		const float lightLength = std::sqrt(lx * lx + ly * ly + lz * lz);
		// Half-vector between the light and a viewer looking down the court.
		const float hx = lx / lightLength, hy = ly / lightLength + 0.5f,
			hz = lz / lightLength + 0.6f;
		const float hl = std::sqrt(hx * hx + hy * hy + hz * hz);
		const float spec = std::pow(
			std::max(0.0f, (nx * hx + ny * hy + nz * hz) / hl), 36.0f);
		const auto channel = [&](const float base)
		{
			return uint8_t(std::clamp(base * environment + 255.0f * spec * 0.9f,
				0.0f, 255.0f));
		};
		return olc::Pixel(channel(175.0f), channel(178.0f), channel(186.0f));
	}

	void DrawHoopRing(olc::Draw& draw, const float goalLine,
		const olc::Pixel colour)
	{
		constexpr int segments = 24;
		constexpr int tubeSides = 8;
		constexpr float ringRadius = HoopRadius;
		constexpr float tubeRadius = HoopTubeRadius;
		constexpr float ringHeight = HoopHeight;
		const float ringCenterZ =
			goalLine - std::copysign(netball::HoopCenterInset, goalLine);
		std::vector<olc::vf4d> points;
		std::vector<olc::Pixel> colours;
		points.reserve(segments * tubeSides * 6);
		colours.reserve(segments * tubeSides * 6);

		for (int segment = 0; segment < segments; ++segment)
		{
			const float angle0 = 2.0f * Pi * float(segment) / float(segments);
			const float angle1 = 2.0f * Pi * float(segment + 1) / float(segments);
			for (int side = 0; side < tubeSides; ++side)
			{
				const float sideAngle0 = 2.0f * Pi * float(side) / float(tubeSides);
				const float sideAngle1 = 2.0f * Pi * float(side + 1) / float(tubeSides);
				const auto ringPoint = [&](const float angle, const float sideAngle)
				{
					const float radial = ringRadius + tubeRadius * std::cos(sideAngle);
					return olc::vf4d{
						radial * std::cos(angle),
						ringHeight + tubeRadius * std::sin(sideAngle),
						ringCenterZ + radial * std::sin(angle)
					};
				};
				const float angleMid = 0.5f * (angle0 + angle1);
				const float sideMid = 0.5f * (sideAngle0 + sideAngle1);
				const olc::Pixel metal = MetalShade(
					std::cos(angleMid) * std::cos(sideMid), std::sin(sideMid),
					std::sin(angleMid) * std::cos(sideMid));
				AddQuad(points, ringPoint(angle0, sideAngle0),
					ringPoint(angle1, sideAngle0), ringPoint(angle1, sideAngle1),
					ringPoint(angle0, sideAngle1), colours, metal);
			}
		}

		draw.Mesh(olc::Structure::List, points, colours);
	}

	void DrawHoopNet(olc::Draw& draw, const float goalLine,
		const olc::Pixel blue)
	{
		constexpr int strands = 12;
		constexpr int bands = 3;
		constexpr float netDepth = netball::HoopNetDepth;
		constexpr float strandWidth = 0.028f;
		const float centerZ =
			goalLine - std::copysign(netball::HoopCenterInset, goalLine);
		const olc::Pixel highlight(
			uint8_t(blue.r + (255 - blue.r) / 2),
			uint8_t(blue.g + (255 - blue.g) / 2),
			uint8_t(blue.b + (255 - blue.b) / 2));
		std::vector<olc::vf4d> points;
		std::vector<olc::Pixel> colours;
		points.reserve((strands + strands * bands) * 6);
		colours.reserve((strands + strands * bands) * 6);

		const auto netPoint = [&](const float angle, const float amount)
		{
			const float radius = HoopRadius * 0.76f * (1.0f - amount) +
				0.16f * amount;
			return olc::vf2d{
				radius * std::cos(angle),
				centerZ + radius * std::sin(angle)
			};
		};

		for (int strand = 0; strand < strands; ++strand)
		{
			const float angle = 2.0f * Pi * float(strand) / float(strands);
			const float halfWidth = strandWidth / 2.0f;
			const olc::vf2d tangent{ -std::sin(angle), std::cos(angle) };
			const olc::vf2d top = netPoint(angle, 0.0f);
			const olc::vf2d bottom = netPoint(angle, 1.0f);
			const olc::vf2d topLeft = top - tangent * halfWidth;
			const olc::vf2d topRight = top + tangent * halfWidth;
			const olc::vf2d bottomLeft = bottom - tangent * halfWidth;
			const olc::vf2d bottomRight = bottom + tangent * halfWidth;
			AddQuad(points,
				{ topLeft.x, HoopHeight - 0.08f, topLeft.y },
				{ topRight.x, HoopHeight - 0.08f, topRight.y },
				{ bottomRight.x, HoopHeight - netDepth, bottomRight.y },
				{ bottomLeft.x, HoopHeight - netDepth, bottomLeft.y },
				colours, strand % 3 == 0 ? highlight : blue);
		}

		for (int band = 1; band <= bands; ++band)
		{
			const float amount = float(band) / float(bands + 1);
			const float y = HoopHeight - 0.08f - netDepth * amount;
			const float radius = HoopRadius * 0.76f * (1.0f - amount) +
				0.16f * amount;
			for (int strand = 0; strand < strands; ++strand)
			{
				const float angle0 = 2.0f * Pi * float(strand) / float(strands);
				const float angle1 = 2.0f * Pi * float(strand + 1) / float(strands);
				const float halfWidth = strandWidth / 2.0f;
				const float innerRadius = radius - halfWidth;
				const float outerRadius = radius + halfWidth;
				AddQuad(points,
					{ innerRadius * std::cos(angle0), y, centerZ + innerRadius * std::sin(angle0) },
					{ outerRadius * std::cos(angle0), y, centerZ + outerRadius * std::sin(angle0) },
					{ outerRadius * std::cos(angle1), y, centerZ + outerRadius * std::sin(angle1) },
					{ innerRadius * std::cos(angle1), y, centerZ + innerRadius * std::sin(angle1) },
					colours, blue);
			}
		}

		draw.Mesh(olc::Structure::List, points, colours);
	}

	void AddZoneLineQuad(std::vector<olc::vf4d>& points,
		std::vector<olc::Pixel>& colours,
		const olc::vf2d& a, const olc::vf2d& b, const float halfWidth,
		const float y, const olc::Pixel colour)
	{
		const olc::vf2d direction = (b - a).norm();
		const olc::vf2d normal{ -direction.y, direction.x };
		const olc::vf2d offset = normal * halfWidth;
		AddQuad(points,
			{ a.x + offset.x, y, a.y + offset.y },
			{ b.x + offset.x, y, b.y + offset.y },
			{ b.x - offset.x, y, b.y - offset.y },
			{ a.x - offset.x, y, a.y - offset.y },
			colours, colour);
	}
}

namespace netball
{
	void DrawFloor(olc::Draw& draw, olc::Image& floorTexture)
	{
		// Tiles keep the texture undistorted over a large area.
		constexpr float extent = 72.0f;
		constexpr float tileWidth = 12.0f;
		const float tileDepth = tileWidth *
			float(floorTexture.Size().y) / float(floorTexture.Size().x);
		constexpr float floorY = -0.03f;
		std::vector<olc::vf4d> points;
		std::vector<olc::vf2d> uvs;
		for (float z = -extent; z < extent; z += tileDepth)
			for (float x = -extent; x < extent; x += tileWidth)
			{
				const float x1 = x + tileWidth;
				const float z1 = z + tileDepth;
				points.insert(points.end(), {
					{ x, floorY, z }, { x, floorY, z1 }, { x1, floorY, z1 },
					{ x, floorY, z }, { x1, floorY, z1 }, { x1, floorY, z } });
				uvs.insert(uvs.end(), {
					{ 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f },
					{ 0.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } });
			}
		draw.Mesh(olc::Structure::List, points,
			std::vector<olc::Pixel>(points.size(), olc::Colour::WHITE),
			uvs, floorTexture);
	}

	void DrawCourt(olc::Draw& draw, olc::Image& courtTexture)
	{
		const float left = -CourtHalfWidth;
		const float right = CourtHalfWidth;
		const float back = -CourtHalfLength;
		const float thirdLength = CourtLength / 3.0f;
		const std::array<float, 4> courtZ{
			back, back + thirdLength, back + thirdLength * 2.0f, CourtHalfLength
		};
		constexpr std::array<float, 4> imageX{ 10.0f, 171.0f, 340.0f, 501.0f };
		std::vector<olc::vf4d> points;
		std::vector<olc::vf2d> textureCoordinates;
		points.reserve(18);
		textureCoordinates.reserve(18);

		const float lineY = 0.0f;
		const float vTop = 9.0f / 279.0f;
		const float vBottom = 270.0f / 279.0f;
		for (std::size_t third = 0; third < 3; ++third)
		{
			const float z0 = courtZ[third];
			const float z1 = courtZ[third + 1];
			const float u0 = imageX[third] / 512.0f;
			const float u1 = imageX[third + 1] / 512.0f;
			points.insert(points.end(), {
				{ left, lineY, z0 }, { left, lineY, z1 }, { right, lineY, z1 },
				{ left, lineY, z0 }, { right, lineY, z1 }, { right, lineY, z0 },
			});
			textureCoordinates.insert(textureCoordinates.end(), {
				{ u0, vBottom }, { u1, vBottom }, { u1, vTop },
				{ u0, vBottom }, { u1, vTop }, { u0, vTop },
			});
		}
		draw.Mesh(olc::Structure::List, points,
			std::vector<olc::Pixel>(points.size(), olc::Colour::WHITE),
			textureCoordinates, courtTexture);
	}

	void DrawGoalStructures(olc::Draw& draw, olc::Image& postTexture)
	{
		const olc::Pixel hoopColour = olc::Colour::WHITE;
		for (const float goalLine : { -CourtHalfLength, CourtHalfLength })
		{
			DrawHoopPost(draw, postTexture, goalLine);
			DrawHoopRing(draw, goalLine, hoopColour);
			DrawHoopNet(draw, goalLine, goalLine > 0.0f
				? netball::HomeTeamColour : netball::AwayTeamDisplayColour);
		}
	}

	void DrawDebugZoneLines(olc::Draw& draw)
	{
		constexpr float lineY = 0.08f;
		constexpr float halfWidth = 0.075f;
		constexpr int circleSegments = 64;
		const olc::Pixel magenta(255, 0, 255);
		std::vector<olc::vf4d> points;
		std::vector<olc::Pixel> colours;
		points.reserve(6 * 6 + 2 * circleSegments * 6);
		colours.reserve(points.capacity());

		const olc::Pixel yellow(255, 220, 0);
		for (const float x : { -CourtHalfWidth, CourtHalfWidth })
			AddZoneLineQuad(points, colours,
				{ x, -CourtHalfLength }, { x, CourtHalfLength },
				halfWidth, lineY, yellow);
		for (const float z : { -CourtHalfLength, CourtHalfLength })
			AddZoneLineQuad(points, colours,
				{ -CourtHalfWidth, z }, { CourtHalfWidth, z },
				halfWidth, lineY, yellow);

		const float thirdBoundary = CourtLength / 6.0f;
		for (const float z : { -thirdBoundary, thirdBoundary })
			AddZoneLineQuad(points, colours,
				{ -CourtHalfWidth, z }, { CourtHalfWidth, z },
				halfWidth, lineY, magenta);

		for (const float goalLine : { -CourtHalfLength, CourtHalfLength })
		{
			const float centerZ = goalLine -
				std::copysign(GoalCircleCenterInset, goalLine);
			for (int segment = 0; segment < circleSegments; ++segment)
			{
				const float angle0 = 2.0f * Pi * float(segment) /
					float(circleSegments);
				const float angle1 = 2.0f * Pi * float(segment + 1) /
					float(circleSegments);
				const olc::vf2d inner0{
					(GoalCircleRadiusX - halfWidth) * std::cos(angle0),
					centerZ + (GoalCircleRadiusZ - halfWidth) * std::sin(angle0)
				};
				const olc::vf2d outer0{
					(GoalCircleRadiusX + halfWidth) * std::cos(angle0),
					centerZ + (GoalCircleRadiusZ + halfWidth) * std::sin(angle0)
				};
				const olc::vf2d outer1{
					(GoalCircleRadiusX + halfWidth) * std::cos(angle1),
					centerZ + (GoalCircleRadiusZ + halfWidth) * std::sin(angle1)
				};
				const olc::vf2d inner1{
					(GoalCircleRadiusX - halfWidth) * std::cos(angle1),
					centerZ + (GoalCircleRadiusZ - halfWidth) * std::sin(angle1)
				};
				AddQuad(points,
					{ inner0.x, lineY, inner0.y },
					{ outer0.x, lineY, outer0.y },
					{ outer1.x, lineY, outer1.y },
					{ inner1.x, lineY, inner1.y },
					colours, magenta);
			}
		}

		draw.Mesh(olc::Structure::List, points, colours);
	}

	void DrawSuperShotZone(olc::Draw& draw)
	{
		constexpr float lineY = 0.09f;
		constexpr float halfWidth = 0.18f;
		constexpr int segments = 32;
		const olc::Pixel white = olc::Colour::WHITE;
		std::vector<olc::vf4d> points;
		std::vector<olc::Pixel> colours;
		for (const float goalLine : { -CourtHalfLength, CourtHalfLength })
		{
			const float centerZ = goalLine -
				std::copysign(GoalCircleCenterInset, goalLine);
			const float k = SuperShotRadiusScale;
			// Courtside half only: sin < 0 at the +Z end, sin > 0 at the -Z end.
			const float start = goalLine > 0.0f ? Pi : 0.0f;
			for (int segment = 0; segment < segments; ++segment)
			{
				const float angle0 = start + Pi * float(segment) / float(segments);
				const float angle1 = start + Pi * float(segment + 1) / float(segments);
				const auto point = [&](const float angle, const float offset)
				{
					return olc::vf4d{
						(k * GoalCircleRadiusX + offset) * std::cos(angle), lineY,
						centerZ + (k * GoalCircleRadiusZ + offset) * std::sin(angle) };
				};
				AddQuad(points, point(angle0, -halfWidth), point(angle0, halfWidth),
					point(angle1, halfWidth), point(angle1, -halfWidth),
					colours, white);
			}
		}
		draw.Mesh(olc::Structure::List, points, colours);
	}
}
