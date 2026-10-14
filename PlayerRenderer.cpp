#include "PlayerRenderer.h"

#include "ButtonIcons.h"
#include "FontRenderer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;

	olc::Pixel Shade(const uint8_t r, const uint8_t g, const uint8_t b)
	{
		return olc::Pixel(r, g, b);
	}

	void AddTriangle(std::vector<olc::vf4d>& points,
		std::vector<olc::Pixel>& colours,
		const olc::vf4d& a, const olc::vf4d& b, const olc::vf4d& c,
		const olc::Pixel colour)
	{
		points.insert(points.end(), { a, b, c });
		colours.push_back(colour);
		colours.push_back(colour);
		colours.push_back(colour);
	}

	void DrawGroundCircle(olc::Draw& draw, const olc::vf4d& position)
	{
		constexpr int segments = 40;
		constexpr float innerRadius = 0.89f;
		constexpr float outerRadius = 1.19f;
		constexpr float y = 0.045f;
		std::vector<olc::vf4d> points;
		std::vector<olc::Pixel> colours;
		points.reserve(segments * 6);
		colours.reserve(segments * 6);
		const olc::Pixel colour(255, 224, 96, 230);
		for (int i = 0; i < segments; ++i)
		{
			const float a0 = 2.0f * Pi * float(i) / float(segments);
			const float a1 = 2.0f * Pi * float(i + 1) / float(segments);
			const olc::vf4d inner0{
				position.x + innerRadius * std::cos(a0), y,
				position.z + innerRadius * std::sin(a0)
			};
			const olc::vf4d inner1{
				position.x + innerRadius * std::cos(a1), y,
				position.z + innerRadius * std::sin(a1)
			};
			const olc::vf4d outer0{
				position.x + outerRadius * std::cos(a0), y,
				position.z + outerRadius * std::sin(a0)
			};
			const olc::vf4d outer1{
				position.x + outerRadius * std::cos(a1), y,
				position.z + outerRadius * std::sin(a1)
			};
			points.insert(points.end(),
				{ inner0, outer0, outer1, inner0, outer1, inner1 });
			colours.insert(colours.end(), 6, colour);
		}
		draw.SetBlendMode(olc::BlendMode::Alpha);
		draw.Mesh(olc::Structure::List, points, colours);
	}

	olc::vf4d LightDirection()
	{
		const olc::vf4d light{ 0.45f, 0.8f, 0.3f, 0.0f };
		const float length = std::sqrt(light.x * light.x + light.y * light.y +
			light.z * light.z);
		return { light.x / length, light.y / length, light.z / length, 0.0f };
	}

	olc::Pixel ShadeVertex(const olc::Pixel colour, const olc::vf4d& vertex)
	{
		static const olc::vf4d light = LightDirection();
		const float verticalRadius = (3.44f - 0.06f) * 0.5f;
		const float horizontalRadius = netball::PlayerHalfWidth;
		olc::vf4d normal{
			vertex.x / (horizontalRadius * horizontalRadius),
			vertex.y / (verticalRadius * verticalRadius),
			vertex.z / (horizontalRadius * horizontalRadius), 0.0f
		};
		const float length = std::sqrt(normal.x * normal.x +
			normal.y * normal.y + normal.z * normal.z);
		if (length > 0.0f)
		{
			normal.x /= length;
			normal.y /= length;
			normal.z /= length;
		}
		const float diffuse = std::max(0.0f,
			normal.x * light.x + normal.y * light.y + normal.z * light.z);
		const float brightness = 0.6f + 0.6f * diffuse;
		const auto scale = [brightness](const uint8_t channel)
		{
			return uint8_t(std::min(255.0f, float(channel) * brightness));
		};
		return olc::Pixel(scale(colour.r), scale(colour.g), scale(colour.b));
	}

	const std::vector<olc::vf4d>& GetEggMesh()
	{
		constexpr int latitudeSegments = 16;
		constexpr int longitudeSegments = 24;
		static const std::vector<olc::vf4d> mesh = []
		{
			const float verticalRadius = (3.44f - 0.06f) * 0.5f;
			const float horizontalRadius = netball::PlayerHalfWidth;
			std::vector<olc::vf4d> points;
			points.reserve(latitudeSegments * longitudeSegments * 6);
			for (int latitude = 0; latitude < latitudeSegments; ++latitude)
			{
				const float phi0 = Pi * float(latitude) / float(latitudeSegments);
				const float phi1 = Pi * float(latitude + 1) / float(latitudeSegments);
				for (int longitude = 0; longitude < longitudeSegments; ++longitude)
				{
					const float theta0 = 2.0f * Pi * float(longitude) /
						float(longitudeSegments);
					const float theta1 = 2.0f * Pi * float(longitude + 1) /
						float(longitudeSegments);
					const auto eggPoint = [&](const float phi, const float theta)
					{
						const float taper = 1.0f - 0.2f * std::cos(phi);
						const float radius = horizontalRadius * std::sin(phi) * taper;
						return olc::vf4d{
							radius * std::cos(theta),
							verticalRadius * std::cos(phi),
							radius * std::sin(theta)
						};
					};
					const olc::vf4d topLeft = eggPoint(phi0, theta0);
					const olc::vf4d topRight = eggPoint(phi0, theta1);
					const olc::vf4d bottomLeft = eggPoint(phi1, theta0);
					const olc::vf4d bottomRight = eggPoint(phi1, theta1);
					points.insert(points.end(), {
						topLeft, bottomLeft, topRight,
						topRight, bottomLeft, bottomRight
					});
				}
			}
			return points;
		}();
		return mesh;
	}
}

namespace netball
{
	void DrawPlayers(olc::Draw& draw, const Teams& teamPlayers,
		const std::size_t selectedPlayer, const std::size_t zPassTarget,
		const std::size_t xPassTarget, const std::size_t cPassTarget,
		olc::Image* textures, 		const std::vector<Player>& extras,
				const olc::Pixel* hairColours)
			{
		// Substitutes are drawn like players, after the on-court ones.
		std::vector<Player> players(teamPlayers.begin(), teamPlayers.end());
		players.insert(players.end(), extras.begin(), extras.end());
		const std::size_t courtPlayerCount = teamPlayers.size();
		const std::vector<olc::vf4d>& eggMesh = GetEggMesh();
		static std::vector<float> brightnessCache;
		static std::vector<olc::Pixel> whiteShadeCache;
		static std::vector<olc::vf2d> uvCache;
		static std::vector<std::size_t> texturedTriangleVertices;
		if (brightnessCache.empty())
		{
			const float verticalRadius = (3.44f - 0.06f) * 0.5f;
			const float horizontalRadius = PlayerHalfWidth;
			// Face placement: FaceCentreOnEgg is how far down the egg (0 = top,
			// 1 = bottom) the face centre sits; FaceZoom > 1 enlarges the face.
			constexpr float FaceCentreV = 0.293f;
			constexpr float FaceCentreOnEgg = 0.2025f;
			constexpr float FaceZoom = 1.6f;
			// Same image pixels per metre horizontally and vertically, so the
			// egg's tall shape no longer stretches the picture.
			const float ImageSpanPerEgg = verticalRadius / horizontalRadius;
			for (const olc::vf4d& vertex : eggMesh)
			{
				whiteShadeCache.push_back(
					ShadeVertex(olc::Pixel(255, 255, 255), vertex));
				brightnessCache.push_back(
					float(ShadeVertex(olc::Pixel(100, 100, 100), vertex).r) / 100.0f);
				uvCache.push_back({
					0.5f + vertex.z / (2.0f * horizontalRadius) / FaceZoom,
					FaceCentreV + ((verticalRadius - vertex.y) /
						(2.0f * verticalRadius) - FaceCentreOnEgg) *
						ImageSpanPerEgg / FaceZoom
				});
			}
			// Only triangles that land inside the picture get the texture;
			// otherwise the image repeats and the egg grows a second head.
			for (std::size_t t = 0; t + 2 < uvCache.size(); t += 3)
			{
				bool inside = true;
				for (std::size_t k = 0; k < 3; ++k)
				{
					const olc::vf2d& uv = uvCache[t + k];
					inside = inside && uv.x >= 0.0f && uv.x <= 1.0f &&
						uv.y >= 0.0f && uv.y <= 0.75f;
				}
				if (inside)
					for (std::size_t k = 0; k < 3; ++k)
						texturedTriangleVertices.push_back(t + k);
			}
		}
		static std::vector<olc::vf4d> points;
		static std::vector<olc::Pixel> colours;
		points.clear();
		colours.clear();
		points.reserve(eggMesh.size() * players.size());
		colours.reserve(eggMesh.size() * players.size());

		for (std::size_t i = 0; i < players.size(); ++i)
		{
			const Player& player = players[i];
			const float centerY = (3.44f + 0.06f) * 0.5f;
			const olc::Pixel teamColour = player.homeTeam
				? HomeTeamColour : AwayTeamColour;
			const olc::Pixel hairColour = hairColours && i < 18
				? hairColours[i] : teamColour;
			for (std::size_t v = 0; v < eggMesh.size(); ++v)
			{
				const olc::vf4d& vertex = eggMesh[v];
				const float brightness = brightnessCache[v];
				// Top 10% of the egg's height takes the hair colour.
				const olc::Pixel baseColour =
					vertex.y > 0.8f * (3.44f - 0.06f) * 0.5f
					? hairColour : teamColour;
				points.push_back({
					vertex.x + player.position.x,
					vertex.y + centerY + player.position.y,
					vertex.z + player.position.z
				});
				colours.push_back(olc::Pixel(
					uint8_t(baseColour.r * brightness),
					uint8_t(baseColour.g * brightness),
					uint8_t(baseColour.b * brightness)));
			}
		}
		draw.Mesh(olc::Structure::List, points, colours);

		if (textures)
		{
			constexpr float inflate = 1.02f;
			static std::vector<olc::vf4d> texturedPoints;
			static std::vector<olc::Pixel> texturedColours;
			static std::vector<olc::vf2d> uvs;
			draw.SetBlendMode(olc::BlendMode::Alpha);
			for (std::size_t variant = 0; variant < std::min<std::size_t>(players.size(), 18); ++variant)
			{
				texturedPoints.clear();
				texturedColours.clear();
				uvs.clear();
				for (std::size_t i = variant; i < players.size(); i += players.size())
				{
					const Player& player = players[i];
					const float centerY = (3.44f + 0.06f) * 0.5f;
					for (const std::size_t v : texturedTriangleVertices)
					{
						const olc::vf4d& vertex = eggMesh[v];
						texturedPoints.push_back({
							vertex.x * inflate + player.position.x,
							vertex.y * inflate + centerY + player.position.y,
							vertex.z * inflate + player.position.z
						});
						texturedColours.push_back(whiteShadeCache[v]);
						uvs.push_back(uvCache[v]);
					}
				}
				if (!texturedPoints.empty())
					draw.Mesh(olc::Structure::List, texturedPoints,
						texturedColours, uvs, textures[variant]);
			}
		}

		if (selectedPlayer < courtPlayerCount)
			DrawGroundCircle(draw, players[selectedPlayer].position);

		(void)zPassTarget;
		(void)xPassTarget;
		(void)cPassTarget;
	}

	void DrawPassTargetLabels(olc::Draw& draw, const Teams& players,
		const std::array<std::size_t, 4>& targets, const ButtonPlatform platform,
		const olc::vi2d& screenSize)
	{
		const float labelUi = float(screenSize.y) / 1600.0f;
		const olc::vf2d labelScale{ 3.0f * labelUi, 3.0f * labelUi };
		const auto drawLabel = [&](const std::size_t playerIndex,
			const std::size_t iconIndex, const std::string_view label,
			const olc::Pixel colour)
		{
			if (playerIndex >= players.size())
				return;

			const olc::vf4d& player = players[playerIndex].position;
			const olc::vf4d clipPosition = draw.GetMVPMatrix() *
				olc::vf4d{ player.x, 3.44f, player.z, 1.0f };
			if (clipPosition.w <= 0.0f)
				return;

			const float ndcX = clipPosition.x / clipPosition.w;
			const float ndcY = clipPosition.y / clipPosition.w;
			if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f)
				return;

			olc::Image* icon = ButtonIcon(platform, iconIndex);
			const olc::vf2d fontLabelScale = labelScale * HudFontScale();
			const olc::vf2d textSize =
				draw.GetTextSize(label, true, fontLabelScale, HudFont());
			const float iconSize = icon ? 64.0f * labelUi : 0.0f;
			const float gap = icon ? 8.0f * labelUi : 0.0f;
			const float height = std::max(textSize.y, iconSize);
			const olc::vf2d totalSize{ iconSize + gap + textSize.x, height };
			const olc::vf2d origin{
				(ndcX + 1.0f) * 0.5f * float(screenSize.x) - totalSize.x * 0.5f,
				(ndcY + 1.0f) * 0.5f * float(screenSize.y) - totalSize.y -
					18.0f * labelUi
			};
			const olc::vf2d textPosition{
				origin.x + iconSize + gap, origin.y + (height - textSize.y) * 0.5f };
			if (icon)
			{
				draw.SetBlendMode(olc::BlendMode::Alpha);
				draw.ImageRect(*icon, origin, { iconSize, iconSize });
			}
			draw.StringProp(textPosition + olc::vf2d{ 2.0f, 2.0f } * labelUi,
				label, olc::Colour::BLACK, fontLabelScale, HudFont());
			draw.StringProp(textPosition, label, colour, fontLabelScale,
				HudFont());
		};

		// Without an icon (or with a gamepad) the button letter is shown as text.
		const auto makeLabel = [&](const char key, const std::size_t iconIndex,
			const std::size_t playerIndex)
		{
			if (playerIndex >= players.size())
				return std::string{};
			const std::string role =
				netball::Positions[players[playerIndex].role].abbreviation;
			return !ButtonIcon(platform, iconIndex)
				? std::string(1, key) + ":" + role : role;
		};

		constexpr std::array<char, 4> keyLetters{ 'Z', 'X', 'C', 'V' };
		constexpr std::array<char, 4> padLetters{ 'A', 'B', 'X', 'Y' };
		const std::array<olc::Pixel, 4> colours{
			Shade(80, 245, 245), Shade(255, 205, 70),
			Shade(120, 230, 120), Shade(255, 120, 120) };
		const bool keyboard = platform == ButtonPlatform::PC;
		for (std::size_t option = 0; option < targets.size(); ++option)
			drawLabel(targets[option], option,
				makeLabel(keyboard ? keyLetters[option] : padLetters[option],
					option, targets[option]), colours[option]);
	}
}
