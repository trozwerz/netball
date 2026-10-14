#pragma once

#include "Camera.h"
#include "Difficulty.h"
#include "ButtonIcons.h"
#include "Gamepad.h"
#include "Players.h"
#include "PlayerProfiles.h"
#include "olcPixelGameEngine3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <random>
#include <string>
#include <vector>

class NetballGame final : public olc::PixelGameEngine
{
	struct PositionSnapshot
	{
		float time = 0.0f;
		std::array<olc::vf4d, netball::PlayerCount> positions{};
	};

	struct GoalParticle
	{
		olc::vf4d position{};
		olc::vf4d velocity{};
		olc::Pixel colour = olc::Colour::WHITE;
		float lifetime = 0.0f;
		float accelerationY = -9.8f;
		float size = 0.09f;
	};

public:
	NetballGame();

	bool OnUserCreate() override;
	bool OnUserUpdate(float elapsedTime) override;

private:
	netball::Teams players{};
	std::array<olc::vf4d, netball::PlayerCount> startingPositions{};
	olc::Image courtTexture;
	olc::Image floorTexture;
	olc::Image postTexture;
	bool floorTextureLoaded = false;
	olc::Image splashImage;
	olc::Image ballTexture;
	olc::Image playerBase;
	std::array<olc::Pixel, 18> playerHair{};
	std::array<olc::Image, 18> playerTextures;
	bool playerTextureLoaded = false;
	float ballSpinAngle = 0.0f;
	bool splashActive = true;
	bool demoActive = false;
	netball::Camera camera;
	void StartMatch();
	float uiScale = 1.0f;
	olc::mf4d projection;
	olc::vf2d playerVelocity{};
	std::array<olc::vf2d, netball::PlayerCount> teammateDirections{};
	std::array<olc::vf2d, netball::PlayerCount> smoothedDirections{};
	std::array<float, netball::PlayerCount> directionChangeTimers{};
	std::array<bool, netball::PlayerCount> separatingFromMarker{};
	std::array<bool, netball::PlayerCount> escapingCourtEdge{};
	std::array<olc::vf2d, netball::PlayerCount> courtEdgeEscapeDirections{};
	std::array<olc::vf4d, netball::PlayerCount> previousFramePositions{};
	std::array<olc::vf4d, netball::PlayerCount> attackerProgressPositions{};
	std::array<float, netball::PlayerCount> stationaryDurations{};
	std::array<float, netball::PlayerCount> defenderClipDurations{};
	std::mt19937 randomEngine{ std::random_device{}() };
	std::deque<PositionSnapshot> positionHistory;
	std::vector<GoalParticle> goalParticles;
	std::ofstream debugLog;
	float gameTime = 0.0f;
	// 15:00 on the clock runs down 5x faster than real time, slowing to real
	// time for the final 30 seconds.
	static constexpr float ClockSpeed = 5.0f;
	static constexpr float MatchClockSeconds = 15.0f * 60.0f;
	static constexpr float FinalSeconds = 30.0f;
	float clockRemaining = MatchClockSeconds;
	bool gameOver = false;
	static constexpr float GameOverHoldSeconds = 1.0f;
	float gameOverHold = 0.0f;
	void DrawGameOver();
	std::uint64_t frameNumber = 0;
	std::size_t homeScore = 0;
	std::size_t awayScore = 0;
	std::size_t homeTurnovers = 0;
	std::vector<netball::PlayerProfile> profiles;
	olc::Image portraitSheet;
	bool portraitSheetLoaded = false;
	// Players within the 0.5 m double-team radius of the ball carrier.
	std::vector<std::size_t> doubleTeamed;
	std::size_t loggedDoubleTeam = static_cast<std::size_t>(-1);
	float throwDoubleTeamDrop = 0.0f;
	std::size_t throwDoubleTeamCount = 0;
	float ComputeDoubleTeam(std::size_t& defenderCount) const;
	static constexpr float DoubleTeamRadius = 1.5f;
	std::vector<std::size_t> CountNearbyDefenders(std::size_t index, float radius) const;
	// Defenders already within reach when the receiver caught the ball; nobody
	// else may close in until the ball is released.
	std::vector<std::size_t> lockedDefenders;
	bool defenderLockActive = false;
	void EnforceDefenderLock();
	std::size_t lastReceiver = netball::PlayerCount;
	std::array<std::size_t, 2> assistChain{ netball::PlayerCount, netball::PlayerCount };
	bool reboundPending = false;
	bool dropPending = false;
	bool dropTeamHome = true;
	bool reboundMarkerActive = false;
	olc::vf2d reboundMarker{};
	float reboundLockout = 0.0f;
	void ResolveRebound();
	olc::vf2d PredictLooseBallLanding() const;
	void DrawPlayerPanel();
	std::size_t awayTurnovers = 0;
	std::size_t homeShots = 0;
	std::size_t awayShots = 0;
	std::size_t homeMissedShots = 0;
	std::size_t awayMissedShots = 0;
	// Pass options Z, X, C, V (keyboard) / A, B, X, Y (gamepad).
	std::array<std::size_t, 4> passTargets{
		netball::PlayerCount, netball::PlayerCount,
		netball::PlayerCount, netball::PlayerCount
	};
	netball::GamepadState pad;
	bool usingGamepad = false;
	bool padSeen = false;
	// PlayStation by default; F2 cycles PlayStation / Xbox / Switch icons.
	netball::ButtonPlatform consolePlatform = netball::ButtonPlatform::PlayStation;
	netball::ButtonPlatform ActivePlatform() const;
	void DrawConfirmPrompt(const std::string& action, float holdProgress = -1.0f);
	olc::vf4d ballPosition{};
	olc::vf4d passStart{};
	olc::vf4d passEnd{};
	olc::vf2d passDestination{};
	olc::vf4d catchBlendFrom{};
	float catchBlendRemaining = 0.0f;
	float catchBlendDuration = 0.2f;
	olc::vf4d previousBallPosition{};
	float passLogTimer = 0.0f;
	std::size_t selectedPlayer = 0;
	std::size_t ballCarrier = 0;
	std::size_t passReceiver = 0;
	int controlledPassOption = -1;
	float passElapsed = 0.0f;
	float passDuration = 0.0f;
	float shotArcHeight = 0.0f;
	float shotResetDelayRemaining = 0.0f;
	float restartHoldRemaining = 0.0f;
	// Substitutes beside the far touchline: index 0 away, 1 home.
	std::array<float, 2> cheerRemaining{};
	std::array<float, 4> cheerPhase{};
	bool shotIsSuper = false;
	float superStreamRemaining = 0.0f;
	olc::vf2d superStreamPosition{};
	bool SuperShotActive() const
	{
		return !gameOver && clockRemaining <= 300.0f;
	}
	olc::vf2d shotHoopCenter{};
	olc::vf2d looseBallHorizontalVelocity{};
	float looseBallVerticalVelocity = 0.0f;
	float throwInPauseRemaining = 0.0f;
	float aiAttackActionDelay = 0.0f;
	float aiShooterPauseRemaining = 0.0f;
	std::size_t aiShooterPauseFor = static_cast<std::size_t>(-1);
	bool passInFlight = false;
	bool shotInFlight = false;
	bool shotInNet = false;
	bool shotScores = false;
	bool shotMustMiss = false;
	bool shotDistanceMustMiss = false;
	bool shotResetPending = false;
	bool looseBall = false;
	bool looseBallOriginTeamHome = true;
	bool throwInPending = false;
	olc::vf4d throwInInsidePosition{};
	bool shootingMeterActive = false;
	bool paused = false;
	std::size_t difficulty = 0;
	std::size_t pauseSelection = 0;
	olc::vf2d previousPadMove{};
	const netball::DifficultySettings& Difficulty() const
	{
		return netball::Difficulties[difficulty];
	}
	bool UpdatePauseMenu();
	void DrawPauseMenu();
	bool aiShotMustMiss = false;
	float shootingMeterPosition = 0.0f;
	float shootingMeterElapsed = 0.0f;
	float shootingMeterSpeed = 0.0f;
	float shootingMeterDisplayTime = 0.0f;
	bool shootingMeterGuaranteedShot = false;
	bool aiAttackMode = false;
	bool debugMode = false;
	bool debugLogErrorReported = false;
	bool aiNoReceiverLogged = false;
	std::array<std::string, netball::PlayerCount> lastMovementBlockReasons{};
	std::array<std::uint64_t, netball::PlayerCount> lastMovementBlockFrames{};
	std::array<std::size_t, 2> looseBallChasers{
		netball::PlayerCount, netball::PlayerCount
	};
	std::array<std::size_t, 2> looseBallBlockedChasers{
		netball::PlayerCount, netball::PlayerCount
	};
	std::array<std::uint64_t, 2> looseBallChaseBlockedFrames{};
	float messageTime = 0.0f;
	std::string movementMessage;

	void UpdatePlayers(float elapsedTime);
	void LogEvent(const std::string& event);
	void LogPlayerPositions();
	void LogMovementBlock(std::size_t playerIndex, const std::string& reason,
		const olc::vf2d& displacement);
	void RegisterTurnover(bool losingTeamHome, const std::string& reason);
	void SetBallCarrier(std::size_t playerIndex, const std::string& reason,
		bool countTurnover = true);
	void UpdateAIAttack(float elapsedTime);
	bool IsAIAttacking() const;
	void SelectPlayerForPossession();
	void UpdatePassing(float elapsedTime);
	void RegisterGoal();
	void UpdateGoalParticles(float elapsedTime);
	void DrawGoalParticles();
	bool HandleBallOutOfBounds(const olc::vf4d& previousPosition);
	void StartThrowIn(bool throwInTeamHome, const olc::vf2d& boundaryPosition);
	void CompleteThrowIn();
	// Easy mode: the user's team always completes passes and shots while
	// trailing by 3 or more.
	bool CatchUpAssistActive(const bool teamIsHome) const
	{
		return teamIsHome && !demoActive && difficulty == 0 &&
			awayScore >= homeScore + 3;
	}
	void StartLooseBall(const olc::vf2d& horizontalVelocity,
		float verticalVelocity);
	void UpdateTeammateMovement(float elapsedTime);
	void UpdateLooseBallChasers(float elapsedTime);
	void UpdateAttackerStuckTimers(float elapsedTime);
	void RecordPlayerPositions();
	olc::vf4d GetDelayedPlayerPosition(std::size_t playerIndex) const;
	void RefreshPassTargets();
	void ChooseRandomDirection(std::size_t playerIndex,
		float minDuration = 0.5f, float maxDuration = 1.5f);
	bool MoveOutOfCourtCorner(std::size_t playerIndex, float distance,
		bool constrainToShootingCircle);
	bool MoveAwayFromDefenders(std::size_t playerIndex,
		const olc::vf2d& preferredDirection, float distance,
		bool constrainToShootingCircle);
	bool IsWithinPassTargetRadius(std::size_t playerIndex,
		const olc::vf4d& position) const;
	bool MovePlayer(std::size_t playerIndex, const olc::vf2d& displacement,
		bool showBlockedMessage, bool constrainToShootingCircle = false);
	void StartPassOption(std::size_t option);
	void StartPassToPlayer(std::size_t playerIndex);
	bool CanShoot() const;
	bool StartShot();
	void ResetAfterShot();
	float GetShootingMeterStartPosition() const;
	float GetShootingMeterSpeed() const;
	void UpdateShootingMeter(float elapsedTime);
	void TryPickupLooseBall();
	void DrawShootingMeter();
	void UiFilledRect(const olc::vf2d& position, const olc::vf2d& size,
		const olc::Pixel colour);
	void UiString(const olc::vf2d& position, const std::string& text,
		const olc::Pixel colour, const olc::vf2d& scale);
	olc::vf2d UiTextSize(const std::string& text, const olc::vf2d& scale);
	olc::vf2d UiScreenSize();
	void DrawScene();
	void DrawHud();
};
