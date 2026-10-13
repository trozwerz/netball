#define OLC_PGE3_APPLICATION
#include "olcPixelGameEngine3.h"

#include "NetballGame.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(const int argc, char* argv[])
{
	if (argc > 0)
	{
		std::error_code error;
		const std::filesystem::path executablePath =
			std::filesystem::absolute(argv[0], error);
		if (error)
		{
			std::cerr << "Unable to locate the game executable: " << error.message() << '\n';
			return 1;
		}
		std::filesystem::current_path(executablePath.parent_path(), error);
		if (error)
		{
			std::cerr << "Unable to locate the game assets: " << error.message() << '\n';
			return 1;
		}
	}

	// Render scale: 4 = 10240x6400 (default); --1x/--2x select lower resolutions.
	int renderScale = 4;
	for (int i = 1; i < argc; ++i)
	{
		if (std::string(argv[i]) == "--1x")
			renderScale = 1;
		else if (std::string(argv[i]) == "--2x")
			renderScale = 2;
	}

	NetballGame game;
	if (game.Construct({ 2560 * renderScale, 1600 * renderScale }, { 1, 1 }))
		game.Start();
	return 0;
}
