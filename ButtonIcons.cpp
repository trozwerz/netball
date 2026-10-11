#include "ButtonIcons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace netball
{
	namespace
	{
		std::array<olc::Image, 16> icons;
		bool iconsLoaded = false;

		// Sheet position (left to right) of each pass option's button, per platform.
		// Options are the face buttons A, B, X, Y in Xbox layout positions.
		constexpr std::array<std::array<std::size_t, 4>, 4> sheetIndex{ {
			{ 0, 1, 2, 3 },  // PC: Z X C V
			{ 0, 2, 3, 1 },  // PlayStation: cross, circle, square, triangle
			{ 2, 3, 0, 1 },  // Xbox sheet is X Y A B
			{ 3, 2, 0, 1 }   // Switch sheet is Y X A B
		} };

		struct Blob
		{
			int minX, minY, maxX, maxY, area;
		};
	}

	olc::Image* ButtonIcon(const ButtonPlatform platform, const std::size_t option)
	{
		if (!iconsLoaded || option >= 4)
			return nullptr;
		return &icons[std::size_t(platform) * 4 + sheetIndex[std::size_t(platform)][option]];
	}

	bool LoadButtonIcons(olc::PixelGameEngine& engine, const std::string& path)
	{
		olc::ImageConfig config;
		config.Filtered = true;
		olc::Image sheet;
		if (!engine.CreateImageFromFile(sheet, path, config))
			return false;

		const int width = sheet.Size().x;
		const int height = sheet.Size().y;
		const olc::Pixel* pixels = sheet.Data();

		// The sheet has a baked-in checkerboard, so buttons are found as the
		// dark rings / saturated colours that the grey checkerboard lacks.
		std::vector<char> foreground(std::size_t(width) * height, 0);
		for (int i = 0; i < width * height; ++i)
		{
			const olc::Pixel p = pixels[i];
			const int hi = std::max({ int(p.r), int(p.g), int(p.b) });
			const int lo = std::min({ int(p.r), int(p.g), int(p.b) });
			foreground[i] = hi < 90 || hi - lo > 60;
		}

		std::vector<Blob> blobs;
		std::vector<char> visited(foreground.size(), 0);
		std::vector<int> stack;
		for (int start = 0; start < width * height; ++start)
		{
			if (!foreground[start] || visited[start])
				continue;
			Blob blob{ width, height, 0, 0, 0 };
			stack.assign(1, start);
			visited[start] = 1;
			while (!stack.empty())
			{
				const int index = stack.back();
				stack.pop_back();
				const int x = index % width;
				const int y = index / width;
				blob.minX = std::min(blob.minX, x);
				blob.maxX = std::max(blob.maxX, x);
				blob.minY = std::min(blob.minY, y);
				blob.maxY = std::max(blob.maxY, y);
				++blob.area;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int nx = x + dx;
						const int ny = y + dy;
						if (nx < 0 || ny < 0 || nx >= width || ny >= height)
							continue;
						const int next = ny * width + nx;
						if (foreground[next] && !visited[next])
						{
							visited[next] = 1;
							stack.push_back(next);
						}
					}
			}
			if (blob.area > 600 && std::abs((blob.maxX - blob.minX) - (blob.maxY - blob.minY)) < 6 &&
				blob.maxX - blob.minX > 25 && blob.maxX - blob.minX < 60)
				blobs.push_back(blob);
		}
		if (blobs.size() != 16)
		{
			for (const Blob& b : blobs)
				std::fprintf(stderr, "blob %d,%d-%d,%d area %d\n", b.minX, b.minY, b.maxX, b.maxY, b.area);
			std::fprintf(stderr, "sheet %dx%d blobs %zu\n", width, height, blobs.size());
			return false;
		}

		// Quadrants in order: PC, PlayStation, Xbox, Switch; left to right within each.
		const auto quadrant = [&](const Blob& b)
		{
			return ((b.minY + b.maxY) > height ? 2 : 0) +
				((b.minX + b.maxX) > width ? 1 : 0);
		};
		std::sort(blobs.begin(), blobs.end(), [&](const Blob& a, const Blob& b)
		{
			if (quadrant(a) != quadrant(b))
				return quadrant(a) < quadrant(b);
			return a.minX < b.minX;
		});

		for (std::size_t n = 0; n < icons.size(); ++n)
		{
			const Blob& blob = blobs[n];
			const int size = std::max(blob.maxX - blob.minX, blob.maxY - blob.minY) + 1;
			const float centreX = 0.5f * float(blob.minX + blob.maxX);
			const float centreY = 0.5f * float(blob.minY + blob.maxY);
			if (!engine.CreateImage(icons[n], { size, size }, config))
				return false;
			olc::Pixel* destination = icons[n].Data();
			const float radius = 0.5f * float(size);
			const int originX = int(std::lround(centreX - radius + 0.5f));
			const int originY = int(std::lround(centreY - radius + 0.5f));
			for (int y = 0; y < size; ++y)
				for (int x = 0; x < size; ++x)
				{
					const int sx = originX + x;
					const int sy = originY + y;
					olc::Pixel p = olc::Pixel(0, 0, 0, 0);
					if (sx >= 0 && sy >= 0 && sx < width && sy < height)
					{
						p = pixels[sy * width + sx];
						const float distance = std::hypot(
							float(x) + 0.5f - radius, float(y) + 0.5f - radius);
						const float edge = std::clamp(radius - distance, 0.0f, 1.5f) / 1.5f;
						p.a = uint8_t(255.0f * edge);
					}
					destination[y * size + x] = p;
				}
			icons[n].BindCPU();
		}
		iconsLoaded = true;
		return true;
	}
}
