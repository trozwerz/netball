#include "FontRenderer.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <vector>

namespace netball
{
	namespace
	{
		olc::Font hudFont;
		bool hudFontLoaded = false;

		// Tweak to make TTF text larger or smaller relative to the 8x8 font.
		constexpr float TextSizeAdjust = 1.2f;
		constexpr int AtlasWidth = 2048;
		constexpr int CellPadding = 2;
	}

	bool LoadHudFont(olc::PixelGameEngine& engine, const std::string& path,
		const float pixelHeight)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		const std::vector<unsigned char> fontData(
			(std::istreambuf_iterator<char>(file)),
			std::istreambuf_iterator<char>());

		stbtt_fontinfo info;
		if (fontData.empty() ||
			!stbtt_InitFont(&info, fontData.data(),
				stbtt_GetFontOffsetForIndex(fontData.data(), 0)))
			return false;

		const float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
		int ascent = 0, descent = 0, lineGap = 0;
		stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
		const int ascentPx = int(std::ceil(ascent * scale));
		const int cellHeight = int(std::ceil((ascent - descent) * scale)) + 2;

		struct Cell
		{
			int x = 0, y = 0, width = 0;
			float advance = 0.0f;
			int glyphX = 0, glyphY = 0, glyphW = 0, glyphH = 0;
			int codepoint = 0;
		};
		std::vector<Cell> cells(256);

		int cursorX = CellPadding, cursorY = CellPadding;
		for (int c = 32; c < 127; ++c)
		{
			Cell& cell = cells[c];
			cell.codepoint = c;

			int advanceUnits = 0, bearing = 0;
			stbtt_GetCodepointHMetrics(&info, c, &advanceUnits, &bearing);
			cell.advance = advanceUnits * scale;

			int x0, y0, x1, y1;
			stbtt_GetCodepointBitmapBox(&info, c, scale, scale, &x0, &y0, &x1, &y1);
			cell.glyphX = std::max(0, x0);
			cell.glyphY = ascentPx + y0;
			cell.glyphW = x1 - x0;
			cell.glyphH = y1 - y0;
			cell.width = std::max(int(std::ceil(cell.advance)),
				cell.glyphX + cell.glyphW);

			if (cursorX + cell.width + CellPadding > AtlasWidth)
			{
				cursorX = CellPadding;
				cursorY += cellHeight + CellPadding;
			}
			cell.x = cursorX;
			cell.y = cursorY;
			cursorX += cell.width + CellPadding;
		}
		const int atlasHeight = cursorY + cellHeight + CellPadding;

		if (!engine.CreateImage(hudFont.imgFont, { AtlasWidth, atlasHeight },
				olc::ImageConfig{ .Filtered = true }))
			return false;
		hudFont.imgFont.BindCPU();

		std::vector<unsigned char> coverage(
			size_t(AtlasWidth) * atlasHeight, 0);
		for (int c = 32; c < 127; ++c)
		{
			const Cell& cell = cells[c];
			if (cell.glyphW <= 0 || cell.glyphH <= 0)
				continue;
			const int originX = cell.x + cell.glyphX;
			const int originY = cell.y + std::max(0, cell.glyphY);
			const int skipRows = std::max(0, -cell.glyphY);
			stbtt_MakeCodepointBitmap(&info,
				coverage.data() + size_t(originY) * AtlasWidth + originX,
				cell.glyphW, cell.glyphH - skipRows, AtlasWidth,
				scale, scale, c);
		}

		olc::Pixel* pixels = hudFont.imgFont.Data();
		for (size_t i = 0; i < coverage.size(); ++i)
			pixels[i] = olc::Pixel(255, 255, 255, coverage[i]);

		hudFont.glyphs.clear();
		for (int c = 0; c < 256; ++c)
		{
			const Cell& cell = cells[c];
			if (cell.codepoint == 0)
			{
				hudFont.glyphs.push_back(olc::FontGlyph{
					hudFont.imgFont.region({ 0.0f, 0.0f }, { 1.0f, 1.0f }),
					0.0f, { cells[' '].advance, float(cellHeight) },
					{ cells[' '].advance, float(cellHeight) } });
				continue;
			}
			hudFont.glyphs.push_back(olc::FontGlyph{
				hudFont.imgFont.region(
					{ float(cell.x), float(cell.y) },
					{ float(cell.width), float(cellHeight) }),
				0.0f,
				{ cell.advance, float(cellHeight) },
				{ cell.advance, float(cellHeight) } });
		}

		hudFont.fLineHeight = float(cellHeight);
		hudFont.fTabWidth = cells[' '].advance * 4.0f;
		hudFontLoaded = true;
		return true;
	}

	olc::Font& HudFont()
	{
		return hudFontLoaded ? hudFont : olc::fontClassicPGE;
	}

	float HudFontScale()
	{
		return hudFontLoaded
			? 8.0f * TextSizeAdjust / hudFont.fLineHeight
			: 1.0f;
	}
}
