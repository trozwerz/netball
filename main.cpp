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

	// Render scale: 3 = 7680x4800 (default); --1x..--4x select other sizes.
	int renderScale = 3;
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument.size() == 4 && argument[0] == '-' && argument[1] == '-' &&
			argument[2] >= '1' && argument[2] <= '4' && argument[3] == 'x')
			renderScale = argument[2] - '0';
	}

	NetballGame game;
	if (game.Construct({ 2560 * renderScale, 1600 * renderScale }, { 1, 1 }))
		game.Start();
	return 0;
}
