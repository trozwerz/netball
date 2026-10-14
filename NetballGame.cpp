#include "NetballGame.h"

#include "BallRenderer.h"
#include "ButtonIcons.h"
#include "CourtRenderer.h"
#include "FontRenderer.h"
#include "GameRules.h"
#include "PlayerRenderer.h"
#include "WindowPlacement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;
	constexpr float MaxPlayerSpeed = 6.0f;
	constexpr float PlayerAcceleration = 10.0f;
	constexpr float PlayerDeceleration = 8.0f;
	constexpr float BallSideOffset = 1.1f;
	constexpr float CarriedBallHeight = 1.9f;
	constexpr float ShootingMeterMark = 0.72f;
	constexpr float BaseShootingMeterSpeed = 0.9f;
	constexpr float ShotPeakHeight = netball::HoopHeight * 1.15f * 1.10f;

	olc::Pixel Shade(const uint8_t r, const uint8_t g, const uint8_t b)
	{
		return olc::Pixel(r, g, b);
	}

	olc::vf2d MoveTowards(const olc::vf2d& current, const olc::vf2d& target,
		const float maxDelta)
	{
		const olc::vf2d difference = target - current;
		const float distance = difference.mag();
		if (distance <= maxDelta || distance == 0.0f)
			return target;
		return current + difference * (maxDelta / distance);
	}

	bool IsBallInAttackingThird(const olc::vf4d& position, const bool homeTeam)
	{
		const float boundary = homeTeam
			? netball::CourtHalfLength - netball::CourtLength / 3.0f
			: -netball::CourtHalfLength + netball::CourtLength / 3.0f;
		return homeTeam ? position.z >= boundary : position.z <= boundary;
	}
}

NetballGame::NetballGame()
{
	sAppName = "Netball - Zone Training - OneLoneCoder.com";
}

bool NetballGame::OnUserCreate()
{
	netball::SetPlayerCollisionScale(Difficulty().collisionScale);
	debugLog.open("netball_debug_log.txt", std::ios::out | std::ios::trunc);
	if (!debugLog)
	{
		std::cerr << "Unable to create netball_debug_log.txt in the game directory\n";
		return false;
	}
	debugLog << "Netball debug log\n"
		<< "Player coordinates are court-space x, y, z; teams: HOME/AWAY\n";

	if (!CreateImageFromFile(splashImage, "assets/SMN.jpg"))
	{
		std::cerr << "Unable to load splash image: assets/SMN.jpg\n";
		splashActive = false;
	}

	// Change the file name here to try a different font.
	netball::LoadHudFont(*this, "assets/fonts/Lato-Bold.ttf", 64.0f);
	if (!netball::LoadButtonIcons(*this, "assets/buttons.jpg"))
		std::cerr << "Unable to load button icons: assets/buttons.jpg\n";

	olc::ImageConfig ballConfig;
	ballConfig.Filtered = true;
	if (!CreateImageFromFile(ballTexture, "assets/ballTexture.jpg", ballConfig))
	{
		std::cerr << "Unable to load ball texture: assets/ballTexture.jpg\n";
		return false;
	}

	olc::ImageConfig playerConfig;
	playerConfig.Filtered = true;
	if (CreateImageFromFile(playerBase, "assets/player.png", playerConfig))
	{
		olc::Image& playerTexture = playerBase;
		// The PNG has an opaque baked-in checkerboard and a black footer, so
		// flood-fill the light-grey background from the edges and make it
		// transparent. Enclosed white areas (e.g. the number plate) survive.
		const int width = playerTexture.Size().x;
		const int height = playerTexture.Size().y;
		olc::Pixel* pixels = playerTexture.Data();
		const auto isBackground = [&](const olc::Pixel p)
		{
			const int hi = std::max({ int(p.r), int(p.g), int(p.b) });
			const int lo = std::min({ int(p.r), int(p.g), int(p.b) });
			return lo >= 150 && hi - lo <= 24;
		};
		int footerStart = height;
		while (footerStart > 0)
		{
			bool dark = true;
			for (int x = 0; x < width && dark; x += 8)
				dark = pixels[(footerStart - 1) * width + x].r < 30 &&
					pixels[(footerStart - 1) * width + x].g < 30;
			if (!dark)
				break;
			--footerStart;
		}
		std::vector<int> stack;
		std::vector<bool> visited(size_t(width) * height, false);
		const auto push = [&](const int x, const int y)
		{
			if (x < 0 || y < 0 || x >= width || y >= footerStart)
				return;
			const int index = y * width + x;
			if (visited[index] || !isBackground(pixels[index]))
				return;
			visited[index] = true;
			stack.push_back(index);
		};
		for (int x = 0; x < width; ++x)
		{
			push(x, 0);
			push(x, footerStart - 1);
		}
		for (int y = 0; y < footerStart; ++y)
		{
			push(0, y);
			push(width - 1, y);
		}
		while (!stack.empty())
		{
			const int index = stack.back();
			stack.pop_back();
			const int x = index % width, y = index / width;
			push(x + 1, y);
			push(x - 1, y);
			push(x, y + 1);
			push(x, y - 1);
		}
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
			{
				const int index = y * width + x;
				if (y >= footerStart || visited[index])
					pixels[index] = olc::Pixel(255, 255, 255, 0);
			}
		// Clean the cut-out edge: erode the opaque mask a few pixels so the
		// light background fringe goes, then give the transparent pixels the
		// colour of their nearest opaque neighbour so filtering can't blend in
		// white. The bottom of the picture fades out where the egg mesh stops.
		{
			constexpr int erode = 3;
			std::vector<uint8_t> alpha(size_t(width) * height);
			for (int i = 0; i < width * height; ++i)
				alpha[i] = pixels[i].a;
			std::vector<uint8_t> keep(alpha.size(), 1);
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x)
				{
					bool edge = alpha[y * width + x] == 0;
					for (int dy = -erode; dy <= erode && !edge; ++dy)
						for (int dx = -erode; dx <= erode && !edge; ++dx)
						{
							const int nx = x + dx, ny = y + dy;
							edge = nx < 0 || ny < 0 || nx >= width ||
								ny >= height || alpha[ny * width + nx] == 0;
						}
					keep[y * width + x] = edge ? 0 : 1;
				}
			for (int y = 0; y < height; ++y)
			{
				const float v = float(y) / float(height);
				const float fade = std::clamp((0.75f - v) / 0.09f, 0.0f, 1.0f);
				for (int x = 0; x < width; ++x)
				{
					olc::Pixel& p = pixels[y * width + x];
					if (!keep[y * width + x])
						p.a = 0;
					else
						p.a = uint8_t(p.a * fade);
				}
			}
			// Bleed opaque colours outward into transparent pixels.
			std::vector<uint8_t> coloured(size_t(width) * height);
			for (int i = 0; i < width * height; ++i)
				coloured[i] = pixels[i].a > 0 ? 1 : 0;
			for (int pass = 0; pass < 8; ++pass)
			{
				std::vector<olc::Pixel> next(pixels, pixels + size_t(width) * height);
				std::vector<uint8_t> nextColoured = coloured;
				for (int y = 1; y < height - 1; ++y)
					for (int x = 1; x < width - 1; ++x)
					{
						if (coloured[y * width + x])
							continue;
						for (const int offset : { -1, 1, -width, width })
						{
							if (coloured[y * width + x + offset])
							{
								const olc::Pixel& n = pixels[y * width + x + offset];
								next[y * width + x] = olc::Pixel(n.r, n.g, n.b, 0);
								nextColoured[y * width + x] = 1;
								break;
							}
						}
					}
				std::copy(next.begin(), next.end(), pixels);
				coloured.swap(nextColoured);
			}
		}
		playerTexture.BindCPU();

		playerTextureLoaded = true;
	}
	else
		std::cerr << "Unable to load player texture: assets/player.png\n";

	if (!CreateImageFromFile(courtTexture, "assets/court_blue.png"))
	{
		std::cerr << "Unable to load court image: assets/court_blue.png\n";
		return false;
	}

	olc::ImageConfig floorConfig;
	floorConfig.Filtered = true;
	floorTextureLoaded = CreateImageFromFile(
		floorTexture, "assets/cement_background.jpg", floorConfig);
	if (!floorTextureLoaded)
		std::cerr << "Unable to load floor image: assets/cement_background.jpg\n";

	olc::ImageConfig postConfig;
	postConfig.Filtered = true;
	if (!CreateImageFromFile(postTexture, "assets/post.jpg", postConfig))
		std::cerr << "Unable to load post image: assets/post.jpg\n";

	profiles = netball::LoadProfiles("assets/players.csv");
	olc::ImageConfig portraitConfig;
	portraitConfig.Filtered = true;
	portraitSheetLoaded = CreateImageFromFile(
		portraitSheet, "assets/portraits.png", portraitConfig);
	if (!portraitSheetLoaded)
		portraitSheetLoaded = CreateImageFromFile(
			portraitSheet, "assets/portraits.jpg", portraitConfig);
	if (!portraitSheetLoaded)
		portraitSheetLoaded = CreateImageFromFile(
			portraitSheet, "assets/characters.jpg", portraitConfig);
	if (!portraitSheetLoaded)
		std::cerr << "No portrait sheet (assets/portraits.png, .jpg, or characters.jpg); "
			"portraits are skipped\n";

	// One texture per player: skin and hair tones are taken from the portrait.
	if (playerTextureLoaded)
	{
		const olc::vi2d baseSize = playerBase.Size();
		const olc::Pixel* basePixels = playerBase.Data();
		const olc::Pixel* sheet = portraitSheetLoaded ? portraitSheet.Data() : nullptr;
		const olc::vi2d sheetSize = portraitSheetLoaded
			? portraitSheet.Size() : olc::vi2d{ 0, 0 };
		const auto sample = [&](const int cell, const float fx0, const float fy0,
			const float fx1, const float fy1, float (&rgb)[3])
		{
			const float cellWidth = float(sheetSize.x) / 10.0f;
			const float cellHeight = float(sheetSize.y) / 10.0f;
			const int x0 = int((cell % 10 + fx0) * cellWidth);
			const int x1 = std::max(x0 + 1, int((cell % 10 + fx1) * cellWidth));
			const int y0 = int((cell / 10 + fy0) * cellHeight);
			const int y1 = std::max(y0 + 1, int((cell / 10 + fy1) * cellHeight));
			float sum[3] = { 0, 0, 0 };
			int count = 0;
			for (int y = y0; y < y1 && y < sheetSize.y; ++y)
				for (int x = x0; x < x1 && x < sheetSize.x; ++x)
				{
					const olc::Pixel p = sheet[y * sheetSize.x + x];
					sum[0] += p.r; sum[1] += p.g; sum[2] += p.b;
					++count;
				}
			for (int c = 0; c < 3; ++c)
				rgb[c] = count > 0 ? sum[c] / float(count) : 0.0f;
		};
		const float defaultHair[3][3] = {
			{ 204.0f, 170.0f, 20.0f }, { 125.0f, 78.0f, 40.0f }, { 48.0f, 42.0f, 46.0f }
		};
		const float baseSkin[3] = { 250.0f, 199.0f, 144.0f };
		bool texturesOk = true;
		for (std::size_t slot = 0; slot < playerTextures.size() && texturesOk; ++slot)
		{
			float skin[3] = { baseSkin[0], baseSkin[1], baseSkin[2] };
			float hair[3] = { defaultHair[slot % 3][0], defaultHair[slot % 3][1],
				defaultHair[slot % 3][2] };
			if (sheet && slot < profiles.size())
			{
				const int cell = std::clamp(profiles[slot].portrait, 0, 99);
				float cheekLeft[3], cheekRight[3], forehead[3], crown[3];
				sample(cell, 0.34f, 0.50f, 0.44f, 0.58f, cheekLeft);
				sample(cell, 0.56f, 0.50f, 0.66f, 0.58f, cheekRight);
				sample(cell, 0.45f, 0.36f, 0.55f, 0.42f, forehead);
				sample(cell, 0.40f, 0.04f, 0.60f, 0.12f, crown);
				for (int c = 0; c < 3; ++c)
					skin[c] = (cheekLeft[c] + cheekRight[c] + forehead[c]) / 3.0f;
				// Hair is warm or dark (r >= g >= b); anything else is background.
				if (crown[0] >= crown[1] - 3.0f && crown[1] >= crown[2] - 3.0f &&
					crown[0] < 235.0f)
					for (int c = 0; c < 3; ++c)
						hair[c] = crown[c];
			}
			playerHair[slot] = olc::Pixel(uint8_t(hair[0]), uint8_t(hair[1]),
				uint8_t(hair[2]));
			texturesOk = CreateImage(playerTextures[slot], baseSize, playerConfig);
			if (!texturesOk)
				break;
			olc::Pixel* destination = playerTextures[slot].Data();
			for (int i = 0; i < baseSize.x * baseSize.y; ++i)
			{
				olc::Pixel p = basePixels[i];
				const int hi = std::max({ int(p.r), int(p.g), int(p.b) });
				const int lo = std::min({ int(p.r), int(p.g), int(p.b) });
				const float saturation = hi > 0 ? float(hi - lo) / float(hi) : 0.0f;
				float hue = 0.0f;
				if (hi != lo)
				{
					if (hi == p.r)
						hue = 60.0f * (float(p.g) - float(p.b)) / float(hi - lo);
					else if (hi == p.g)
						hue = 120.0f + 60.0f * (float(p.b) - float(p.r)) / float(hi - lo);
					else
						hue = 240.0f + 60.0f * (float(p.r) - float(p.g)) / float(hi - lo);
					if (hue < 0.0f)
						hue += 360.0f;
				}
				if (p.a > 0 && saturation > 0.6f && hue >= 35.0f && hue <= 65.0f)
				{
					const float brightness =
						(0.299f * p.r + 0.587f * p.g + 0.114f * p.b) / 204.0f;
					p = olc::Pixel(
						uint8_t(std::clamp(hair[0] * brightness, 0.0f, 255.0f)),
						uint8_t(std::clamp(hair[1] * brightness, 0.0f, 255.0f)),
						uint8_t(std::clamp(hair[2] * brightness, 0.0f, 255.0f)), p.a);
				}
				else if (p.a > 0 && p.r >= 190 && saturation >= 0.22f &&
					saturation <= 0.5f && hue >= 15.0f && hue <= 35.0f)
				{
					// Skin: scale by the ratio to the original tone, keeping shading.
					p = olc::Pixel(
						uint8_t(std::clamp(p.r * skin[0] / baseSkin[0], 0.0f, 255.0f)),
						uint8_t(std::clamp(p.g * skin[1] / baseSkin[1], 0.0f, 255.0f)),
						uint8_t(std::clamp(p.b * skin[2] / baseSkin[2], 0.0f, 255.0f)),
						p.a);
				}
				destination[i] = p;
			}
			playerTextures[slot].BindCPU();
		}
		playerTextureLoaded = texturesOk;

		// Recolour each portrait's magenta jersey to its team's egg colour.
		if (portraitSheetLoaded)
		{
			olc::Pixel* sheetPixels = portraitSheet.Data();
			for (std::size_t slot = 0; slot < profiles.size() && slot < 18; ++slot)
			{
				const bool home = slot < 7 || (slot >= 14 && slot < 16);
				const olc::Pixel team = home ? netball::HomeTeamColour
					: netball::AwayTeamColour;
				const int cell = std::clamp(profiles[slot].portrait, 0, 99);
				const int x0 = cell % 10 * sheetSize.x / 10;
				const int x1 = (cell % 10 + 1) * sheetSize.x / 10;
				const int y0 = cell / 10 * sheetSize.y / 10;
				const int y1 = (cell / 10 + 1) * sheetSize.y / 10;
				for (int y = y0; y < y1; ++y)
					for (int x = x0; x < x1; ++x)
					{
						olc::Pixel& p = sheetPixels[y * sheetSize.x + x];
						if (p.r > 110 && p.b > 90 && p.g < 0.6f * p.r &&
							p.g < 0.7f * p.b && std::abs(int(p.r) - int(p.b)) < 90)
						{
							const float shade = (0.299f * p.r + 0.587f * p.g +
								0.114f * p.b) / 150.0f;
							p.r = uint8_t(std::clamp(team.r * shade, 0.0f, 255.0f));
							p.g = uint8_t(std::clamp(team.g * shade, 0.0f, 255.0f));
							p.b = uint8_t(std::clamp(team.b * shade, 0.0f, 255.0f));
						}
					}
			}
			portraitSheet.BindCPU();
		}
	}

	projection.perspective(25.0f * Pi / 180.0f,
		float(ScreenSize().x) / float(ScreenSize().y), 0.1f, 150.0f);
	uiScale = float(ScreenSize().y) / 1600.0f;
	draw.SetProjectionMatrix(projection);
	players = netball::CreateTeams();
	for (std::size_t i = 0; i < players.size(); ++i)
		startingPositions[i] = players[i].position;
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (players[i].homeTeam && players[i].role == 3)
		{
			ballCarrier = i;
			break;
		}
	}
	selectedPlayer = ballCarrier;
	RefreshPassTargets();
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		const bool isZXOption = i == passTargets[0] || i == passTargets[1];
		const bool isAttacker = players[i].homeTeam == players[ballCarrier].homeTeam &&
			players[i].role <= 1;
		if (isAttacker && !isZXOption)
			ChooseRandomDirection(i, 0.5f, 1.0f);
		else
			ChooseRandomDirection(i);
	}
	ballPosition = players[ballCarrier].position;
	ballPosition.x += players[ballCarrier].homeTeam
		? BallSideOffset : -BallSideOffset;
	ballPosition.y = CarriedBallHeight;
	LogEvent("SESSION_START possession=HOME role=C");
	RecordPlayerPositions();
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		previousFramePositions[i] = players[i].position;
		attackerProgressPositions[i] = players[i].position;
	}
	return true;
}

bool NetballGame::OnUserUpdate(const float elapsedTime)
{
	static bool windowPlaced = false;
	if (!windowPlaced)
	{
		windowPlaced = true;
		const bool onDisplay3 = netball::FillMonitor(3);
		const bool filled = onDisplay3 || netball::FillMonitor(1);
		LogEvent(onDisplay3 ? "WINDOW fullscreen display=3"
			: filled ? "WINDOW display 3 not found, fullscreen display=1"
			: "WINDOW no display found");
	}

	pad = netball::PollGamepad();
	// A connected pad is the default input until a key is pressed.
	if (pad.connected && !padSeen)
	{
		padSeen = true;
		usingGamepad = true;
	}
	if (pad.anyActivity)
		usingGamepad = true;
	else if (usingGamepad)
	{
		for (const olc::Key key : { olc::Key::A, olc::Key::D, olc::Key::W,
			olc::Key::S, olc::Key::LEFT, olc::Key::RIGHT, olc::Key::UP,
			olc::Key::DOWN, olc::Key::Z, olc::Key::X, olc::Key::C, olc::Key::V,
			olc::Key::SPACE, olc::Key::TAB })
			if (keyboard.GetKey(key).bPressed)
				usingGamepad = false;
	}
	const bool confirmPressed = keyboard.GetKey(olc::Key::SPACE).bPressed ||
		pad.startPressed || pad.passPressed[0] || pad.shootPressed;

	if (keyboard.GetKey(olc::Key::F2).bPressed)
	{
		consolePlatform = netball::ButtonPlatform(
			1 + (int(consolePlatform)) % 3);
		LogEvent("CONSOLE_STYLE " + std::to_string(int(consolePlatform)));
	}

	if (keyboard.GetKey(olc::Key::ESCAPE).bPressed ||
		keyboard.GetKey(olc::Key::Q).bPressed || pad.backPressed)
		return false;

	if (splashActive)
	{
		if (confirmPressed)
		{
			splashActive = false;
			demoActive = true;
			aiAttackMode = true;
			clockRemaining = MatchClockSeconds;
			homeScore = awayScore = 0;
			LogEvent("AI_MODE_CHANGE home_ai=ON reason=GAME_START");
			return true;
		}
		draw.Clear(olc::Colour::BLACK);
		const olc::vf2d screen{ float(ScreenSize().x), float(ScreenSize().y) };
		const olc::vf2d imageSize{
			float(splashImage.Size().x), float(splashImage.Size().y) };
		const float scale = std::min(
			screen.x / imageSize.x, screen.y / imageSize.y);
		const olc::vf2d size = imageSize * scale;
		draw.ImageRect(splashImage, (screen - size) * 0.5f, size);
		DrawConfirmPrompt("to start");
		return true;
	}

	if (demoActive && confirmPressed)
	{
		StartMatch();
		return true;
	}

	++frameNumber;
	if (frameNumber % 30 == 0)
		LogPlayerPositions();

	camera.Update(elapsedTime, ballPosition.z);

	if (keyboard.GetKey(olc::Key::K).bPressed)
		debugMode = !debugMode;

	if (keyboard.GetKey(olc::Key::P).bPressed ||
		(pad.startPressed && !paused))
	{
		paused = !paused;
		pauseSelection = 0;
		// Skip the menu this frame so the opening Start press is not reused.
		if (paused)
		{
			DrawScene();
			return true;
		}
	}

	if (paused)
	{
		if (!UpdatePauseMenu())
			return false;
		DrawScene();
		return true;
	}
	previousPadMove = {};

	UpdateGoalParticles(elapsedTime);

	if (throwInPending)
	{
		throwInPauseRemaining = std::max(
			0.0f, throwInPauseRemaining - elapsedTime);
		if (throwInPauseRemaining <= 0.0f)
			CompleteThrowIn();
		else
		{
			DrawScene();
			return true;
		}
	}

	if (shotResetPending)
	{
		shotResetDelayRemaining = std::max(
			0.0f, shotResetDelayRemaining - elapsedTime);
		if (shotResetDelayRemaining <= 0.0f)
			ResetAfterShot();
		else
		{
			DrawScene();
			return true;
		}
	}

	if (restartHoldRemaining > 0.0f)
	{
		restartHoldRemaining = std::max(0.0f, restartHoldRemaining - elapsedTime);
		DrawScene();
		return true;
	}

	if (gameOver)
	{
		const bool confirmHeld = keyboard.GetKey(olc::Key::SPACE).bHeld ||
			(usingGamepad && pad.confirmHeld);
		gameOverHold = confirmHeld ? gameOverHold + elapsedTime : 0.0f;
		DrawScene();
		DrawGameOver();
		if (gameOverHold >= GameOverHoldSeconds)
		{
			gameOverHold = 0.0f;
			StartMatch();
		}
		return true;
	}

	gameTime += elapsedTime;
	clockRemaining -= elapsedTime *
		(clockRemaining <= FinalSeconds ? 1.0f : ClockSpeed);
	if (clockRemaining <= 0.0f)
	{
		clockRemaining = 0.0f;
		LogEvent("GAME_OVER score=" + std::to_string(homeScore) + "-" +
			std::to_string(awayScore) + (demoActive ? " demo=1" : ""));
		if (demoActive)
		{
			// The demo plays on: reset score, clock and positions.
			StartMatch();
			demoActive = true;
			aiAttackMode = true;
			movementMessage.clear();
			messageTime = 0.0f;
			LogEvent("DEMO_RESTART");
			return true;
		}
		gameOver = true;
	}
	if (passInFlight || shotInFlight || looseBall)
		ballSpinAngle = std::fmod(ballSpinAngle + 3.0f * elapsedTime, 2.0f * Pi);
	UpdateAttackerStuckTimers(elapsedTime);
	UpdatePlayers(elapsedTime);
	TryPickupLooseBall();
	{
		const float jumpDx = ballPosition.x - previousBallPosition.x;
		const float jumpDy = ballPosition.y - previousBallPosition.y;
		const float jumpDz = ballPosition.z - previousBallPosition.z;
		const float jump = std::sqrt(
			jumpDx * jumpDx + jumpDy * jumpDy + jumpDz * jumpDz);
		if (jump > 0.6f && frameNumber > 2 && !shotInFlight)
		{
			std::ostringstream event;
			event << std::fixed << std::setprecision(3)
				<< "BALL_JUMP distance=" << jump << " dt=" << elapsedTime
				<< " from=(" << previousBallPosition.x << ","
				<< previousBallPosition.y << "," << previousBallPosition.z
				<< ") to=(" << ballPosition.x << "," << ballPosition.y << ","
				<< ballPosition.z << ") state="
				<< (passInFlight ? (shotInFlight ? "SHOT" : "PASS")
					: looseBall ? "LOOSE" : "CARRIED")
				<< " carrier="
				<< netball::Positions[players[ballCarrier].role].abbreviation
				<< " carrier_pos=(" << players[ballCarrier].position.x << ","
				<< players[ballCarrier].position.z << ") blend_remaining="
				<< catchBlendRemaining << " throw_in=" << throwInPending;
			LogEvent(event.str());
		}
		if (passInFlight && !shotInFlight)
		{
			passLogTimer += elapsedTime;
			if (passLogTimer >= 0.1f)
			{
				passLogTimer = 0.0f;
				std::ostringstream event;
				event << std::fixed << std::setprecision(3)
					<< "PASS_FLIGHT t=" << passElapsed << "/" << passDuration
					<< " ball=(" << ballPosition.x << "," << ballPosition.z
					<< ") end=(" << passEnd.x << "," << passEnd.z
					<< ") receiver_pos=("
					<< players[passReceiver].position.x << ","
					<< players[passReceiver].position.z
					<< ") destination=(" << passDestination.x << ","
					<< passDestination.y << ") selected="
					<< netball::Positions[players[selectedPlayer].role].abbreviation;
				LogEvent(event.str());
			}
		}
		previousBallPosition = ballPosition;
	}
	for (std::size_t i = 0; i < players.size(); ++i)
		previousFramePositions[i] = players[i].position;
	RecordPlayerPositions();
	DrawScene();
	if (demoActive)
		DrawConfirmPrompt("to play");
	return true;
}

netball::ButtonPlatform NetballGame::ActivePlatform() const
{
	return usingGamepad ? consolePlatform : netball::ButtonPlatform::PC;
}

void NetballGame::DrawConfirmPrompt(const std::string& action,
	const float holdProgress)
{
	const olc::vf2d promptScale{ 6.0f, 6.0f };
	olc::Image* icon = usingGamepad ? netball::ButtonIcon(consolePlatform, 0) : nullptr;
	const std::string prefix = holdProgress >= 0.0f ? "HOLD " : "";
	const std::string prefixText = icon ? prefix : prefix + "SPACE ";
	const std::string text = action;
	const float prefixWidth = prefixText.empty()
		? 0.0f : UiTextSize(prefixText, promptScale).x;
	const olc::vf2d textSize = UiTextSize(text, promptScale);
	const float iconSize = icon ? textSize.y * 1.3f : 0.0f;
	const float gap = icon ? 16.0f : 0.0f;
	const olc::vf2d totalSize{ prefixWidth + iconSize + gap + textSize.x,
		std::max(textSize.y, iconSize) };
	const olc::vf2d uiScreen = UiScreenSize();
	const olc::vf2d origin{
		(uiScreen.x - totalSize.x) * 0.5f, uiScreen.y - totalSize.y - 80.0f };
	UiFilledRect(origin - olc::vf2d{ 20.0f, 12.0f },
		totalSize + olc::vf2d{ 40.0f, 24.0f }, olc::Pixel(0, 0, 0, 170));
	if (!prefixText.empty())
		UiString({ origin.x, origin.y + (totalSize.y - textSize.y) * 0.5f },
			prefixText, olc::Colour::WHITE, promptScale);
	if (icon)
	{
		draw.SetBlendMode(olc::BlendMode::Alpha);
		draw.ImageRect(*icon, olc::vf2d{ origin.x + prefixWidth, origin.y } * uiScale,
			olc::vf2d{ iconSize, iconSize } * uiScale);
	}
	UiString({ origin.x + prefixWidth + iconSize + gap,
		origin.y + (totalSize.y - textSize.y) * 0.5f },
		text, olc::Colour::WHITE, promptScale);
	if (holdProgress >= 0.0f)
	{
		const float barY = origin.y + totalSize.y + 14.0f;
		UiFilledRect({ origin.x - 20.0f, barY }, { totalSize.x + 40.0f, 10.0f },
			olc::Pixel(255, 255, 255, 60));
		UiFilledRect({ origin.x - 20.0f, barY },
			{ (totalSize.x + 40.0f) * std::clamp(holdProgress, 0.0f, 1.0f), 10.0f },
			olc::Pixel(255, 215, 55));
	}
}

void NetballGame::StartMatch()
{
	demoActive = false;
	// Force a home restart: ResetAfterShot gives possession to the other team.
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (!players[i].homeTeam)
		{
			ballCarrier = i;
			break;
		}
	}
	passInFlight = false;
	shotInFlight = false;
	looseBall = false;
	throwInPending = false;
	shootingMeterActive = false;
	paused = false;
	catchBlendRemaining = 0.0f;
	aiAttackActionDelay = 0.0f;
	goalParticles.clear();
	homeScore = awayScore = 0;
	gameTime = 0.0f;
	clockRemaining = MatchClockSeconds;
	superStreamRemaining = 0.0f;
	shotIsSuper = false;
	gameOver = false;
	aiAttackMode = false;
	ResetAfterShot();
	homeTurnovers = awayTurnovers = 0;
	homeShots = awayShots = 0;
	homeMissedShots = awayMissedShots = 0;
	previousBallPosition = ballPosition;
	camera.Snap(ballPosition.z);
	movementMessage = "YOU CONTROL HOME";
	messageTime = 2.5f;
	LogEvent("MATCH_START score=0-0 home_ai=OFF");
}

void NetballGame::LogEvent(const std::string& event)
{
	if (!debugLog)
	{
		if (!debugLogErrorReported)
		{
			std::cerr << "Unable to write to netball_debug_log.txt\n";
			debugLogErrorReported = true;
		}
		return;
	}

	debugLog << std::fixed << std::setprecision(3)
		<< "frame=" << frameNumber << " time=" << gameTime
		<< " " << event << '\n';
	debugLog.flush();
	if (!debugLog && !debugLogErrorReported)
	{
		std::cerr << "Unable to write to netball_debug_log.txt\n";
		debugLogErrorReported = true;
	}
}

void NetballGame::LogPlayerPositions()
{
	std::ostringstream event;
	event << std::fixed << std::setprecision(2)
		<< "STATE_SNAPSHOT"
		<< " possession=" << (players[ballCarrier].homeTeam ? "HOME" : "AWAY")
		<< ":" << netball::Positions[players[ballCarrier].role].abbreviation
		<< " selected=" << (players[selectedPlayer].homeTeam ? "HOME" : "AWAY")
		<< ":" << netball::Positions[players[selectedPlayer].role].abbreviation
		<< " ball=(" << ballPosition.x << "," << ballPosition.y << ","
		<< ballPosition.z << ")"
		<< " ball_velocity=("
		<< (looseBall ? looseBallHorizontalVelocity.x :
			passInFlight ? (passEnd.x - passStart.x) /
				std::max(passDuration, 0.001f) : 0.0f)
		<< "," << (looseBall ? looseBallVerticalVelocity : 0.0f) << ","
		<< (looseBall ? looseBallHorizontalVelocity.y :
			passInFlight ? (passEnd.z - passStart.z) /
				std::max(passDuration, 0.001f) : 0.0f)
		<< ")"
		<< " phase=" << (shotInFlight ? (shotInNet ? "SHOT_IN_NET" : "SHOT")
			: passInFlight ? "PASS"
			: looseBall ? "LOOSE_BALL" : "CARRIED")
		<< " score=" << homeScore << "-" << awayScore
		<< " turnovers=" << homeTurnovers << "-" << awayTurnovers
		<< " shots=" << homeShots << "/" << homeMissedShots
		<< "-" << awayShots << "/" << awayMissedShots
		<< " home_ai=" << (aiAttackMode ? "ON" : "OFF")
		<< " ai_attacking=" << (IsAIAttacking() ? "YES" : "NO")
		<< " chasers=HOME:"
		<< (looseBallChasers[0] < players.size()
			? netball::Positions[players[looseBallChasers[0]].role].abbreviation
			: "NONE")
		<< ",AWAY:"
		<< (looseBallChasers[1] < players.size()
			? netball::Positions[players[looseBallChasers[1]].role].abbreviation
			: "NONE")
		<< " PLAYER_POSITIONS";
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		const netball::Player& player = players[i];
		event << " " << (player.homeTeam ? "HOME" : "AWAY")
			<< ":" << netball::Positions[player.role].abbreviation
			<< "=(" << player.position.x << "," << player.position.y
			<< "," << player.position.z << ")"
			<< "{stuck=" << stationaryDurations[i]
			<< ",clip=" << defenderClipDurations[i] << "}";
	}
	LogEvent(event.str());
}

void NetballGame::RegisterTurnover(const bool losingTeamHome,
	const std::string& reason)
{
	if (losingTeamHome)
		++homeTurnovers;
	else
		++awayTurnovers;
	LogEvent("TURNOVER team=" +
		std::string(losingTeamHome ? "HOME" : "AWAY") +
		" reason=" + reason +
		" totals=" + std::to_string(homeTurnovers) + "-" +
		std::to_string(awayTurnovers));
}

void NetballGame::SetBallCarrier(const std::size_t playerIndex,
	const std::string& reason, const bool countTurnover)
{
	if (playerIndex >= players.size())
		return;

	const bool previousTeamHome = players[ballCarrier].homeTeam;
	const bool nextTeamHome = players[playerIndex].homeTeam;
	if (countTurnover && previousTeamHome != nextTeamHome)
		RegisterTurnover(previousTeamHome, reason);

	const bool looseRecovery = reason == "LOOSE_BALL_RECOVERED" ||
		reason == "REBOUND_WON";
	if (looseRecovery && reboundPending)
	{
		++profiles[playerIndex].rebounds;
		LogEvent("STAT rebound player=" + profiles[playerIndex].name);
	}
	if (looseRecovery && dropPending && nextTeamHome != dropTeamHome)
	{
		++profiles[playerIndex].turnovers;
		LogEvent("STAT turnover_won player=" + profiles[playerIndex].name);
	}
	reboundPending = false;
	dropPending = false;
	reboundMarkerActive = false;
	if (reason != "PASS_CAUGHT")
	{
		lastReceiver = netball::PlayerCount;
		assistChain.fill(netball::PlayerCount);
	}
	defenderLockActive = false;
	lockedDefenders.clear();
	ballCarrier = playerIndex;
	catchBlendRemaining = 0.0f;
	aiShooterPauseFor = static_cast<std::size_t>(-1);
	aiShooterPauseRemaining = 0.0f;
	if (previousTeamHome != nextTeamHome)
		LogEvent("POSSESSION_CHANGE from=" +
			std::string(previousTeamHome ? "HOME" : "AWAY") +
			" to=" + (nextTeamHome ? "HOME" : "AWAY") +
			" reason=" + reason);
}

void NetballGame::LogMovementBlock(const std::size_t playerIndex,
	const std::string& reason, const olc::vf2d& displacement)
{
	if (playerIndex >= players.size())
		return;

	if (reason == lastMovementBlockReasons[playerIndex] &&
		frameNumber - lastMovementBlockFrames[playerIndex] < 150)
		return;

	const netball::Player& player = players[playerIndex];
	std::ostringstream event;
	event << std::fixed << std::setprecision(2)
		<< "MOVEMENT_BLOCKED team=" << (player.homeTeam ? "HOME" : "AWAY")
		<< " player=" << netball::Positions[player.role].abbreviation
		<< " reason=" << reason
		<< " at=(" << player.position.x << "," << player.position.z << ")"
		<< " requested=(" << displacement.x << "," << displacement.y << ")"
		<< " selected=" << (selectedPlayer == playerIndex ? "yes" : "no")
		<< " carrier=" << (ballCarrier == playerIndex ? "yes" : "no");
	LogEvent(event.str());
	lastMovementBlockReasons[playerIndex] = reason;
	lastMovementBlockFrames[playerIndex] = frameNumber;
}







void NetballGame::UpdatePlayers(const float elapsedTime)
{
	if (throwInPending)
		return;

	if (!demoActive && keyboard.GetKey(olc::Key::O).bPressed)
	{
		aiAttackMode = !aiAttackMode;
		aiAttackActionDelay = 0.0f;
		aiNoReceiverLogged = false;
		playerVelocity = {};
		LogEvent(std::string("AI_MODE_CHANGE home_ai=") +
			(aiAttackMode ? "ON" : "OFF") +
			" away_ai=ON possession_team=" +
			(players[ballCarrier].homeTeam ? "HOME" : "AWAY") +
			" ai_attacking=" + (IsAIAttacking() ? "YES" : "NO"));
		movementMessage = aiAttackMode
			? "HOME AI ATTACK ON" : "HOME AI ATTACK OFF";
		messageTime = 1.5f;
	}
	aiAttackActionDelay = std::max(0.0f, aiAttackActionDelay - elapsedTime);

	const bool wasShotInFlight = shotInFlight;
	UpdatePassing(elapsedTime);
	if (wasShotInFlight && !shotInFlight)
		return;
	UpdateShootingMeter(elapsedTime);

	if (!passInFlight && !shootingMeterActive &&
		players[ballCarrier].homeTeam && !aiAttackMode &&
		(keyboard.GetKey(olc::Key::TAB).bPressed || pad.cyclePressed))
	{
		const std::size_t optionCount = passTargets.size();
		for (std::size_t offset = 1; offset <= optionCount; ++offset)
		{
			const std::size_t option = controlledPassOption < 0
				? offset - 1 : (std::size_t(controlledPassOption) + offset) % optionCount;
			const std::size_t target = passTargets[option];
			if (target >= netball::PlayerCount)
				continue;

			controlledPassOption = int(option);
			selectedPlayer = target;
			playerVelocity = {};
			break;
		}
	}

	UpdateTeammateMovement(elapsedTime);
	UpdateAIAttack(elapsedTime);

	if (!IsAIAttacking() && !passInFlight && !looseBall &&
		(keyboard.GetKey(olc::Key::SPACE).bPressed || pad.shootPressed))
	{
		if (shootingMeterActive)
		{
			if (!StartShot())
			{
				movementMessage = "CANNOT SHOOT FROM THIS POSITION";
				messageTime = 1.2f;
			}
		}
		else if (CanShoot())
		{
			shootingMeterActive = true;
			shootingMeterPosition = GetShootingMeterStartPosition();
			shootingMeterElapsed = shootingMeterPosition;
			shootingMeterSpeed = GetShootingMeterSpeed();
			shootingMeterDisplayTime = 0.0f;
			shootingMeterGuaranteedShot = false;
			messageTime = 0.0f;
		}
		else
		{
			movementMessage = players[ballCarrier].role > 1
				? "ONLY GS/GA CAN SHOOT"
				: "SHOOTING REQUIRES SHOOTING CIRCLE";
			messageTime = 1.2f;
		}
	}
	else if (!IsAIAttacking() && !passInFlight && !looseBall &&
		!shootingMeterActive)
	{
		const std::array<bool, 4> passKeys{
			keyboard.GetKey(olc::Key::Z).bPressed || pad.passPressed[0],
			keyboard.GetKey(olc::Key::X).bPressed || pad.passPressed[1],
			keyboard.GetKey(olc::Key::C).bPressed || pad.passPressed[2],
			keyboard.GetKey(olc::Key::V).bPressed || pad.passPressed[3]
		};
		if (controlledPassOption >= 0 && passKeys[0])
			StartPassToPlayer(selectedPlayer);
		else if (controlledPassOption >= 0 && passKeys[1])
		{
			const std::size_t optionCount = passTargets.size();
			for (std::size_t offset = 1; offset <= optionCount; ++offset)
			{
				const std::size_t option =
					(std::size_t(controlledPassOption) + offset) % optionCount;
				if (passTargets[option] >= netball::PlayerCount)
					continue;
				StartPassToPlayer(passTargets[option]);
				break;
			}
		}
		else
		{
			for (std::size_t option = 0; option < passKeys.size(); ++option)
			{
				if (!passKeys[option])
					continue;
				// With a receiver being controlled, Z and X are handled above.
				if (controlledPassOption >= 0 && option < 2)
					continue;
				StartPassOption(option);
				break;
			}
		}
	}

	olc::vf4d movement{ 0.0f, 0.0f, 0.0f };
	if (!aiAttackMode)
	{
		movement.z += pad.move.x;
		movement.x -= pad.move.y;
		if ((!keyboard.GetKey(olc::Key::A).bPressed &&
			keyboard.GetKey(olc::Key::A).bHeld) ||
			keyboard.GetKey(olc::Key::LEFT).bHeld)
			movement.z -= 1.0f;
		if (keyboard.GetKey(olc::Key::D).bHeld ||
			keyboard.GetKey(olc::Key::RIGHT).bHeld)
			movement.z += 1.0f;
		if (keyboard.GetKey(olc::Key::W).bHeld)
			movement.x += 1.0f;
		if (keyboard.GetKey(olc::Key::S).bHeld)
			movement.x -= 1.0f;
		if (keyboard.GetKey(olc::Key::UP).bHeld)
			movement.x -= 1.0f;
		if (keyboard.GetKey(olc::Key::DOWN).bHeld)
			movement.x += 1.0f;
	}

	const bool hasInput = movement.x != 0.0f || movement.z != 0.0f;
	if (shotInFlight || shootingMeterActive)
	{
		playerVelocity = {};
		messageTime = std::max(0.0f, messageTime - elapsedTime);
		return;
	}

	if (selectedPlayer == ballCarrier && !looseBall)
	{
		playerVelocity = {};
		if (hasInput)
		{
			LogMovementBlock(selectedPlayer, "BALL_CARRIER_RESTRICTION",
				{ movement.x, movement.z });
			movementMessage = "BALL CARRIER CANNOT MOVE";
			messageTime = 0.8f;
		}
		else
		{
			messageTime = std::max(0.0f, messageTime - elapsedTime);
		}
		return;
	}

	if (movement.x != 0.0f || movement.z != 0.0f)
	{
		const float magnitude = std::sqrt(movement.x * movement.x + movement.z * movement.z);
		movement.x /= magnitude;
		movement.z /= magnitude;
	}

	const olc::vf2d desiredVelocity{
		movement.x * MaxPlayerSpeed,
		movement.z * MaxPlayerSpeed
	};
	playerVelocity = MoveTowards(playerVelocity, desiredVelocity,
		(hasInput ? PlayerAcceleration : PlayerDeceleration) * elapsedTime);

	if (playerVelocity.mag2() < 0.0001f)
	{
		playerVelocity = {};
		messageTime = std::max(0.0f, messageTime - elapsedTime);
		return;
	}

	const float distanceX = playerVelocity.x * elapsedTime;
	const float distanceZ = playerVelocity.y * elapsedTime;
	const bool constrainSelectedShooter = false;
	if (!MovePlayer(selectedPlayer, { distanceX, distanceZ }, true,
		constrainSelectedShooter))
		playerVelocity = {};
	else
		messageTime = std::max(0.0f, messageTime - elapsedTime);
}











bool NetballGame::MovePlayer(const std::size_t playerIndex,
	const olc::vf2d& displacement, const bool showBlockedMessage,
	const bool constrainToShootingCircle)
{
	netball::Player& player = players[playerIndex];
	const auto isAllowed = [&](const olc::vf4d& position)
	{
		return netball::IsLegalPosition(player, position) &&
			IsWithinPassTargetRadius(playerIndex, position) &&
			(!constrainToShootingCircle ||
				netball::IsMovingIntoGoalCircle(
					player.position, position, player.homeTeam));
	};
	const float distance = displacement.mag();
	const int steps = std::max(1, static_cast<int>(std::ceil(distance / 0.2f)));
	const float stepX = displacement.x / float(steps);
	const float stepZ = displacement.y / float(steps);
	bool hitZone = false;
	bool hitPassRadius = false;
	bool hitShootingCircle = false;
	bool hitPlayer = false;
	const auto recordPositionBlock = [&](const olc::vf4d& position)
	{
		if (!netball::IsLegalPosition(player, position))
		{
			hitZone = true;
			return;
		}
		if (!IsWithinPassTargetRadius(playerIndex, position))
		{
			hitPassRadius = true;
			return;
		}
		if (constrainToShootingCircle &&
			!netball::IsMovingIntoGoalCircle(
				player.position, position, player.homeTeam))
			hitShootingCircle = true;
	};

	for (int step = 0; step < steps; ++step)
	{
		const olc::vf4d destination{
			player.position.x + stepX, player.position.y, player.position.z + stepZ
		};
		if (isAllowed(destination) &&
			!netball::OverlapsAnotherPlayer(players, playerIndex, destination,
				players[ballCarrier].homeTeam,
				defenderClipDurations[playerIndex] > 0.0f))
		{
			player.position = destination;
			continue;
		}

		bool moved = false;
		const auto tryMove = [&](const float dx, const float dz)
		{
			const olc::vf4d candidate{
				player.position.x + dx, player.position.y, player.position.z + dz
			};
			if (!isAllowed(candidate))
			{
				recordPositionBlock(candidate);
				return;
			}
			if (netball::OverlapsAnotherPlayer(players, playerIndex, candidate,
				players[ballCarrier].homeTeam,
				defenderClipDurations[playerIndex] > 0.0f))
			{
				hitPlayer = true;
				return;
			}
			player.position = candidate;
			moved = true;
		};

		if (std::abs(stepX) > std::abs(stepZ))
		{
			tryMove(stepX, 0.0f);
			tryMove(0.0f, stepZ);
		}
		else
		{
			tryMove(0.0f, stepZ);
			tryMove(stepX, 0.0f);
		}

		if (!moved)
		{
			if (showBlockedMessage)
			{
				const std::string reason = hitPlayer
					? "PLAYER_COLLISION"
					: hitZone
					? "POSITION_ZONE"
					: hitPassRadius
					? "PASS_TARGET_RADIUS"
					: hitShootingCircle
					? "SHOOTING_CIRCLE"
					: "MOVEMENT_RESTRICTED";
				LogMovementBlock(playerIndex, reason, displacement);
				movementMessage = hitPlayer
					? "BLOCKED - players cannot overlap"
					: hitZone
					? "OFFSIDE - this position is outside the selected bib's area"
					: "BLOCKED - movement is restricted";
				messageTime = 1.0f;
			}
			return false;
		}
	}

	return true;
}

void NetballGame::StartPassOption(const std::size_t option)
{
	StartPassToPlayer(option < passTargets.size()
		? passTargets[option] : netball::PlayerCount);
}

void NetballGame::StartPassToPlayer(const std::size_t playerIndex)
{
	passReceiver = playerIndex;
	if (passReceiver >= netball::PlayerCount)
	{
		LogEvent("PASS_DECISION team=" +
			std::string(players[ballCarrier].homeTeam ? "HOME" : "AWAY") +
			" passer=" + netball::Positions[players[ballCarrier].role].abbreviation +
			" receiver=NONE result=NO_ELIGIBLE_TARGET");
		return;
	}

	const bool aiDecision = IsAIAttacking();
	{
		std::ostringstream event;
		event << std::fixed << std::setprecision(2)
			<< "PASS_DECISION source=" << (aiDecision ? "AI" : "PLAYER")
			<< " team=" << (players[ballCarrier].homeTeam ? "HOME" : "AWAY")
			<< " passer=" << netball::Positions[players[ballCarrier].role].abbreviation
			<< " receiver=" << netball::Positions[players[passReceiver].role].abbreviation
			<< " from=(" << players[ballCarrier].position.x << ","
			<< players[ballCarrier].position.z << ") to=("
			<< players[passReceiver].position.x << ","
			<< players[passReceiver].position.z << ")";
		LogEvent(event.str());
	}
	passStart = players[ballCarrier].position;
	passStart.x += players[ballCarrier].homeTeam
		? BallSideOffset : -BallSideOffset;
	passStart.y = CarriedBallHeight;
	passEnd = players[passReceiver].position;
	passEnd.x += players[passReceiver].homeTeam
		? BallSideOffset : -BallSideOffset;
	passEnd.y = CarriedBallHeight;
	passDestination = { players[passReceiver].position.x,
		players[passReceiver].position.z };
	if (!aiDecision && players[passReceiver].homeTeam)
	{
		selectedPlayer = passReceiver;
		controlledPassOption = -1;
	}
	else if (!players[ballCarrier].homeTeam && !aiAttackMode)
	{
		// Defending: hand control to the home player nearest the landing spot.
		float nearest = std::numeric_limits<float>::max();
		for (std::size_t i = 0; i < players.size(); ++i)
		{
			if (!players[i].homeTeam)
				continue;
			const float dx = players[i].position.x - passDestination.x;
			const float dz = players[i].position.z - passDestination.y;
			const float distanceSquared = dx * dx + dz * dz;
			if (distanceSquared < nearest)
			{
				nearest = distanceSquared;
				selectedPlayer = i;
			}
		}
		playerVelocity = {};
	}
	passElapsed = 0.0f;
	const float dx = passEnd.x - passStart.x;
	const float dz = passEnd.z - passStart.z;
	passDuration = std::max(0.35f, std::sqrt(dx * dx + dz * dz) / 9.0f);
	shotInFlight = false;
	passInFlight = true;
	throwDoubleTeamDrop = ComputeDoubleTeam(throwDoubleTeamCount);
	catchBlendRemaining = 0.0f;
	aiNoReceiverLogged = false;
	playerVelocity = {};
	messageTime = 0.0f;
}

bool NetballGame::CanShoot() const
{
	const netball::Player& shooter = players[ballCarrier];
	return shooter.role <= 1 &&
		netball::IsInsideGoalCircle(shooter.position, shooter.homeTeam);
}

bool NetballGame::StartShot()
{
	if (!CanShoot())
	{
		shootingMeterActive = false;
		aiShotMustMiss = false;
		return false;
	}

	const netball::Player& shooter = players[ballCarrier];
	shotIsSuper = SuperShotActive() &&
		netball::IsInSuperShotZone(shooter.position, shooter.homeTeam);
	const bool forcedAIMiss = aiShotMustMiss;
	aiShotMustMiss = false;
	passStart = ballPosition;
	const float goalLine = shooter.homeTeam
		? netball::CourtHalfLength : -netball::CourtHalfLength;
	const float poleDx = shooter.position.x;
	const float poleDz = shooter.position.z - goalLine;
	const float distanceFromPole =
		std::sqrt(poleDx * poleDx + poleDz * poleDz);
	const float maxShotDistance = std::sqrt(
		netball::GoalCircleRadiusX * netball::GoalCircleRadiusX +
		(netball::GoalCircleRadiusZ + netball::GoalCircleCenterInset) *
			(netball::GoalCircleRadiusZ + netball::GoalCircleCenterInset));
	const float circleCentreZ =
		goalLine - std::copysign(netball::GoalCircleCenterInset, goalLine);
	const float circleDx = shooter.position.x / netball::GoalCircleRadiusX;
	const float circleDz =
		(shooter.position.z - circleCentreZ) / netball::GoalCircleRadiusZ;
	const bool nearCircleEdge =
		std::sqrt(circleDx * circleDx + circleDz * circleDz) > 0.8f;
	const float baseMissChance = std::max(0.0f, 1.0f - 1.3f * (1.0f - (0.05f + 0.40f * std::clamp(
		distanceFromPole / maxShotDistance, 0.0f, 1.0f))));
	const float rawDistanceMissChance = nearCircleEdge
		? std::min(1.0f, (1.0f - (1.0f - baseMissChance) * 0.75f) * 1.2f)
		: baseMissChance;
	const bool userShot = !IsAIAttacking();
	const netball::DifficultySettings& level = Difficulty();
	const float distanceMissChance = userShot
		? std::min(1.0f, rawDistanceMissChance * level.userMissScale)
		: (shooter.homeTeam ? rawDistanceMissChance
			: (level.aiFixedMissChance >= 0.0f ? 0.0f
				: std::min(1.0f, rawDistanceMissChance * level.aiMissScale)));
	std::uniform_real_distribution<float> randomChance(0.0f, 1.0f);
	const float shootingSkill = std::clamp(
		1.0f - 0.2f * float(profiles[ballCarrier].shooting - 3), 0.0f, 2.0f);
	shotDistanceMustMiss = randomChance(randomEngine) <
		std::min(1.0f, distanceMissChance * shootingSkill);
	shotMustMiss = forcedAIMiss || shotDistanceMustMiss;
	if (userShot && CatchUpAssistActive(shooter.homeTeam))
		shotMustMiss = false;
	const float meterError = shootingMeterPosition - ShootingMeterMark;
	const float hoopCenterZ =
		goalLine - std::copysign(netball::HoopCenterInset, goalLine);
	shotHoopCenter = { 0.0f, hoopCenterZ };
	const float scatterRadius = std::abs(meterError) *
		(userShot ? level.userScatter : 1.2f);
	std::uniform_real_distribution<float> randomUnit(-1.0f, 1.0f);
	float missX = randomUnit(randomEngine) * scatterRadius;
	float missZ = randomUnit(randomEngine) * scatterRadius;
	shootingMeterGuaranteedShot = !shotMustMiss &&
		(std::abs(meterError) <= (userShot ? level.userMeterWindow : 0.03f) ||
			(userShot && CatchUpAssistActive(shooter.homeTeam)));
	// Outside the guaranteed zone the user can still get lucky; the chance
	// fades with distance from the target and is larger on easier levels.
	if (userShot && !shotMustMiss && !shootingMeterGuaranteedShot)
	{
		const float beyond = std::abs(meterError) - level.userMeterWindow;
		const float luck = level.userLuckyChance *
			std::clamp(1.0f - beyond / level.userLuckyRange, 0.0f, 1.0f);
		if (randomChance(randomEngine) < luck)
			shootingMeterGuaranteedShot = true;
	}
	if (shootingMeterGuaranteedShot)
	{
		missX = 0.0f;
		missZ = 0.0f;
	}
	else if (shotMustMiss)
	{
		missX = std::copysign(2.0f, randomUnit(randomEngine));
		missZ = 0.0f;
	}
	passEnd = {
		missX,
		netball::HoopHeight - netball::HoopNetDepth - netball::BallRadius,
		hoopCenterZ + missZ
	};
	shotScores = false;
	shotInNet = false;
	shotResetPending = false;
	shotArcHeight = ShotPeakHeight -
		(passStart.y + passEnd.y) * 0.5f;

	const float dx = passEnd.x - passStart.x;
	const float dz = passEnd.z - passStart.z;
	passElapsed = 0.0f;
	passDuration = std::max(0.8f, std::sqrt(dx * dx + dz * dz) / 8.0f);
	shootingMeterActive = false;
	shootingMeterDisplayTime = 0.3f;
	shotInFlight = true;
	shotInNet = false;
	passInFlight = true;
	looseBall = false;
	playerVelocity = {};
	if (shooter.homeTeam)
		++homeShots;
	else
		++awayShots;
	const float accuracy = std::clamp(
		1.0f - std::abs(meterError) / 0.5f, 0.0f, 1.0f);
	{
		std::ostringstream event;
		event << std::fixed << std::setprecision(3)
			<< "SHOT_DECISION team="
			<< (shooter.homeTeam ? "HOME" : "AWAY")
			<< " player=" << netball::Positions[shooter.role].abbreviation
			<< " meter_position=" << shootingMeterPosition
			<< " target_position=" << ShootingMeterMark
			<< " timing_error=" << meterError
			<< " timing_accuracy_percent=" << accuracy * 100.0f
			<< " distance_from_pole=" << distanceFromPole
			<< " distance_miss_chance_percent=" << distanceMissChance * 100.0f
			<< " guaranteed=" << (shootingMeterGuaranteedShot ? "yes" : "no")
			<< " ai_forced_miss=" << (forcedAIMiss ? "yes" : "no")
			<< " distance_forced_miss="
			<< (shotDistanceMustMiss ? "yes" : "no")
			<< " target=(" << passEnd.x << "," << passEnd.z << ")";
		LogEvent(event.str());
	}
	movementMessage = "SHOT TIMING: " +
		std::to_string(static_cast<int>(accuracy * 100.0f)) + "%";
	messageTime = passDuration;
	return true;
}

void NetballGame::ResetAfterShot()
{
	const bool nextPossessionHome = !players[ballCarrier].homeTeam;
	LogEvent(std::string("SCORING_RESET next_possession=") +
		(nextPossessionHome ? "HOME" : "AWAY"));
	aiNoReceiverLogged = false;
	shotResetPending = false;
	shotResetDelayRemaining = 0.0f;
	shotInNet = false;
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		players[i].position = startingPositions[i];
		previousFramePositions[i] = startingPositions[i];
		attackerProgressPositions[i] = startingPositions[i];
		stationaryDurations[i] = 0.0f;
		defenderClipDurations[i] = 0.0f;
		separatingFromMarker[i] = false;
		escapingCourtEdge[i] = false;
		ChooseRandomDirection(i);
	}

	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (players[i].homeTeam == nextPossessionHome &&
			players[i].role == 3)
		{
			SetBallCarrier(i, "GOAL_RESTART", false);
			break;
		}
	}
	SelectPlayerForPossession();
	passReceiver = ballCarrier;
	controlledPassOption = -1;
	playerVelocity = {};
	RefreshPassTargets();
	ballPosition = players[ballCarrier].position;
	ballPosition.x += players[ballCarrier].homeTeam
		? BallSideOffset : -BallSideOffset;
	ballPosition.y = CarriedBallHeight;
	positionHistory.clear();
	RecordPlayerPositions();
	movementMessage = "PLAYERS RESET - BALL TO CENTRE";
	messageTime = 2.0f;
	restartHoldRemaining = 0.6f;
}

void NetballGame::UpdateShootingMeter(const float elapsedTime)
{
	if (!shootingMeterActive)
	{
		shootingMeterDisplayTime = std::max(
			0.0f, shootingMeterDisplayTime - elapsedTime);
		return;
	}

	shootingMeterElapsed = std::min(
		shootingMeterElapsed + shootingMeterSpeed * elapsedTime, 1.0f);
	shootingMeterPosition = shootingMeterElapsed;
	if (shootingMeterElapsed >= 1.0f)
		StartShot();
}

float NetballGame::GetShootingMeterStartPosition() const
{
	return players[ballCarrier].role == 1
		? 0.1f : 0.0f;
}

float NetballGame::GetShootingMeterSpeed() const
{
	const netball::Player& shooter = players[ballCarrier];
	const float goalLine = shooter.homeTeam
		? netball::CourtHalfLength : -netball::CourtHalfLength;
	const float dx = shooter.position.x;
	const float dz = shooter.position.z -
		(goalLine - std::copysign(netball::HoopCenterInset, goalLine));
	const float distanceToHoop = std::sqrt(dx * dx + dz * dz);
	return BaseShootingMeterSpeed * (1.0f + distanceToHoop * 0.08f);
}

void NetballGame::UiFilledRect(const olc::vf2d& position,
	const olc::vf2d& size, const olc::Pixel colour)
{
	draw.FilledRect(position * uiScale, size * uiScale, colour);
}

void NetballGame::UiString(const olc::vf2d& position, const std::string& text,
	const olc::Pixel colour, const olc::vf2d& scale)
{
	draw.StringProp(position * uiScale, text, colour,
		scale * uiScale * netball::HudFontScale(), netball::HudFont());
}

olc::vf2d NetballGame::UiTextSize(const std::string& text,
	const olc::vf2d& scale)
{
	return draw.GetTextSize(text, true,
		scale * uiScale * netball::HudFontScale(), netball::HudFont()) / uiScale;
}

olc::vf2d NetballGame::UiScreenSize()
{
	return { float(ScreenSize().x) / uiScale, float(ScreenSize().y) / uiScale };
}

void NetballGame::DrawGameOver()
{
	const olc::vf2d screen = UiScreenSize();
	const std::string result = homeScore > awayScore ? "HOME WINS!"
		: awayScore > homeScore ? "AWAY WINS!" : "DRAW";
	const std::string score = std::to_string(homeScore) + "  -  " +
		std::to_string(awayScore);
	const olc::vf2d titleScale{ 8.0f, 8.0f };
	const olc::vf2d scoreScale{ 7.0f, 7.0f };
	const olc::vf2d titleSize = UiTextSize(result, titleScale);
	const olc::vf2d scoreSize = UiTextSize(score, scoreScale);
	const float width = std::max(titleSize.x, scoreSize.x) + 120.0f;
	const float height = titleSize.y + scoreSize.y + 90.0f;
	const olc::vf2d topLeft{ (screen.x - width) * 0.5f, screen.y * 0.3f };
	UiFilledRect(topLeft, { width, height }, olc::Pixel(0, 0, 0, 215));
	olc::Pixel colour = olc::Colour::WHITE;
	if (homeScore > awayScore)
		colour = netball::HomeTeamColour;
	else if (awayScore > homeScore)
		colour = netball::AwayTeamDisplayColour;
	UiString({ (screen.x - titleSize.x) * 0.5f, topLeft.y + 30.0f },
		result, colour, titleScale);
	UiString({ (screen.x - scoreSize.x) * 0.5f,
		topLeft.y + 60.0f + titleSize.y },
		score, olc::Colour::WHITE, scoreScale);
	DrawConfirmPrompt("to continue", gameOverHold / GameOverHoldSeconds);
}

void NetballGame::DrawShootingMeter()
{
	if (!shootingMeterActive)
		return;

	constexpr float barWidth = 260.0f;
	constexpr float barHeight = 38.0f;
	constexpr float framePadding = 5.0f;
	constexpr float frameHeight = 58.0f;
	const netball::Player& shooter = players[ballCarrier];
	const olc::vf4d clipPosition = draw.GetMVPMatrix() *
		olc::vf4d{ shooter.position.x, 3.44f, shooter.position.z, 1.0f };
	if (clipPosition.w <= 0.0f)
		return;

	const float ndcX = clipPosition.x / clipPosition.w;
	const float ndcY = clipPosition.y / clipPosition.w;
	if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f)
		return;

	const olc::vf2d screenSize = UiScreenSize();
	const olc::vf2d playerScreenPosition{
		(ndcX + 1.0f) * 0.5f * screenSize.x,
		(ndcY + 1.0f) * 0.5f * screenSize.y
	};
	const olc::vf2d frameSize{ barWidth + framePadding * 2.0f, frameHeight };
	const olc::vf2d framePosition{
		std::clamp(playerScreenPosition.x - frameSize.x * 0.5f,
			8.0f, screenSize.x - frameSize.x - 8.0f),
		std::clamp(playerScreenPosition.y - frameSize.y - 42.0f,
			112.0f, screenSize.y - frameSize.y - 60.0f)
	};
	const olc::vf2d barPosition{
		framePosition.x + framePadding,
		framePosition.y + framePadding
	};

	UiFilledRect(framePosition, frameSize, Shade(10, 22, 58));
	UiFilledRect(barPosition, { barWidth, 8.0f }, Shade(42, 205, 76));
	UiFilledRect(
		{ barPosition.x, barPosition.y },
		{ barWidth * 0.1f, 8.0f }, Shade(198, 42, 42));
	UiFilledRect(
		{ barPosition.x + barWidth * 0.9f, barPosition.y },
		{ barWidth * 0.1f, 8.0f }, Shade(198, 42, 42));
	UiFilledRect(
		{ barPosition.x, barPosition.y + 8.0f },
		{ barWidth, barHeight - 8.0f },
		Shade(12, 30, 78));

	const float markX = barPosition.x + ShootingMeterMark * barWidth;
	const float markHalfWidth =
		std::max(4.0f, Difficulty().userMeterWindow * barWidth);
	UiFilledRect({ markX - markHalfWidth, barPosition.y + 8.0f },
		{ markHalfWidth * 2.0f, barHeight - 8.0f },
		Shade(255, 215, 55));

	for (int tick = 1; tick < 24; ++tick)
	{
		const float tickX = barPosition.x + barWidth * float(tick) / 24.0f;
		UiFilledRect({ tickX - 1.0f, barPosition.y + 11.0f },
			{ 2.0f, barHeight - 14.0f }, Shade(205, 220, 235));
	}

	const float pointerX = barPosition.x + shootingMeterPosition * barWidth;
	draw.FilledPolygon(olc::Structure::Fan, {
		olc::vf2d{ pointerX, barPosition.y + barHeight + 1.0f } * uiScale,
		olc::vf2d{ pointerX - 8.0f, barPosition.y + barHeight + 11.0f } * uiScale,
		olc::vf2d{ pointerX + 8.0f, barPosition.y + barHeight + 11.0f } * uiScale
	}, olc::Colour::WHITE);
}

void NetballGame::UpdatePassing(const float elapsedTime)
{
	EnforceDefenderLock();
	if (!passInFlight)
	{
		if (looseBall)
		{
			const olc::vf4d previousPosition = ballPosition;
			ballPosition.x += looseBallHorizontalVelocity.x * elapsedTime;
			ballPosition.z += looseBallHorizontalVelocity.y * elapsedTime;
			const float horizontalDrag = std::exp(-0.8f * elapsedTime);
			looseBallHorizontalVelocity *= horizontalDrag;

			looseBallVerticalVelocity -= 9.8f * elapsedTime;
			ballPosition.y += looseBallVerticalVelocity * elapsedTime;
			if (ballPosition.y <= netball::BallRadius)
			{
				ballPosition.y = netball::BallRadius;
				looseBallVerticalVelocity =
					std::max(0.0f, -looseBallVerticalVelocity * 0.28f);
				looseBallHorizontalVelocity *= 0.72f;
				if (looseBallVerticalVelocity < 0.8f)
					looseBallVerticalVelocity = 0.0f;
			}
			if (HandleBallOutOfBounds(previousPosition))
				return;
			return;
		}

		ballPosition = players[ballCarrier].position;
		ballPosition.x += players[ballCarrier].homeTeam
			? BallSideOffset : -BallSideOffset;
		ballPosition.y = CarriedBallHeight;
		if (catchBlendRemaining > 0.0f)
		{
			catchBlendRemaining =
				std::max(0.0f, catchBlendRemaining - elapsedTime);
			const float amount = 1.0f - catchBlendRemaining / catchBlendDuration;
			const float eased = amount * amount * (3.0f - 2.0f * amount);
			ballPosition = catchBlendFrom + (ballPosition - catchBlendFrom) * eased;
		}
		return;
	}

	const float previousElapsed = passElapsed;
	passElapsed = std::min(passElapsed + elapsedTime, passDuration);
	const float t = passElapsed / passDuration;
	ballPosition.x = passStart.x + (passEnd.x - passStart.x) * t;
	ballPosition.z = passStart.z + (passEnd.z - passStart.z) * t;
	const auto getShotHeight = [&](const float amount)
	{
		return passStart.y + (passEnd.y - passStart.y) * amount +
			4.0f * shotArcHeight * amount * (1.0f - amount);
	};
	ballPosition.y = shotInFlight
		? shotInNet
			? passStart.y + (passEnd.y - passStart.y) * t
			: getShotHeight(t)
		: 1.9f + std::sin(t * 3.14159265358979323846f) * 3.2f;
	if (HandleBallOutOfBounds(passStart + (passEnd - passStart) *
		(previousElapsed / passDuration)))
		return;

	if (shotInFlight && !shotInNet)
	{
		constexpr float ballClearanceRadius = netball::HoopRadius -
			netball::HoopTubeRadius - netball::BallRadius + 0.04f;
		constexpr float rimContactRadius = netball::HoopRadius +
			netball::HoopTubeRadius + netball::BallRadius;
		const float hoopCrossingHeight = netball::HoopHeight + netball::BallRadius;
		const float previousT = previousElapsed / passDuration;
		const float previousY = getShotHeight(previousT);
		if (previousY > hoopCrossingHeight &&
			ballPosition.y <= hoopCrossingHeight)
		{
			const float crossingFraction = (previousY - hoopCrossingHeight) /
				(previousY - ballPosition.y);
			const float crossingT = previousT + (t - previousT) * crossingFraction;
			olc::vf2d crossingPosition{
				passStart.x + (passEnd.x - passStart.x) * crossingT,
				passStart.z + (passEnd.z - passStart.z) * crossingT
			};
			if (shootingMeterGuaranteedShot)
				crossingPosition = shotHoopCenter;
			const olc::vf2d fromHoop{
				crossingPosition.x - shotHoopCenter.x,
				crossingPosition.y - shotHoopCenter.y
			};
			const float distanceFromHoop = fromHoop.mag();
			if (!shotMustMiss && (shootingMeterGuaranteedShot ||
				distanceFromHoop <= ballClearanceRadius))
			{
				shotScores = true;
				shotInNet = true;
				passStart = {
					crossingPosition.x,
					hoopCrossingHeight,
					crossingPosition.y
				};
				passEnd = {
					crossingPosition.x,
					netball::HoopHeight - netball::HoopNetDepth -
						netball::BallRadius,
					crossingPosition.y
				};
				passElapsed = 0.0f;
				passDuration = 0.38f;
				ballPosition = passStart;
				return;
			}
			else if (distanceFromHoop <= rimContactRadius)
			{
				ballPosition = {
					crossingPosition.x, hoopCrossingHeight, crossingPosition.y
				};
				olc::vf2d bounceDirection = fromHoop;
				if (bounceDirection.mag2() < 0.0001f)
					bounceDirection = {
						0.0f, players[ballCarrier].homeTeam ? -1.0f : 1.0f
					};
				bounceDirection = bounceDirection.norm();
				shotInFlight = false;
				passInFlight = false;
				StartLooseBall(bounceDirection * 3.2f, 2.8f);
				reboundPending = true;
				{
					const olc::vf2d landing = PredictLooseBallLanding();
					if (std::abs(landing.x) <= netball::CourtHalfWidth &&
						std::abs(landing.y) <= netball::CourtHalfLength)
					{
						reboundMarkerActive = true;
						reboundMarker = landing;
					}
				}
				if (players[ballCarrier].homeTeam)
					++homeMissedShots;
				else
					++awayMissedShots;
				LogEvent("SHOT_RESULT result=MISS subtype=RIM_LOOSE team=" +
					std::string(players[ballCarrier].homeTeam
						? "HOME" : "AWAY") +
					" ai_forced_miss=" +
					(shotMustMiss && !shotDistanceMustMiss ? "yes" : "no") +
					" distance_forced_miss=" +
					(shotDistanceMustMiss ? "yes" : "no") +
					" crossing=(" + std::to_string(crossingPosition.x) + "," +
					std::to_string(crossingPosition.y) + ")");
				movementMessage = "RIM - LOOSE BALL";
				messageTime = 5.0f;
				return;
			}
		}

	}

	if (passElapsed >= passDuration)
	{
		passInFlight = false;
		if (shotInFlight)
		{
			shotInFlight = false;
			if (shotScores)
			{
				LogEvent("SHOT_RESULT result=GOAL team=" +
					std::string(players[ballCarrier].homeTeam ? "HOME" : "AWAY"));
				RegisterGoal();
				shotInNet = false;
				shotResetPending = true;
				shotResetDelayRemaining = 0.5f;
				movementMessage = "GOAL";
				messageTime = 0.5f;
			}
			else
			{
				if (players[ballCarrier].homeTeam)
					++homeMissedShots;
				else
					++awayMissedShots;
				LogEvent("SHOT_RESULT result=MISS team=" +
					std::string(players[ballCarrier].homeTeam ? "HOME" : "AWAY") +
					" ai_forced_miss=" +
					(shotMustMiss && !shotDistanceMustMiss ? "yes" : "no") +
					" distance_forced_miss=" +
					(shotDistanceMustMiss ? "yes" : "no") +
					" landing=(" + std::to_string(ballPosition.x) + "," +
					std::to_string(ballPosition.z) + ")");
				const olc::vf2d missVelocity{
					(passEnd.x - passStart.x) / passDuration,
					(passEnd.z - passStart.z) / passDuration
				};
				StartLooseBall(missVelocity, -0.5f);
				reboundPending = true;
				movementMessage = "LOOSE BALL - MOVE TO REBOUND";
				messageTime = 5.0f;
			}
			return;
		}

		const netball::Player& passer = players[ballCarrier];
		const netball::Player& receiver = players[passReceiver];
		constexpr float defenderInfluenceRadius = 4.0f;
		constexpr float passLineRadius = 1.5f;
		float nearestDefenderDistance = defenderInfluenceRadius;
		bool defenderOnPassLine = false;
		const float passDx = passEnd.x - passStart.x;
		const float passDz = passEnd.z - passStart.z;
		const float passLengthSquared = passDx * passDx + passDz * passDz;
		for (const netball::Player& defender : players)
		{
			if (defender.homeTeam == passer.homeTeam)
				continue;

			const float receiverDx = defender.position.x - receiver.position.x;
			const float receiverDz = defender.position.z - receiver.position.z;
			const float receiverDistance = std::sqrt(
				receiverDx * receiverDx + receiverDz * receiverDz);
			nearestDefenderDistance = std::min(
				nearestDefenderDistance, receiverDistance);

			if (receiverDistance > defenderInfluenceRadius ||
				passLengthSquared <= 0.0001f)
				continue;

			const float fromPasserX = defender.position.x - passStart.x;
			const float fromPasserZ = defender.position.z - passStart.z;
			const float alongPass = std::clamp(
				(fromPasserX * passDx + fromPasserZ * passDz) /
					passLengthSquared,
				0.0f, 1.0f);
			const float lineDx = defender.position.x -
				(passStart.x + passDx * alongPass);
			const float lineDz = defender.position.z -
				(passStart.z + passDz * alongPass);
			if (lineDx * lineDx + lineDz * lineDz <=
				passLineRadius * passLineRadius)
				defenderOnPassLine = true;
		}

		const float defenderProximity = std::clamp(
			(defenderInfluenceRadius - nearestDefenderDistance) /
				defenderInfluenceRadius,
			0.0f, 1.0f);
		const float destinationDx = receiver.position.x - passDestination.x;
		const float destinationDz = receiver.position.z - passDestination.y;
		const float destinationDistance = std::sqrt(
			destinationDx * destinationDx + destinationDz * destinationDz);
		const float distanceDrop = std::min(0.7f,
			std::max(0.0f, destinationDistance - 0.5f) * 0.15f);
		const float passLength = std::sqrt(passDx * passDx + passDz * passDz);
		const float lengthDrop = std::min(0.15f,
			std::max(0.0f, passLength - 4.0f) * 0.012f);
		const float homeScale = passer.homeTeam
			? Difficulty().homeDropScale : Difficulty().awayDropScale;
		const float baseDrop = std::min(0.95f, homeScale * (0.4f * std::min(
			0.30f, 0.05f + defenderProximity * 0.15f +
				(defenderOnPassLine ? 0.10f : 0.0f)) + distanceDrop +
			lengthDrop));
		// Better passers/receivers drop less; rating 3 is neutral.
		const float skillScale = std::clamp(
			1.0f - 0.1f * float(profiles[ballCarrier].passing - 3) -
				0.1f * float(profiles[passReceiver].passing - 3), 0.2f, 2.0f);
		// Two or more defenders on either end of the pass add up to 30%, scaled
		// by the average defence rating of the two closest defenders.
		float doubleTeamDrop = 0.0f;
		std::size_t doubleTeamDefenderCount = 0;
		doubleTeamDrop = ComputeDoubleTeam(doubleTeamDefenderCount);
		if (throwDoubleTeamDrop > doubleTeamDrop)
		{
			doubleTeamDrop = throwDoubleTeamDrop;
			doubleTeamDefenderCount = throwDoubleTeamCount;
		}
		throwDoubleTeamDrop = 0.0f;
		throwDoubleTeamCount = 0;
		if (doubleTeamDefenderCount > 0)
			LogEvent("DOUBLE_TEAM_PASS defenders=" +
				std::to_string(doubleTeamDefenderCount) +
				" extra_drop_percent=" +
				std::to_string(int(std::lround(doubleTeamDrop * 100.0f))));
		const float dropChance = CatchUpAssistActive(passer.homeTeam) ? 0.0f
			: std::min(0.95f, baseDrop * skillScale + doubleTeamDrop);
		std::uniform_real_distribution<float> randomChance(0.0f, 1.0f);
		if (randomChance(randomEngine) < dropChance)
		{
			dropPending = true;
			dropTeamHome = passer.homeTeam;
			const olc::vf2d passDirection{ passDx, passDz };
			const olc::vf2d looseDirection = passDirection.mag2() > 0.0001f
				? passDirection.norm() : olc::vf2d{};
			StartLooseBall(looseDirection * 2.5f, 2.2f);
			{
				std::ostringstream event;
				event << std::fixed << std::setprecision(3)
					<< "PASS_RESULT result=DROPPED missed_pass=yes chance_percent="
					<< dropChance * 100.0f
					<< " nearest_defender_distance="
					<< nearestDefenderDistance
					<< " receiver_distance_from_destination="
					<< destinationDistance
					<< " defender_on_pass_line="
					<< (defenderOnPassLine ? "yes" : "no")
					<< " passer="
					<< netball::Positions[passer.role].abbreviation
					<< " receiver="
					<< netball::Positions[receiver.role].abbreviation;
				LogEvent(event.str());
			}
			movementMessage = "DROPPED PASS - LOOSE BALL (" +
				std::to_string(static_cast<int>(dropChance * 100.0f)) + "%)";
			messageTime = 5.0f;
			return;
		}

		const std::size_t chainPasser = ballCarrier;
		const std::size_t chainReceiver = passReceiver;
		const std::array<std::size_t, 2> previousChain =
			lastReceiver == chainPasser
				? std::array<std::size_t, 2>{ assistChain[0], assistChain[1] }
				: std::array<std::size_t, 2>{ netball::PlayerCount, netball::PlayerCount };
		SetBallCarrier(passReceiver, "PASS_CAUGHT");
		lastReceiver = chainReceiver;
		assistChain = { chainPasser, previousChain[0] };
		lockedDefenders = CountNearbyDefenders(passReceiver, DoubleTeamRadius);
		defenderLockActive = true;
		LogEvent("PASS_RESULT result=CAUGHT receiver=" +
			std::string(netball::Positions[players[passReceiver].role].abbreviation));
		SelectPlayerForPossession();
		controlledPassOption = -1;
		RefreshPassTargets();
		playerVelocity = {};
		catchBlendFrom = passEnd;
		{
			olc::vf4d hands = players[passReceiver].position;
			hands.x += players[passReceiver].homeTeam
				? BallSideOffset : -BallSideOffset;
			const float gap = std::sqrt(
				(hands.x - passEnd.x) * (hands.x - passEnd.x) +
				(hands.z - passEnd.z) * (hands.z - passEnd.z));
			catchBlendDuration = std::clamp(gap / 10.0f, 0.15f, 0.6f);
		}
		catchBlendRemaining = catchBlendDuration;
		{
			olc::vf4d hands = players[passReceiver].position;
			hands.x += players[passReceiver].homeTeam
				? BallSideOffset : -BallSideOffset;
			std::ostringstream event;
			event << std::fixed << std::setprecision(3)
				<< "PASS_CATCH_BLEND landing=(" << passEnd.x << "," << passEnd.z
				<< ") receiver_hands=(" << hands.x << "," << hands.z
				<< ") receiver_pos=(" << players[passReceiver].position.x << ","
				<< players[passReceiver].position.z << ") destination=("
				<< passDestination.x << "," << passDestination.y
				<< ") blend_duration=" << catchBlendDuration;
			LogEvent(event.str());
		}
		ballPosition = passEnd;
	}
}

bool NetballGame::HandleBallOutOfBounds(
	const olc::vf4d& previousPosition)
{
	const float xLimit = netball::CourtHalfWidth - netball::BallRadius;
	const float zLimit = netball::CourtHalfLength - netball::BallRadius;
	float crossing = 1.0f;
	bool crossedSide = false;
	float sideSign = 0.0f;
	float endSign = 0.0f;

	const auto checkCrossing = [&](const float previous, const float current,
		const float limit, const float sign, const bool isSide)
	{
		const float signedCurrent = current * sign;
		if (signedCurrent <= limit)
			return;

		const float signedPrevious = previous * sign;
		const float amount = signedPrevious >= limit
			? 0.0f : (limit - signedPrevious) / (signedCurrent - signedPrevious);
		if (amount < crossing)
		{
			crossing = amount;
			crossedSide = isSide;
			sideSign = isSide ? sign : 0.0f;
			endSign = isSide ? 0.0f : sign;
		}
	};

	checkCrossing(previousPosition.x, ballPosition.x, xLimit, 1.0f, true);
	checkCrossing(previousPosition.x, ballPosition.x, xLimit, -1.0f, true);
	checkCrossing(previousPosition.z, ballPosition.z, zLimit, 1.0f, false);
	checkCrossing(previousPosition.z, ballPosition.z, zLimit, -1.0f, false);
	if (crossing >= 1.0f)
		return false;

	const float crossX = previousPosition.x +
		(ballPosition.x - previousPosition.x) * crossing;
	const float crossZ = previousPosition.z +
		(ballPosition.z - previousPosition.z) * crossing;
	const olc::vf2d boundaryPosition = crossedSide
		? olc::vf2d{
			sideSign * netball::CourtHalfWidth,
			std::clamp(crossZ, -netball::CourtHalfLength,
				netball::CourtHalfLength)
		}
		: olc::vf2d{
			std::clamp(crossX, -netball::CourtHalfWidth,
				netball::CourtHalfWidth),
			endSign * netball::CourtHalfLength
		};

	if (shotInFlight)
	{
		if (players[ballCarrier].homeTeam)
			++homeMissedShots;
		else
			++awayMissedShots;
		LogEvent("SHOT_RESULT result=MISS subtype=OUT_OF_BOUNDS team=" +
			std::string(players[ballCarrier].homeTeam ? "HOME" : "AWAY") +
			" landing=(" + std::to_string(ballPosition.x) + "," +
			std::to_string(ballPosition.z) + ")");
	}
	StartThrowIn(!players[ballCarrier].homeTeam, boundaryPosition);
	return true;
}

void NetballGame::StartThrowIn(const bool throwInTeamHome,
	const olc::vf2d& boundaryPosition)
{
	constexpr float playerEdgeInset = BallSideOffset +
		netball::BallRadius + 0.1f;
	constexpr float endLineInset = 0.7f;
	constexpr float searchStep = 0.25f;
	float bestScore = std::numeric_limits<float>::max();
	std::size_t thrower = netball::PlayerCount;
	olc::vf4d throwerPosition{};
	const bool isSideLine =
		std::abs(boundaryPosition.x) >= netball::CourtHalfWidth - 0.01f;
	const float boundaryCoordinate = isSideLine
		? boundaryPosition.y : boundaryPosition.x;
	const float coordinateLimit = isSideLine
		? netball::CourtHalfLength : netball::CourtHalfWidth;
	const float searchLength = isSideLine
		? netball::CourtLength : netball::CourtWidth;

	// Baseline throws go to the GA (attacking end) or the GK (own end).
	std::size_t wantedRole = netball::PlayerCount;
	if (!isSideLine)
	{
		const bool attackingEnd = throwInTeamHome == (boundaryPosition.y > 0.0f);
		wantedRole = attackingEnd ? 1 : 6;
	}

	for (int attempt = 0; attempt < 2 && thrower >= netball::PlayerCount;
		++attempt)
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (players[i].homeTeam != throwInTeamHome)
			continue;
		if (attempt == 0 && wantedRole < netball::PlayerCount &&
			players[i].role != wantedRole)
			continue;

		for (float offset = 0.0f; offset <= searchLength;
			offset += searchStep)
		{
			for (const float direction : { -1.0f, 1.0f })
			{
				const float coordinate = std::clamp(
					boundaryCoordinate + direction * offset,
					-coordinateLimit, coordinateLimit);
				const olc::vf4d candidate = isSideLine
					? olc::vf4d{
						std::copysign(netball::CourtHalfWidth - playerEdgeInset,
							boundaryPosition.x),
						0.0f, coordinate
					}
					: olc::vf4d{
						coordinate, 0.0f,
						std::copysign(netball::CourtHalfLength - endLineInset,
							boundaryPosition.y)
					};
				if (!netball::IsLegalPosition(players[i], candidate))
					continue;

				const float dx = candidate.x - players[i].position.x;
				const float dz = candidate.z - players[i].position.z;
				const bool overlaps = netball::OverlapsAnotherPlayer(
					players, i, candidate, throwInTeamHome);
				const float score = offset * offset +
					(dx * dx + dz * dz) * 0.05f +
					(overlaps ? 100.0f : 0.0f);
				if (score < bestScore)
				{
					bestScore = score;
					thrower = i;
					throwerPosition = candidate;
				}
			}
		}
	}

	if (thrower >= netball::PlayerCount)
	{
		movementMessage = "OUT OF BOUNDS - NO LEGAL THROW-IN POSITION";
		messageTime = 2.0f;
		return;
	}

	players[thrower].position = throwerPosition;
	constexpr float behindLine = 0.35f;
	if (isSideLine)
		players[thrower].position.x +=
			std::copysign(playerEdgeInset + behindLine, boundaryPosition.x);
	else
		players[thrower].position.z +=
			std::copysign(endLineInset + behindLine, boundaryPosition.y);
	throwInInsidePosition = throwerPosition;

	// The nearest defender drops back ~2m inside the court from the thrower;
	// any other defender inside that distance is pushed out to it.
	constexpr float defenderGap = 2.0f;
	const float inwardX = isSideLine ? -std::copysign(1.0f, boundaryPosition.x) : 0.0f;
	const float inwardZ = isSideLine ? 0.0f : -std::copysign(1.0f, boundaryPosition.y);
	const auto tryPlaceDefender = [&](const std::size_t index, const float baseX,
		const float baseZ) -> bool
	{
		constexpr float Pi = 3.14159265f;
		const float baseAngle = std::atan2(baseZ, baseX);
		for (int step = 0; step <= 9; ++step)
		{
			for (const float sign : { 1.0f, -1.0f })
			{
				if (step == 0 && sign < 0.0f)
					continue;
				const float angle = baseAngle + sign * float(step) * Pi / 18.0f;
				const olc::vf4d candidate{
					throwerPosition.x + defenderGap * std::cos(angle), 0.0f,
					throwerPosition.z + defenderGap * std::sin(angle) };
				if (netball::IsLegalPosition(players[index], candidate) &&
					!netball::OverlapsAnotherPlayer(players, index, candidate,
						throwInTeamHome))
				{
					players[index].position = candidate;
					return true;
				}
			}
		}
		return false;
	};
	std::size_t marker = netball::PlayerCount;
	float markerDistance = std::numeric_limits<float>::max();
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (players[i].homeTeam == throwInTeamHome)
			continue;
		const float dx = players[i].position.x - throwerPosition.x;
		const float dz = players[i].position.z - throwerPosition.z;
		const float distance = dx * dx + dz * dz;
		if (distance < markerDistance)
		{
			markerDistance = distance;
			marker = i;
		}
	}
	if (isSideLine && marker < netball::PlayerCount)
		tryPlaceDefender(marker, inwardX, inwardZ);
	for (std::size_t i = 0; i < players.size() && isSideLine; ++i)
	{
		if (players[i].homeTeam == throwInTeamHome || i == marker)
			continue;
		const float dx = players[i].position.x - throwerPosition.x;
		const float dz = players[i].position.z - throwerPosition.z;
		if (dx * dx + dz * dz < defenderGap * defenderGap)
			tryPlaceDefender(i, dx == 0.0f && dz == 0.0f ? inwardX : dx,
				dx == 0.0f && dz == 0.0f ? inwardZ : dz);
	}
	if (!isSideLine)
	{
		// Baseline throw: defenders inside the circle are respawned just outside
		// it, next to the attacker they mark (opposite role: GS-GK, GA-GD, ...).
		const bool positiveEnd = boundaryPosition.y > 0.0f;
		const float circleCentreZ = (positiveEnd ? 1.0f : -1.0f) *
			(netball::CourtHalfLength - netball::GoalCircleCenterInset);
		for (std::size_t i = 0; i < players.size(); ++i)
		{
			if (players[i].homeTeam == throwInTeamHome ||
				netball::GoalCircleMetric(players[i].position, positiveEnd) > 1.1f)
				continue;
			olc::vf4d target = players[i].position;
			for (std::size_t j = 0; j < players.size(); ++j)
				if (players[j].homeTeam == throwInTeamHome &&
					players[j].role == 6 - players[i].role)
					target = players[j].position;
			float dx = target.x / netball::GoalCircleRadiusX;
			float dz = (target.z - circleCentreZ) / netball::GoalCircleRadiusZ;
			// Marking from the court side of the circle.
			dz = (positiveEnd ? -1.0f : 1.0f) * std::max(std::abs(dz), 0.2f);
			const float baseAngle = std::atan2(dz, dx);
			bool placed = false;
			for (int step = 0; step <= 18 && !placed; ++step)
				for (const float sign : { 1.0f, -1.0f })
				{
					const float angle = baseAngle + sign * float(step) * 0.1f;
					const olc::vf4d candidate{
						1.12f * netball::GoalCircleRadiusX * std::cos(angle), 0.0f,
						circleCentreZ + 1.12f * netball::GoalCircleRadiusZ *
							std::sin(angle) };
					if (!netball::IsLegalPosition(players[i], candidate) ||
						netball::OverlapsAnotherPlayer(players, i, candidate,
							throwInTeamHome))
						continue;
					players[i].position = candidate;
					placed = true;
					break;
				}
		}
	}
	SetBallCarrier(thrower, "THROW_IN");
	LogEvent("THROW_IN_POSSESSION team=" +
		std::string(throwInTeamHome ? "HOME" : "AWAY") +
		" player=" + netball::Positions[players[thrower].role].abbreviation);
	aiNoReceiverLogged = false;
	SelectPlayerForPossession();
	passReceiver = thrower;
	controlledPassOption = -1;
	playerVelocity = {};
	passInFlight = false;
	shotInFlight = false;
	shotScores = false;
	looseBall = false;
	shootingMeterActive = false;
	shotInNet = false;
	shotResetPending = false;
	looseBallHorizontalVelocity = {};
	looseBallVerticalVelocity = 0.0f;
	RefreshPassTargets();
	ballPosition = throwerPosition;
	ballPosition.x += throwInTeamHome ? BallSideOffset : -BallSideOffset;
	ballPosition.y = CarriedBallHeight;

	for (std::size_t i = 0; i < players.size(); ++i)
	{
		previousFramePositions[i] = players[i].position;
		attackerProgressPositions[i] = players[i].position;
	}
	positionHistory.clear();
	RecordPlayerPositions();
	throwInPending = true;
	throwInPauseRemaining = 0.8f;
	movementMessage = "OUT OF BOUNDS - THROW-IN";
	messageTime = 0.8f;
}

void NetballGame::CompleteThrowIn()
{
	throwInPending = false;
	players[ballCarrier].position = throwInInsidePosition;
	previousFramePositions[ballCarrier] = throwInInsidePosition;
	attackerProgressPositions[ballCarrier] = throwInInsidePosition;
	ballPosition.x += players[ballCarrier].homeTeam
		? BallSideOffset : -BallSideOffset;
	ballPosition.y = CarriedBallHeight;
	movementMessage = "THROW-IN - PLAY";
	messageTime = 1.5f;
}

void NetballGame::SelectPlayerForPossession()
{
	if (players[ballCarrier].homeTeam || aiAttackMode)
	{
		selectedPlayer = ballCarrier;
		return;
	}

	float nearestDistanceSquared = std::numeric_limits<float>::max();
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (!players[i].homeTeam)
			continue;

		const float dx = players[i].position.x -
			players[ballCarrier].position.x;
		const float dz = players[i].position.z -
			players[ballCarrier].position.z;
		const float distanceSquared = dx * dx + dz * dz;
		if (distanceSquared < nearestDistanceSquared)
		{
			nearestDistanceSquared = distanceSquared;
			selectedPlayer = i;
		}
	}
}

void NetballGame::RegisterGoal()
{
	const bool homeScored = players[ballCarrier].homeTeam;
	const int points = shotIsSuper ? 2 : 1;
	++profiles[ballCarrier].goals;
	if (lastReceiver == ballCarrier)
		for (const std::size_t assister : assistChain)
			if (assister < profiles.size() && assister != ballCarrier &&
				players[assister].homeTeam == homeScored)
			{
				++profiles[assister].assists;
				LogEvent("STAT assist player=" + profiles[assister].name);
			}
	lastReceiver = netball::PlayerCount;
	assistChain.fill(netball::PlayerCount);
	LogEvent("STAT goal player=" + profiles[ballCarrier].name);
	if (homeScored)
		homeScore += points;
	else
		awayScore += points;
	if (shotIsSuper)
	{
		LogEvent("SUPER_SHOT_GOAL team=" + std::string(homeScored ? "HOME" : "AWAY"));
		superStreamRemaining = 1.2f;
		superStreamPosition = shotHoopCenter;
	}
	cheerRemaining[homeScored ? 1 : 0] = 1.5f;

	const olc::Pixel teamColour = homeScored
		? netball::HomeTeamColour : netball::AwayTeamDisplayColour;
	std::uniform_real_distribution<float> horizontal(-1.0f, 1.0f);
	std::uniform_real_distribution<float> forward(-1.0f, 1.0f);
	std::uniform_real_distribution<float> upward(4.0f, 10.0f);
	std::uniform_real_distribution<float> speed(2.0f, 7.0f);
	std::uniform_real_distribution<float> chance(0.0f, 1.0f);
	std::uniform_real_distribution<float> shade(0.5f, 1.4f);
	for (int i = 0; i < 108; ++i)
	{
		olc::vf2d direction{ horizontal(randomEngine), forward(randomEngine) };
		if (direction.mag2() < 0.0001f)
			direction = { 1.0f, 0.0f };
		direction = direction.norm();
		const float particleSpeed = speed(randomEngine);
		olc::Pixel colour = olc::Colour::WHITE;
		if (chance(randomEngine) < 0.75f)
		{
			const float scale = shade(randomEngine);
			const auto scaled = [scale](const uint8_t channel)
			{
				return uint8_t(std::clamp(float(channel) * scale, 0.0f, 255.0f));
			};
			colour = olc::Pixel(scaled(teamColour.r), scaled(teamColour.g),
				scaled(teamColour.b));
		}
		goalParticles.push_back({
			{ shotHoopCenter.x, netball::HoopHeight, shotHoopCenter.y },
			{ direction.x * particleSpeed, upward(randomEngine),
				direction.y * particleSpeed },
			colour,
			2.0f
		});
	}
}

void NetballGame::UpdateGoalParticles(const float elapsedTime)
{
	reboundLockout = std::max(0.0f, reboundLockout - elapsedTime);
	doubleTeamed.clear();
	if (!passInFlight && !looseBall && !throwInPending && !gameOver &&
		ballCarrier < players.size() &&
		CountNearbyDefenders(ballCarrier, DoubleTeamRadius).size() >= 2)
		doubleTeamed.push_back(ballCarrier);
	if (doubleTeamed.empty())
	{
		if (loggedDoubleTeam != static_cast<std::size_t>(-1))
			LogEvent("DOUBLE_TEAM_END");
		loggedDoubleTeam = static_cast<std::size_t>(-1);
	}
	else if (loggedDoubleTeam != doubleTeamed[0])
	{
		loggedDoubleTeam = doubleTeamed[0];
		LogEvent("DOUBLE_TEAM_START carrier=" + profiles[loggedDoubleTeam].name);
	}
	for (std::size_t team = 0; team < cheerRemaining.size(); ++team)
	{
		const bool cheering = cheerRemaining[team] > 0.0f;
		cheerRemaining[team] = std::max(0.0f, cheerRemaining[team] - elapsedTime);
		const float frequency = cheering ? 7.0f : 2.4f;
		for (std::size_t sub = team * 2; sub < team * 2 + 2; ++sub)
			cheerPhase[sub] = std::fmod(
				cheerPhase[sub] + frequency * elapsedTime, 1.0f);
	}
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	std::uniform_real_distribution<float> jitter(-0.15f, 0.15f);
	std::uniform_real_distribution<float> shade(0.5f, 1.4f);
	const auto teamShade = [&](const olc::Pixel colour)
	{
		if (unit(randomEngine) < 0.2f)
			return olc::Pixel(255, 255, 255);
		const float scale = shade(randomEngine);
		const auto scaled = [scale](const uint8_t channel)
		{
			return uint8_t(std::clamp(float(channel) * scale, 0.0f, 255.0f));
		};
		return olc::Pixel(scaled(colour.r), scaled(colour.g), scaled(colour.b));
	};

	// Yellow jet from the hoop after a super shot: twice the speed and lifetime.
	if (superStreamRemaining > 0.0f)
	{
		superStreamRemaining = std::max(0.0f, superStreamRemaining - elapsedTime);
		std::uniform_real_distribution<float> streamSpeed(8.0f, 20.0f);
		const int count = std::max(1, int(elapsedTime * 90.0f));
		for (int i = 0; i < count; ++i)
			goalParticles.push_back({
				{ superStreamPosition.x, netball::HoopHeight, superStreamPosition.y },
				{ jitter(randomEngine), streamSpeed(randomEngine), jitter(randomEngine) },
				olc::Pixel(255, uint8_t(200 + unit(randomEngine) * 55.0f), 30),
				4.0f });
	}

	// White particles highlight the courtside super zone: about 64 alive at once
	// (0.2 s lifetime), spawned at random points inside each zone.
	if (SuperShotActive() || debugMode)
	{
		std::uniform_real_distribution<float> angleRange(0.0f, Pi);
		std::uniform_real_distribution<float> radiusRange(
			netball::SuperShotRadiusScale, 1.0f);
		const float expected = 128.0f / 0.2f * elapsedTime;
		int count = int(expected);
		if (unit(randomEngine) < expected - float(count))
			++count;
		for (int i = 0; i < count; ++i)
		{
			const bool positiveEnd = unit(randomEngine) < 0.5f;
			const float goalLine = positiveEnd
				? netball::CourtHalfLength : -netball::CourtHalfLength;
			const float centerZ = goalLine -
				std::copysign(netball::GoalCircleCenterInset, goalLine);
			const float angle = angleRange(randomEngine) + (positiveEnd ? Pi : 0.0f);
			const float radius = radiusRange(randomEngine);
			goalParticles.push_back({
				{ radius * netball::GoalCircleRadiusX * std::cos(angle), 0.05f,
					centerZ + radius * netball::GoalCircleRadiusZ * std::sin(angle) },
				{ 0.0f, 0.3f, 0.0f }, olc::Colour::WHITE, 0.2f, 14.0f });
		}
	}

	// GS/GA standing in the 2-point zone sparkle in their team colours.
	if (SuperShotActive() || debugMode)
	{
		for (const netball::Player& player : players)
		{
			if (player.role > 1 ||
				!netball::IsInSuperShotZone(player.position, player.homeTeam))
				continue;
			const olc::Pixel teamColour = player.homeTeam
				? netball::HomeTeamColour : netball::AwayTeamDisplayColour;
			// About 240 sparks per second on a ring around the player, at the
			// height of the top quarter of the body, drawn 70% opaque.
			const float expected = 480.0f * elapsedTime;
			int count = int(expected);
			if (unit(randomEngine) < expected - float(count))
				++count;
			constexpr float ringRadius = 1.1f;
			for (int i = 0; i < count; ++i)
			{
				const float angle = unit(randomEngine) * 2.0f * Pi;
				const olc::Pixel shaded = teamShade(teamColour);
				goalParticles.push_back({
					{ player.position.x + ringRadius * std::cos(angle),
						2.6f + unit(randomEngine) * 0.84f,
						player.position.z + ringRadius * std::sin(angle) },
					{ 0.0f, (4.0f + unit(randomEngine) * 5.0f) * 1.25f, 0.0f },
					olc::Pixel(shaded.r, shaded.g, shaded.b, 178), 0.175f,
					-9.8f, 0.045f });
			}
		}
	}

	// Slow dark-blue ring around double-teamed ball carriers.
	if (!doubleTeamed.empty())
	{
		for (const std::size_t index : doubleTeamed)
		{
			const netball::Player& player = players[index];
			const float expected = 90.0f * elapsedTime;
			int count = int(expected);
			if (unit(randomEngine) < expected - float(count))
				++count;
			for (int i = 0; i < count; ++i)
			{
				const float angle = unit(randomEngine) * 2.0f * Pi;
				goalParticles.push_back({
					{ player.position.x + 0.9f * std::cos(angle),
						0.1f + unit(randomEngine) * 1.8f,
						player.position.z + 0.9f * std::sin(angle) },
					{ 0.0f, 0.6f + unit(randomEngine) * 0.6f, 0.0f },
					olc::Pixel(20, 40, 150, 200), 0.9f, 0.0f, 0.07f });
			}
		}
	}

	for (GoalParticle& particle : goalParticles)
	{
		particle.lifetime = std::max(0.0f, particle.lifetime - elapsedTime);
		particle.velocity.y += particle.accelerationY * elapsedTime;
		particle.position.x += particle.velocity.x * elapsedTime;
		particle.position.y += particle.velocity.y * elapsedTime;
		particle.position.z += particle.velocity.z * elapsedTime;
	}
	goalParticles.erase(std::remove_if(goalParticles.begin(), goalParticles.end(),
		[](const GoalParticle& particle) { return particle.lifetime <= 0.0f; }),
		goalParticles.end());
}

void NetballGame::DrawGoalParticles()
{
	if (goalParticles.empty())
		return;

	std::vector<olc::vf4d> points;
	std::vector<olc::Pixel> colours;
	points.reserve(goalParticles.size() * 12);
	colours.reserve(goalParticles.size() * 12);
	for (const GoalParticle& particle : goalParticles)
	{
		const float size = particle.size;
		const olc::vf4d& p = particle.position;
		const olc::Pixel colour = particle.colour;
		const olc::vf4d x0{ p.x - size, p.y - size, p.z };
		const olc::vf4d x1{ p.x + size, p.y - size, p.z };
		const olc::vf4d x2{ p.x + size, p.y + size, p.z };
		const olc::vf4d x3{ p.x - size, p.y + size, p.z };
		points.insert(points.end(), { x0, x1, x2, x0, x2, x3 });
		colours.insert(colours.end(), 6, colour);
		const olc::vf4d z0{ p.x, p.y - size, p.z - size };
		const olc::vf4d z1{ p.x, p.y - size, p.z + size };
		const olc::vf4d z2{ p.x, p.y + size, p.z + size };
		const olc::vf4d z3{ p.x, p.y + size, p.z - size };
		points.insert(points.end(), { z0, z1, z2, z0, z2, z3 });
		colours.insert(colours.end(), 6, colour);
	}
	draw.SetBlendMode(olc::BlendMode::Alpha);
	draw.Mesh(olc::Structure::List, points, colours);
}

void NetballGame::StartLooseBall(const olc::vf2d& horizontalVelocity,
	const float verticalVelocity)
{
	const bool wasLooseBall = looseBall;
	looseBall = true;
	looseBallHorizontalVelocity = horizontalVelocity;
	looseBallVerticalVelocity = verticalVelocity;
	if (!wasLooseBall)
	{
		looseBallOriginTeamHome = players[ballCarrier].homeTeam;
		looseBallChasers.fill(netball::PlayerCount);
		looseBallBlockedChasers.fill(netball::PlayerCount);
		looseBallChaseBlockedFrames.fill(frameNumber);
		LogEvent("LOOSE_BALL_START position=(" +
			std::to_string(ballPosition.x) + "," +
			std::to_string(ballPosition.y) + "," +
			std::to_string(ballPosition.z) + ") origin_team=" +
			(looseBallOriginTeamHome ? "HOME" : "AWAY") +
			" velocity=(" + std::to_string(horizontalVelocity.x) + "," +
			std::to_string(verticalVelocity) + "," +
			std::to_string(horizontalVelocity.y) + ")");
	}
}

std::vector<std::size_t> NetballGame::CountNearbyDefenders(
	const std::size_t index, const float radius) const
{
	std::vector<std::pair<float, std::size_t>> found;
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (players[i].homeTeam == players[index].homeTeam)
			continue;
		const float dx = players[i].position.x - players[index].position.x;
		const float dz = players[i].position.z - players[index].position.z;
		const float distance = std::sqrt(dx * dx + dz * dz);
		if (distance <= radius)
			found.push_back({ distance, i });
	}
	std::sort(found.begin(), found.end());
	std::vector<std::size_t> result;
	for (const auto& entry : found)
		result.push_back(entry.second);
	return result;
}

float NetballGame::ComputeDoubleTeam(std::size_t& defenderCount) const
{
	float drop = 0.0f;
	defenderCount = 0;
	for (const std::size_t end : { ballCarrier, passReceiver })
	{
		if (end >= players.size())
			continue;
		const std::vector<std::size_t> nearby =
			CountNearbyDefenders(end, DoubleTeamRadius);
		if (nearby.size() >= 2)
		{
			defenderCount = std::max(defenderCount, nearby.size());
			const float defence = 0.5f * float(
				profiles[nearby[0]].defence + profiles[nearby[1]].defence);
			drop = std::max(drop, 0.30f * defence / 3.0f);
		}
	}
	return drop;
}

void NetballGame::EnforceDefenderLock()
{
	if (!defenderLockActive || passInFlight || looseBall ||
		ballCarrier >= players.size() || throwInPending)
		return;
	const olc::vf4d centre = players[ballCarrier].position;
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (players[i].homeTeam == players[ballCarrier].homeTeam ||
			std::find(lockedDefenders.begin(), lockedDefenders.end(), i) !=
				lockedDefenders.end())
			continue;
		float dx = players[i].position.x - centre.x;
		float dz = players[i].position.z - centre.z;
		const float distance = std::sqrt(dx * dx + dz * dz);
		if (distance >= DoubleTeamRadius)
			continue;
		if (distance < 0.001f)
		{
			dx = 0.0f;
			dz = 1.0f;
		}
		else
		{
			dx /= distance;
			dz /= distance;
		}
		players[i].position.x = centre.x + dx * DoubleTeamRadius;
		players[i].position.z = centre.z + dz * DoubleTeamRadius;
	}
}

olc::vf2d NetballGame::PredictLooseBallLanding() const
{
	olc::vf4d position = ballPosition;
	olc::vf2d velocity = looseBallHorizontalVelocity;
	float vertical = looseBallVerticalVelocity;
	constexpr float step = 0.01f;
	for (int i = 0; i < 600; ++i)
	{
		position.x += velocity.x * step;
		position.z += velocity.y * step;
		velocity *= std::exp(-0.8f * step);
		vertical -= 9.8f * step;
		position.y += vertical * step;
		if (position.y <= netball::BallRadius)
			break;
	}
	return { position.x, position.z };
}

void NetballGame::DrawPlayerPanel()
{
	if (gameOver || selectedPlayer >= players.size() ||
		selectedPlayer >= profiles.size())
		return;
	const netball::Player& player = players[selectedPlayer];
	const netball::PlayerProfile& profile = profiles[selectedPlayer];
	const olc::vf2d screen = UiScreenSize();
	const float panelHeight = 190.0f;
	const olc::vf2d origin{ 30.0f, screen.y - panelHeight - 30.0f };
	UiFilledRect(origin, { 560.0f, panelHeight }, olc::Pixel(0, 0, 0, 170));
	const olc::Pixel teamColour = player.homeTeam
		? netball::HomeTeamColour : netball::AwayTeamDisplayColour;
	UiFilledRect(origin, { 8.0f, panelHeight }, teamColour);
	float textLeft = origin.x + 28.0f;
	if (portraitSheetLoaded)
	{
		const int cell = std::clamp(profile.portrait, 0, 99);
		const float u0 = float(cell % 10) / 10.0f, v0 = float(cell / 10) / 10.0f;
		const olc::ImageRegion region(portraitSheet, { u0, v0 },
			{ u0 + 0.1f, v0 }, { u0, v0 + 0.1f }, { u0 + 0.1f, v0 + 0.1f });
		draw.SetBlendMode(olc::BlendMode::Alpha);
		draw.ImageRect(region, (origin + olc::vf2d{ 24.0f, 20.0f }) * uiScale,
			olc::vf2d{ 150.0f, 150.0f } * uiScale);
		textLeft += 170.0f;
	}
	UiString({ textLeft, origin.y + 18.0f }, profile.name,
		olc::Colour::WHITE, { 5.0f, 5.0f });
	UiString({ textLeft, origin.y + 76.0f },
		std::string(netball::Positions[player.role].abbreviation) + "  " +
			netball::Positions[player.role].name,
		olc::Pixel(255, 215, 90), { 3.0f, 3.0f });
	UiString({ textLeft, origin.y + 122.0f },
		"G " + std::to_string(profile.goals) +
		"  A " + std::to_string(profile.assists) +
		"  R " + std::to_string(profile.rebounds) +
		"  T " + std::to_string(profile.turnovers),
		olc::Colour::WHITE, { 3.0f, 3.0f });
}

void NetballGame::TryPickupLooseBall()
{
	if (!looseBall)
	{
		reboundMarkerActive = false;
		return;
	}
	if (ballPosition.y > 4.5f || reboundLockout > 0.0f)
		return;

	std::size_t forcedPlayer = netball::PlayerCount;
	if (reboundMarkerActive)
	{
		// Nobody can claim the ball until it comes down on the marker.
		if (ballPosition.y > netball::BallRadius + 0.03f)
			return;
		reboundMarkerActive = false;
		std::size_t closest = netball::PlayerCount;
		float closestSquared = 1.0f;
		for (std::size_t i = 0; i < players.size(); ++i)
		{
			const float dx = players[i].position.x - ballPosition.x;
			const float dz = players[i].position.z - ballPosition.z;
			if (dx * dx + dz * dz <= closestSquared)
			{
				closestSquared = dx * dx + dz * dz;
				closest = i;
			}
		}
		std::uniform_real_distribution<float> chance(0.0f, 1.0f);
		if (closest < players.size() && chance(randomEngine) < 0.70f)
			forcedPlayer = closest;
		else
		{
			reboundLockout = 0.5f;
			return;
		}
	}

	constexpr float pickupRadiusSquared = 1.8f * 1.8f;
	std::size_t pickupPlayer = selectedPlayer;
	float pickupDistanceSquared = pickupPlayer < players.size()
		? (players[pickupPlayer].position.x - ballPosition.x) *
			(players[pickupPlayer].position.x - ballPosition.x) +
			(players[pickupPlayer].position.z - ballPosition.z) *
			(players[pickupPlayer].position.z - ballPosition.z)
		: pickupRadiusSquared;

	for (std::size_t i = 0; i < players.size() && forcedPlayer >= players.size(); ++i)
	{
		const bool aiControlled = !players[i].homeTeam || aiAttackMode;
		if (!aiControlled)
			continue;

		const float dx = players[i].position.x - ballPosition.x;
		const float dz = players[i].position.z - ballPosition.z;
		const float distanceSquared = dx * dx + dz * dz;
		if (distanceSquared < pickupDistanceSquared)
		{
			pickupPlayer = i;
			pickupDistanceSquared = distanceSquared;
		}
	}
	if (forcedPlayer < players.size())
	{
		pickupPlayer = forcedPlayer;
		pickupDistanceSquared = 0.0f;
	}

	if (pickupPlayer >= players.size() ||
		pickupDistanceSquared > pickupRadiusSquared)
		return;

	looseBall = false;
	looseBallChasers.fill(netball::PlayerCount);
	looseBallBlockedChasers.fill(netball::PlayerCount);
	looseBallVerticalVelocity = 0.0f;
	SetBallCarrier(pickupPlayer, forcedPlayer < players.size()
		? "REBOUND_WON" : "LOOSE_BALL_RECOVERED");
	LogEvent("LOOSE_BALL_PICKUP team=" +
		std::string(players[pickupPlayer].homeTeam ? "HOME" : "AWAY") +
		" player=" + netball::Positions[players[pickupPlayer].role].abbreviation);
	aiNoReceiverLogged = false;
	SelectPlayerForPossession();
	passReceiver = pickupPlayer;
	controlledPassOption = -1;
	playerVelocity = {};
	RefreshPassTargets();
	movementMessage = "REBOUND";
	messageTime = 1.5f;
}

void NetballGame::DrawScene()
{
	draw.Clear(Shade(70, 88, 105));

	draw.SetViewMatrix(camera.ViewMatrix());

	olc::mf4d sideView;
	sideView.rotateY(Pi * 0.5f);
	draw.SetModelMatrix(sideView);
	draw.SetCullMode(olc::CullMode::None);

	if (floorTextureLoaded)
		netball::DrawFloor(draw, floorTexture);
	netball::DrawCourt(draw, courtTexture);
	if (SuperShotActive() || debugMode)
		netball::DrawSuperShotZone(draw);
	netball::DrawGoalStructures(draw, postTexture);
	if (reboundMarkerActive && looseBall)
	{
		std::vector<olc::vf4d> disc;
		std::vector<olc::Pixel> discColours;
		constexpr int segments = 20;
		constexpr float discRadius = 0.35f;
		for (int s = 0; s < segments; ++s)
		{
			const float a0 = 2.0f * Pi * float(s) / segments;
			const float a1 = 2.0f * Pi * float(s + 1) / segments;
			disc.push_back({ reboundMarker.x, 0.03f, reboundMarker.y });
			disc.push_back({ reboundMarker.x + discRadius * std::cos(a0), 0.03f,
				reboundMarker.y + discRadius * std::sin(a0) });
			disc.push_back({ reboundMarker.x + discRadius * std::cos(a1), 0.03f,
				reboundMarker.y + discRadius * std::sin(a1) });
			discColours.insert(discColours.end(), 3, olc::Pixel(0, 0, 0, 230));
		}
		draw.SetBlendMode(olc::BlendMode::Alpha);
		draw.Mesh(olc::Structure::List, disc, discColours);
	}
	if (debugMode)
		netball::DrawDebugZoneLines(draw);
	const std::size_t zPassTarget = passTargets[0];
	const std::size_t xPassTarget = passTargets[1];
	const std::size_t cPassTarget = passTargets[2];
	if (passInFlight && !shotInFlight)
		netball::DrawPassDestination(draw, passDestination,
			players[ballCarrier].homeTeam
				? netball::HomeTeamColour : netball::AwayTeamDisplayColour);
	std::vector<netball::Player> substitutes;
	for (std::size_t team = 0; team < 2; ++team)
	{
		const bool home = team == 1;
		const float amplitude = cheerRemaining[team] > 0.0f ? 1.1f : 0.25f;
		for (std::size_t k = 0; k < 2; ++k)
		{
			netball::Player sub;
			sub.homeTeam = home;
			sub.role = 0;
			// Second sub in each pair jumps half a cycle behind the first.
			const float phase = std::fmod(
				cheerPhase[team * 2 + k] + 0.5f * float(k), 1.0f);
			sub.position = { -(netball::CourtHalfWidth + 1.6f),
				amplitude * std::fabs(std::sin(phase * Pi)),
				(home ? 1.0f : -1.0f) * (9.2f + 2.0f * float(k)), 0.0f };
			substitutes.push_back(sub);
		}
	}
	netball::DrawPlayers(draw, players, selectedPlayer,
		zPassTarget, xPassTarget, cPassTarget,
		playerTextureLoaded ? playerTextures.data() : nullptr, substitutes,
		playerHair.data());
	if (debugMode || (!demoActive && !IsAIAttacking()))
		netball::DrawPassTargetLabels(draw, players, passTargets,
			ActivePlatform(), ScreenSize());
	netball::DrawBall(draw, ballPosition, ballTexture, ballSpinAngle,
		passInFlight || looseBall);
	DrawGoalParticles();
	DrawHud();
	DrawPlayerPanel();
	DrawShootingMeter();
}

void NetballGame::DrawHud()
{
	const std::string scoreboard = "HOME  " + std::to_string(homeScore) +
		"   -   " + std::to_string(awayScore) + "  AWAY";
	const olc::vf2d scoreboardScale{ 6.0f, 6.0f };
	const olc::vf2d scoreboardSize = UiTextSize(scoreboard, scoreboardScale);
	const olc::vf2d screen = UiScreenSize();
	const std::string turnovers = "TURNOVERS  " +
		std::to_string(homeTurnovers) + "  -  " +
		std::to_string(awayTurnovers);
	const olc::vf2d turnoversScale{ 2.4f, 2.4f };
	const olc::vf2d turnoversSize = UiTextSize(turnovers, turnoversScale);
	const int clockTotal = int(std::ceil(clockRemaining));
	char clockText[16];
	std::snprintf(clockText, sizeof(clockText), "%02d:%02d",
		clockTotal / 60, clockTotal % 60);
	const olc::vf2d clockScale{ 4.0f, 4.0f };
	const olc::vf2d clockSize = UiTextSize(clockText, clockScale);
	const float statsWidth = std::max({ scoreboardSize.x, turnoversSize.x,
		clockSize.x });
	const float boardHeight = scoreboardSize.y + turnoversSize.y +
		clockSize.y + 40.0f;
	const olc::vf2d scoreboardPosition{
		(screen.x - scoreboardSize.x) * 0.5f,
		250.0f - boardHeight
	};
	const float statsTop = scoreboardPosition.y + scoreboardSize.y + 8.0f;
	UiFilledRect({
		(screen.x - statsWidth) * 0.5f - 12.0f,
		scoreboardPosition.y - 8.0f
	}, {
		statsWidth + 24.0f,
		boardHeight
	}, Shade(10, 22, 35));
	UiString(scoreboardPosition, scoreboard, olc::Colour::WHITE,
		scoreboardScale);
	UiString({
		(screen.x - turnoversSize.x) * 0.5f,
		statsTop
	}, turnovers, olc::Colour::WHITE, turnoversScale);
	UiString({
		(screen.x - clockSize.x) * 0.5f,
		statsTop + turnoversSize.y + 8.0f
	}, clockText,
		clockRemaining <= FinalSeconds ? olc::Pixel(255, 60, 60) : olc::Colour::WHITE,
		clockScale);

	const olc::vf2d hudScale{ 3.0f, 3.0f };
	if (debugMode)
	{
		char fpsText[24];
		std::snprintf(fpsText, sizeof(fpsText), "%d FPS", int(GetFPS()));
		const olc::vf2d fpsScale{ 9.0f, 9.0f };
		const olc::vf2d fpsSize = UiTextSize(fpsText, fpsScale);
		UiString({ screen.x - fpsSize.x - 24.0f, screen.y - fpsSize.y - 24.0f },
			fpsText, olc::Pixel(120, 255, 130), fpsScale);
	}
	if (debugMode)
	{
		UiString({ 18.0f, 16.0f },
			"NETBALL  /  ZONE TRAINING", olc::Colour::WHITE, hudScale);
		UiString({ 18.0f, 52.0f },
			std::string("Move: WASD / Arrows / stick    TAB/bumper: cycle receiver    O: home AI ") +
				(aiAttackMode ? "ON" : "OFF") + "    K: debug    P: pause",
			olc::Colour::WHITE, hudScale);
		UiString({ 18.0f, 88.0f },
			"A/D along court    W/S across court    Space/trigger: shoot    Z X C V / A B X Y: pass",
			olc::Colour::WHITE, hudScale);
		constexpr std::array<const char*, 4> keyNames{ "Z", "X", "C", "V" };
		constexpr std::array<const char*, 4> padNames{ "A", "B", "X", "Y" };
		for (std::size_t option = 0; option < passTargets.size(); ++option)
		{
			const std::size_t target = passTargets[option];
			const std::string name = usingGamepad ? padNames[option] : keyNames[option];
			UiString({ 18.0f, 132.0f + 36.0f * float(option) },
				name + " pass option: " + (target < netball::PlayerCount
					? std::string(netball::Positions[players[target].role].abbreviation) +
						" (" + netball::Positions[players[target].role].name + ")"
					: std::string("no teammate")),
				olc::Colour::WHITE, hudScale);
		}
		UiString({ screen.x - 650.0f, 16.0f },
			"POSITION AREAS\nGS: attacking third\nGA: centre + attacking third\n"
			"WA: centre + attack (no circle)\nC: any third (no circles)\n"
			"WD: centre + defence (no circle)\nGD: centre + defending third\n"
			"GK: defending third", olc::Colour::WHITE, hudScale);
		UiString({ screen.x - 650.0f, 300.0f },
			"SHOTS MADE/MISSED\nHome: " +
			std::to_string(homeShots - homeMissedShots) + "/" +
			std::to_string(homeMissedShots) + "\nAway: " +
			std::to_string(awayShots - awayMissedShots) + "/" +
			std::to_string(awayMissedShots), olc::Colour::WHITE, hudScale);
	}

	if (debugMode)
	{
		const netball::Player& selected = players[selectedPlayer];
		const std::string selectedLabel = std::string(
			controlledPassOption >= 0 ? "Controlling receiver: " : "Selected: ") +
			netball::Positions[selected.role].abbreviation + " - " +
			netball::Positions[selected.role].name;
		UiString({ 18.0f, screen.y - 76.0f },
			selectedLabel, olc::Colour::WHITE, hudScale);
		UiString({ 18.0f, screen.y - 40.0f },
			"Yellow: home    Black: away    Powered by olcPixelGameEngine3 - OneLoneCoder.com",
			olc::Colour::WHITE, hudScale);
	}

	if (debugMode && messageTime > 0.0f)
		UiString({ 18.0f, screen.y - 120.0f }, movementMessage,
			Shade(255, 196, 92), { 3.0f, 3.0f });

	if (paused)
		DrawPauseMenu();
}

bool NetballGame::UpdatePauseMenu()
{
	constexpr std::size_t itemCount = 4;
	// Pad stick/d-pad y is positive when pushed up.
	const auto edge = [](const float now, const float before, const float sign)
	{
		return now * sign > 0.5f && before * sign <= 0.5f;
	};
	const bool up = keyboard.GetKey(olc::Key::UP).bPressed ||
		keyboard.GetKey(olc::Key::W).bPressed ||
		edge(pad.move.y, previousPadMove.y, 1.0f);
	const bool down = keyboard.GetKey(olc::Key::DOWN).bPressed ||
		keyboard.GetKey(olc::Key::S).bPressed ||
		edge(pad.move.y, previousPadMove.y, -1.0f);
	const bool right = keyboard.GetKey(olc::Key::RIGHT).bPressed ||
		keyboard.GetKey(olc::Key::D).bPressed ||
		edge(pad.move.x, previousPadMove.x, 1.0f);
	const bool left = keyboard.GetKey(olc::Key::LEFT).bPressed ||
		keyboard.GetKey(olc::Key::A).bPressed ||
		edge(pad.move.x, previousPadMove.x, -1.0f);
	previousPadMove = pad.move;

	if (up)
		pauseSelection = (pauseSelection + itemCount - 1) % itemCount;
	if (down)
		pauseSelection = (pauseSelection + 1) % itemCount;

	const bool activate = keyboard.GetKey(olc::Key::ENTER).bPressed ||
		keyboard.GetKey(olc::Key::SPACE).bPressed ||
		keyboard.GetKey(olc::Key::X).bPressed || pad.passPressed[0];
	const std::size_t count = netball::Difficulties.size();
	if (pauseSelection == 2 && (left || right || activate))
	{
		difficulty = (difficulty + count + (left ? count - 1 : 1)) % count;
		LogEvent(std::string("DIFFICULTY ") + Difficulty().name);
		netball::SetPlayerCollisionScale(Difficulty().collisionScale);
	}
	else if (activate && pauseSelection == 0)
		paused = false;
	else if (activate && pauseSelection == 1)
		StartMatch();
	else if (activate && pauseSelection == 3)
		return false;

	if (pad.startPressed)
		StartMatch();
	if (pad.passPressed[1])
		paused = false;
	return true;
}

void NetballGame::DrawPauseMenu()
{
	const olc::vf2d screen = UiScreenSize();
	const std::array<std::string, 4> items{
		"Resume", "Restart",
		std::string("Difficulty: ") + Difficulty().name, "Exit" };
	const olc::vf2d titleScale{ 5.0f, 5.0f };
	const olc::vf2d itemScale{ 4.0f, 4.0f };
	const float lineHeight = UiTextSize("Ag", itemScale).y * 1.5f;
	float width = UiTextSize("PAUSED", titleScale).x;
	for (const std::string& item : items)
		width = std::max(width, UiTextSize("> " + item, itemScale).x);
	const float height = lineHeight * float(items.size() + 2);
	const olc::vf2d topLeft{ (screen.x - width) * 0.5f, (screen.y - height) * 0.5f };
	UiFilledRect(topLeft - olc::vf2d{ 40.0f, 30.0f },
		olc::vf2d{ width + 80.0f, height + 90.0f }, olc::Pixel(0, 0, 0, 200));
	UiString({ (screen.x - UiTextSize("PAUSED", titleScale).x) * 0.5f, topLeft.y },
		"PAUSED", olc::Colour::WHITE, titleScale);
	for (std::size_t i = 0; i < items.size(); ++i)
	{
		const bool selected = i == pauseSelection;
		UiString({ topLeft.x, topLeft.y + lineHeight * float(i + 2) },
			(selected ? "> " : "  ") + items[i],
			selected ? Shade(255, 205, 70) : olc::Colour::WHITE, itemScale);
	}
}
