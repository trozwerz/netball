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
	if (CreateImageFromFile(playerTextures[0], "assets/player.png", playerConfig))
	{
		olc::Image& playerTexture = playerTextures[0];
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
		playerTexture.BindCPU();

		// Variants 1 (brown) and 2 (black): recolour the saturated yellow
		// hair pixels, keeping their brightness so the strands stay visible.
		const float hairColours[2][3] = {
			{ 125.0f, 78.0f, 40.0f }, { 48.0f, 42.0f, 46.0f }
		};
		bool variantsOk = true;
		for (int variant = 1; variant <= 2 && variantsOk; ++variant)
		{
			variantsOk = CreateImage(playerTextures[variant],
				playerTexture.Size(), playerConfig);
			if (!variantsOk)
				break;
			olc::Pixel* destination = playerTextures[variant].Data();
			for (int i = 0; i < width * height; ++i)
			{
				olc::Pixel p = pixels[i];
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
					const auto tint = [&](const float base)
					{
						return uint8_t(std::clamp(base * brightness, 0.0f, 255.0f));
					};
					p = olc::Pixel(tint(hairColours[variant - 1][0]),
						tint(hairColours[variant - 1][1]),
						tint(hairColours[variant - 1][2]), p.a);
				}
				destination[i] = p;
			}
			playerTextures[variant].BindCPU();
		}
		playerTextureLoaded = variantsOk;
	}
	else
		std::cerr << "Unable to load player texture: assets/player.png\n";

	if (!CreateImageFromFile(courtTexture, "assets/court_blue.png"))
	{
		std::cerr << "Unable to load court image: assets/court_blue.png\n";
		return false;
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
		LogEvent(netball::FillMonitor(3) ? "WINDOW fullscreen display=3"
			: "WINDOW display 3 not found");
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
		DrawScene();
		DrawGameOver();
		if (confirmPressed)
			StartMatch();
		return true;
	}

	gameTime += elapsedTime;
	if (!demoActive)
	{
		clockRemaining -= elapsedTime *
			(clockRemaining <= FinalSeconds ? 1.0f : ClockSpeed);
		if (clockRemaining <= 0.0f)
		{
			clockRemaining = 0.0f;
			gameOver = true;
			LogEvent("GAME_OVER score=" + std::to_string(homeScore) + "-" +
				std::to_string(awayScore));
		}
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

void NetballGame::DrawConfirmPrompt(const std::string& action)
{
	const olc::vf2d promptScale{ 6.0f, 6.0f };
	olc::Image* icon = usingGamepad ? netball::ButtonIcon(consolePlatform, 0) : nullptr;
	const std::string text = icon ? action : "SPACE " + action;
	const olc::vf2d textSize = UiTextSize(text, promptScale);
	const float iconSize = icon ? textSize.y * 1.3f : 0.0f;
	const float gap = icon ? 16.0f : 0.0f;
	const olc::vf2d totalSize{ iconSize + gap + textSize.x, std::max(textSize.y, iconSize) };
	const olc::vf2d uiScreen = UiScreenSize();
	const olc::vf2d origin{
		(uiScreen.x - totalSize.x) * 0.5f, uiScreen.y - totalSize.y - 80.0f };
	UiFilledRect(origin - olc::vf2d{ 20.0f, 12.0f },
		totalSize + olc::vf2d{ 40.0f, 24.0f }, olc::Pixel(0, 0, 0, 170));
	if (icon)
	{
		draw.SetBlendMode(olc::BlendMode::Alpha);
		draw.ImageRect(*icon, origin * uiScale,
			olc::vf2d{ iconSize, iconSize } * uiScale);
	}
	UiString({ origin.x + iconSize + gap, origin.y + (totalSize.y - textSize.y) * 0.5f },
		text, olc::Colour::WHITE, promptScale);
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
	shotDistanceMustMiss = randomChance(randomEngine) < distanceMissChance;
	shotMustMiss = forcedAIMiss || shotDistanceMustMiss;
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
		std::abs(meterError) <= (userShot ? level.userMeterWindow : 0.03f);
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
		colour = olc::Pixel(56, 190, 92);
	else if (awayScore > homeScore)
		colour = olc::Pixel(232, 81, 92);
	UiString({ (screen.x - titleSize.x) * 0.5f, topLeft.y + 30.0f },
		result, colour, titleScale);
	UiString({ (screen.x - scoreSize.x) * 0.5f,
		topLeft.y + 60.0f + titleSize.y },
		score, olc::Colour::WHITE, scoreScale);
	DrawConfirmPrompt("to continue");
}

void NetballGame::DrawShootingMeter()
{
	if (!shootingMeterActive && shootingMeterDisplayTime <= 0.0f)
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
		shootingMeterGuaranteedShot
			? Shade(24, 150, 62) : Shade(12, 30, 78));

	const float markX = barPosition.x + ShootingMeterMark * barWidth;
	const float markHalfWidth =
		std::max(4.0f, Difficulty().userMeterWindow * barWidth);
	UiFilledRect({ markX - markHalfWidth, barPosition.y + 8.0f },
		{ markHalfWidth * 2.0f, barHeight - 8.0f },
		shootingMeterGuaranteedShot
			? Shade(120, 255, 130) : Shade(255, 215, 55));

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
		const float dropChance = std::min(0.95f, homeScale * (0.4f * std::min(
			0.30f, 0.05f + defenderProximity * 0.15f +
				(defenderOnPassLine ? 0.10f : 0.0f)) + distanceDrop +
			lengthDrop));
		std::uniform_real_distribution<float> randomChance(0.0f, 1.0f);
		if (randomChance(randomEngine) < dropChance)
		{
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

		SetBallCarrier(passReceiver, "PASS_CAUGHT");
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

	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (players[i].homeTeam != throwInTeamHome)
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
	if (homeScored)
		++homeScore;
	else
		++awayScore;

	const olc::Pixel teamColour = homeScored
		? olc::Pixel(56, 190, 92) : olc::Pixel(232, 81, 92);
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
	for (GoalParticle& particle : goalParticles)
	{
		particle.lifetime = std::max(0.0f, particle.lifetime - elapsedTime);
		particle.velocity.y -= 9.8f * elapsedTime;
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
		constexpr float size = 0.09f;
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

void NetballGame::TryPickupLooseBall()
{
	if (!looseBall || ballPosition.y > 4.5f)
		return;

	constexpr float pickupRadiusSquared = 1.8f * 1.8f;
	std::size_t pickupPlayer = selectedPlayer;
	float pickupDistanceSquared = pickupPlayer < players.size()
		? (players[pickupPlayer].position.x - ballPosition.x) *
			(players[pickupPlayer].position.x - ballPosition.x) +
			(players[pickupPlayer].position.z - ballPosition.z) *
			(players[pickupPlayer].position.z - ballPosition.z)
		: pickupRadiusSquared;

	for (std::size_t i = 0; i < players.size(); ++i)
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

	if (pickupPlayer >= players.size() ||
		pickupDistanceSquared > pickupRadiusSquared)
		return;

	looseBall = false;
	looseBallChasers.fill(netball::PlayerCount);
	looseBallBlockedChasers.fill(netball::PlayerCount);
	looseBallVerticalVelocity = 0.0f;
	SetBallCarrier(pickupPlayer, "LOOSE_BALL_RECOVERED");
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
	draw.Clear(Shade(24, 37, 48));

	draw.SetViewMatrix(camera.ViewMatrix());

	olc::mf4d sideView;
	sideView.rotateY(Pi * 0.5f);
	draw.SetModelMatrix(sideView);
	draw.SetCullMode(olc::CullMode::None);

	netball::DrawCourt(draw, courtTexture);
	netball::DrawGoalStructures(draw);
	if (debugMode)
		netball::DrawDebugZoneLines(draw);
	const std::size_t zPassTarget = passTargets[0];
	const std::size_t xPassTarget = passTargets[1];
	const std::size_t cPassTarget = passTargets[2];
	if (passInFlight && !shotInFlight)
		netball::DrawPassDestination(draw, passDestination,
			players[ballCarrier].homeTeam
				? olc::Pixel(56, 190, 92) : olc::Pixel(232, 81, 92));
	netball::DrawPlayers(draw, players, selectedPlayer,
		zPassTarget, xPassTarget, cPassTarget,
		playerTextureLoaded ? playerTextures.data() : nullptr);
	if (debugMode || (!demoActive && !IsAIAttacking()))
		netball::DrawPassTargetLabels(draw, players, passTargets,
			ActivePlatform(), ScreenSize());
	netball::DrawBall(draw, ballPosition, ballTexture, ballSpinAngle,
		passInFlight || looseBall);
	DrawGoalParticles();
	DrawHud();
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
	const olc::vf2d turnoversScale{ 2.0f, 2.0f };
	const olc::vf2d turnoversSize = UiTextSize(turnovers, turnoversScale);
	const float statsWidth = std::max(scoreboardSize.x, turnoversSize.x);
	const float statsHeight = turnoversSize.y;
	// Raised by one box height from the original 250.
	const olc::vf2d scoreboardPosition{
		(screen.x - scoreboardSize.x) * 0.5f,
		250.0f - (scoreboardSize.y + statsHeight + 28.0f)
	};
	const float statsTop = scoreboardPosition.y + scoreboardSize.y + 8.0f;
	UiFilledRect({
		(screen.x - statsWidth) * 0.5f - 12.0f,
		scoreboardPosition.y - 8.0f
	}, {
		statsWidth + 24.0f,
		scoreboardSize.y + statsHeight + 28.0f
	}, Shade(10, 22, 35));
	UiString(scoreboardPosition, scoreboard, olc::Colour::WHITE,
		scoreboardScale);
	UiString({
		(screen.x - turnoversSize.x) * 0.5f,
		statsTop
	}, turnovers, olc::Colour::WHITE, turnoversScale);

	const olc::vf2d hudScale{ 3.0f, 3.0f };
	if (!demoActive)
	{
		const float remaining = clockRemaining;
		const int total = int(std::ceil(remaining));
		char clockText[16];
		std::snprintf(clockText, sizeof(clockText), "%02d:%02d",
			total / 60, total % 60);
		const olc::vf2d clockScale{ 6.0f, 6.0f };
		const olc::vf2d clockSize = UiTextSize(clockText, clockScale);
		UiFilledRect({ 6.0f, 4.0f }, clockSize + olc::vf2d{ 24.0f, 16.0f },
			Shade(10, 22, 35));
		UiString({ 18.0f, 12.0f }, clockText,
			remaining <= FinalSeconds ? olc::Pixel(255, 60, 60) : olc::Colour::WHITE,
			clockScale);
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
			"Green: home    Red: away    Powered by olcPixelGameEngine3 - OneLoneCoder.com",
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
