#include "NetballGame.h"

#include "GameRules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>

namespace
{
	constexpr float Pi = 3.14159265358979323846f;
	constexpr float MarkingDelay = 0.25f;
	constexpr float MarkingDistance = netball::PlayerHalfWidth * 2.0f + 0.05f;
	constexpr float MarkerSeparationDistance = 2.5f;
	constexpr float DefenderAvoidanceDistance = MarkerSeparationDistance + 1.0f;
	constexpr float StuckThreshold = 1.0f;
	constexpr float DefenderClipDuration = 3.0f;
	constexpr float MeaningfulProgressDistance = 0.2f;
	constexpr float EscapeMovementSpeed = 5.4f;
	constexpr float PassTargetRadius = netball::CourtLength * 0.25f;
	constexpr float CourtCornerMargin = 2.5f;
	constexpr float CourtEdgeReleaseMargin = 4.0f;
	constexpr std::array<std::size_t, netball::TeamSize> MarkedRoles{
		6, 5, 4, 3, 2, 1, 0
	};

	bool IsBallInAttackingThird(const olc::vf4d& position, const bool homeTeam)
	{
		const float boundary = homeTeam
			? netball::CourtHalfLength - netball::CourtLength / 3.0f
			: -netball::CourtHalfLength + netball::CourtLength / 3.0f;
		return homeTeam ? position.z >= boundary : position.z <= boundary;
	}
}
void NetballGame::UpdateAttackerStuckTimers(const float elapsedTime)
{
	const bool attackingTeamHome = players[ballCarrier].homeTeam;
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		defenderClipDurations[i] =
			std::max(0.0f, defenderClipDurations[i] - elapsedTime);
		if (i == ballCarrier || players[i].homeTeam != attackingTeamHome)
		{
			stationaryDurations[i] = 0.0f;
			defenderClipDurations[i] = 0.0f;
			attackerProgressPositions[i] = players[i].position;
			continue;
		}

		const float dx = players[i].position.x - attackerProgressPositions[i].x;
		const float dz = players[i].position.z - attackerProgressPositions[i].z;
		if (dx * dx + dz * dz >=
			MeaningfulProgressDistance * MeaningfulProgressDistance)
		{
			stationaryDurations[i] = 0.0f;
			attackerProgressPositions[i] = players[i].position;
			continue;
		}

		stationaryDurations[i] += elapsedTime;
		if (stationaryDurations[i] > StuckThreshold &&
			defenderClipDurations[i] <= 0.0f)
		{
			defenderClipDurations[i] = DefenderClipDuration;
			stationaryDurations[i] = 0.0f;
			attackerProgressPositions[i] = players[i].position;
		}
	}
}

void NetballGame::RecordPlayerPositions()
{
	PositionSnapshot snapshot;
	snapshot.time = gameTime;
	for (std::size_t i = 0; i < players.size(); ++i)
		snapshot.positions[i] = players[i].position;
	positionHistory.push_back(snapshot);

	const float oldestNeededTime = gameTime - MarkingDelay;
	while (positionHistory.size() > 2 &&
		positionHistory[1].time <= oldestNeededTime)
		positionHistory.pop_front();
}

olc::vf4d NetballGame::GetDelayedPlayerPosition(const std::size_t playerIndex) const
{
	const float targetTime = gameTime - MarkingDelay;
	if (positionHistory.empty() || targetTime <= positionHistory.front().time)
		return players[playerIndex].position;

	for (std::size_t i = 1; i < positionHistory.size(); ++i)
	{
		if (positionHistory[i].time < targetTime)
			continue;

		const PositionSnapshot& before = positionHistory[i - 1];
		const PositionSnapshot& after = positionHistory[i];
		const float duration = after.time - before.time;
		const float amount = duration > 0.0f
			? (targetTime - before.time) / duration
			: 0.0f;
		const olc::vf4d& start = before.positions[playerIndex];
		const olc::vf4d& end = after.positions[playerIndex];
		return {
			start.x + (end.x - start.x) * amount,
			start.y + (end.y - start.y) * amount,
			start.z + (end.z - start.z) * amount
		};
	}

	return positionHistory.back().positions[playerIndex];
}

void NetballGame::ChooseRandomDirection(const std::size_t playerIndex,
	const float minDuration, const float maxDuration)
{
	std::uniform_real_distribution<float> angleChoice(0.0f, 2.0f * Pi);
	std::uniform_real_distribution<float> durationChoice(minDuration, maxDuration);
	const float angle = angleChoice(randomEngine);
	teammateDirections[playerIndex] = { std::cos(angle), std::sin(angle) };
	directionChangeTimers[playerIndex] = durationChoice(randomEngine);
}

void NetballGame::RefreshPassTargets()
{
	const netball::Player& carrier = players[ballCarrier];
	const float sign = carrier.homeTeam ? 1.0f : -1.0f;
	const float carrierProgress = carrier.position.z * sign;
	passTargets.fill(netball::PlayerCount);

	// Teammates ordered by progress toward the hoop, nearest ahead first.
	std::array<std::pair<float, std::size_t>, netball::TeamSize - 1> ahead{};
	std::size_t aheadCount = 0;
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (i == ballCarrier || players[i].homeTeam != carrier.homeTeam)
			continue;
		const float progress = players[i].position.z * sign;
		if (progress > carrierProgress)
			ahead[aheadCount++] = { progress, i };
	}
	std::sort(ahead.begin(), ahead.begin() + aheadCount,
		[](const auto& left, const auto& right) { return left.first < right.first; });
	if (aheadCount > 0)
		passTargets[0] = ahead[0].second;
	if (aheadCount > 1)
		passTargets[1] = ahead[1].second;

	// C: closest unassigned teammate. V: closest unassigned one behind the carrier.
	const auto isAssigned = [&](const std::size_t index)
	{
		for (const std::size_t target : passTargets)
			if (target == index)
				return true;
		return false;
	};
	const auto closestUnassigned = [&](const bool mustBeBehind)
	{
		std::size_t best = netball::PlayerCount;
		float bestDistance = std::numeric_limits<float>::max();
		for (std::size_t i = 0; i < players.size(); ++i)
		{
			if (i == ballCarrier || players[i].homeTeam != carrier.homeTeam ||
				isAssigned(i))
				continue;
			if (mustBeBehind && players[i].position.z * sign >= carrierProgress)
				continue;
			const float dx = players[i].position.x - carrier.position.x;
			const float dz = players[i].position.z - carrier.position.z;
			const float distance = dx * dx + dz * dz;
			if (distance < bestDistance)
			{
				bestDistance = distance;
				best = i;
			}
		}
		return best;
	};
	passTargets[2] = closestUnassigned(false);
	passTargets[3] = closestUnassigned(true);

	for (std::size_t i = 0; i < players.size(); ++i)
	{
		separatingFromMarker[i] =
			i != ballCarrier && players[i].homeTeam == carrier.homeTeam;
		if (players[i].homeTeam == players[ballCarrier].homeTeam &&
			players[i].role <= 1 && i != ballCarrier)
			directionChangeTimers[i] = 0.0f;
	}
}

void NetballGame::UpdateTeammateMovement(const float elapsedTime)
{
	if (looseBall)
	{
		UpdateLooseBallChasers(elapsedTime);
		return;
	}

	constexpr float teammateSpeed = 3.6f;
	const netball::Player& carrier = players[ballCarrier];
	const float goalDirection = carrier.homeTeam ? 1.0f : -1.0f;
	const bool ballInAttackingThird =
		IsBallInAttackingThird(ballPosition, carrier.homeTeam);
	for (std::size_t playerIndex = 0; playerIndex < players.size(); ++playerIndex)
	{
		if (playerIndex == ballCarrier)
			continue;

		if (passInFlight && !shotInFlight && playerIndex == passReceiver &&
			playerIndex != selectedPlayer)
		{
			const olc::vf2d toDestination{
				passDestination.x - players[playerIndex].position.x,
				passDestination.y - players[playerIndex].position.z
			};
			const float distance = toDestination.mag();
			if (distance > 0.05f)
			{
				const float moveDistance =
					std::min(teammateSpeed * elapsedTime, distance);
				MovePlayer(playerIndex,
					toDestination * (moveDistance / distance), false);
			}
			continue;
		}
		const float playerMovementSpeed =
			defenderClipDurations[playerIndex] > 0.0f
				? EscapeMovementSpeed : teammateSpeed;

		if (players[playerIndex].homeTeam != carrier.homeTeam)
		{
			if (playerIndex == selectedPlayer)
				continue;

			const std::size_t markedRole = MarkedRoles[players[playerIndex].role];
			std::size_t markedPlayer = netball::PlayerCount;
			for (std::size_t i = 0; i < players.size(); ++i)
			{
				if (players[i].homeTeam == players[playerIndex].homeTeam ||
					players[i].role != markedRole)
					continue;
				markedPlayer = i;
				break;
			}
			if (markedPlayer >= netball::PlayerCount)
				continue;

			const olc::vf4d delayedPosition = GetDelayedPlayerPosition(markedPlayer);
			const olc::vf2d toMarked{
				delayedPosition.x - players[playerIndex].position.x,
				delayedPosition.z - players[playerIndex].position.z
			};
			const float distance = toMarked.mag();
			if (distance > MarkingDistance)
			{
				const float moveDistance = std::min(
					teammateSpeed * elapsedTime, distance - MarkingDistance);
				const olc::vf2d movement = toMarked * (moveDistance / distance);
				MovePlayer(playerIndex, movement, false);
			}
			continue;
		}

		const bool shooterHasBallInCircle =
			(carrier.role == 0 || carrier.role == 1) &&
			netball::IsInsideGoalCircle(carrier.position, carrier.homeTeam);
		if (shooterHasBallInCircle && players[playerIndex].role == 3 &&
			playerIndex != selectedPlayer)
		{
			const float goalLine = carrier.homeTeam
				? netball::CourtHalfLength : -netball::CourtHalfLength;
			const float circleCenterZ = goalLine -
				std::copysign(netball::GoalCircleCenterInset, goalLine);
			const olc::vf2d target{
				0.0f,
				circleCenterZ -
					goalDirection * (netball::GoalCircleRadiusZ + 0.25f)
			};
			const olc::vf2d toCircleTop{
				target.x - players[playerIndex].position.x,
				target.y - players[playerIndex].position.z
			};
			const float distance = toCircleTop.mag();
			if (distance > 0.15f)
			{
				const float moveDistance =
					std::min(playerMovementSpeed * elapsedTime, distance);
				const olc::vf2d movement =
					toCircleTop * (moveDistance / distance);
				if (!MovePlayer(playerIndex, movement, false))
					MoveAwayFromDefenders(playerIndex, toCircleTop,
						playerMovementSpeed * elapsedTime, false);
			}
			continue;
		}

		const bool isAttacker = players[playerIndex].role <= 1;
		if (aiShooterPauseRemaining > 0.0f && isAttacker &&
			carrier.role <= 1 && IsAIAttacking())
		{
			const float goalLine = carrier.homeTeam
				? netball::CourtHalfLength : -netball::CourtHalfLength;
			const olc::vf2d target{ 0.0f, goalLine - goalDirection * 3.0f };
			const olc::vf2d toTarget{
				target.x - players[playerIndex].position.x,
				target.y - players[playerIndex].position.z
			};
			const float distance = toTarget.mag();
			if (distance > 0.15f)
			{
				const float moveDistance =
					std::min(playerMovementSpeed * elapsedTime, distance);
				const olc::vf2d movement = toTarget * (moveDistance / distance);
				if (!MovePlayer(playerIndex, movement, false))
					MoveAwayFromDefenders(playerIndex, toTarget,
						playerMovementSpeed * elapsedTime, false);
			}
			continue;
		}

		const bool keepShooterInCircle = isAttacker && ballInAttackingThird;
		// Direction from the player towards the middle of the attacking circle.
		olc::vf2d towardCircleCentre{};
		float circleMetric = 0.0f;
		float circleLean = 0.0f;
		if (isAttacker)
		{
			const float goalLine = carrier.homeTeam
				? netball::CourtHalfLength : -netball::CourtHalfLength;
			const float circleCenterZ = goalLine -
				std::copysign(netball::GoalCircleCenterInset, goalLine);
			towardCircleCentre = {
				-players[playerIndex].position.x,
				circleCenterZ - players[playerIndex].position.z };
			circleMetric = netball::GoalCircleMetric(
				players[playerIndex].position, carrier.homeTeam);
			if (keepShooterInCircle)
				circleLean = std::clamp((circleMetric - 0.3f) / 0.4f, 0.0f, 1.0f);
			else
			{
				// Without the ball up the court, GS and GA still drift forward
				// rather than back towards their own defensive end.
				const float forward = players[playerIndex].position.z * goalDirection;
				circleLean = std::clamp((4.0f - forward) / 4.0f, 0.0f, 1.0f);
			}
		}
		if (keepShooterInCircle && circleMetric > 0.7f &&
			towardCircleCentre.mag2() > 0.0001f)
		{
			const olc::vf2d step = towardCircleCentre.norm() *
				(teammateSpeed * elapsedTime);
			if (MovePlayer(playerIndex, step, false) ||
				MoveAwayFromDefenders(playerIndex, towardCircleCentre,
					teammateSpeed * elapsedTime, false))
				continue;
		}

		if (playerIndex == selectedPlayer)
			continue;

		if (MoveOutOfCourtCorner(playerIndex, playerMovementSpeed * elapsedTime,
			false))
			continue;

		const bool isZXOption =
			playerIndex == passTargets[0] || playerIndex == passTargets[1];
		if (separatingFromMarker[playerIndex])
		{
			const std::size_t markedRole = MarkedRoles[players[playerIndex].role];
			std::size_t markedPlayer = netball::PlayerCount;
			for (std::size_t i = 0; i < players.size(); ++i)
			{
				if (players[i].homeTeam == players[playerIndex].homeTeam ||
					players[i].role != markedRole)
					continue;
				markedPlayer = i;
				break;
			}

			if (markedPlayer < netball::PlayerCount)
			{
				const olc::vf2d awayFromMarker{
					players[playerIndex].position.x - players[markedPlayer].position.x,
					players[playerIndex].position.z - players[markedPlayer].position.z
				};
				const float distance = awayFromMarker.mag();
				if (distance < MarkerSeparationDistance && distance > 0.0001f)
				{
					const float moveDistance = std::min(playerMovementSpeed * elapsedTime,
						MarkerSeparationDistance - distance);
					MoveAwayFromDefenders(playerIndex, awayFromMarker,
						moveDistance, false);
				}
				else if (distance <= 0.0001f)
				{
					MoveAwayFromDefenders(playerIndex, teammateDirections[playerIndex],
						playerMovementSpeed * elapsedTime, false);
				}

				const olc::vf2d updatedAwayFromMarker{
					players[playerIndex].position.x - players[markedPlayer].position.x,
					players[playerIndex].position.z - players[markedPlayer].position.z
				};
				if (updatedAwayFromMarker.mag() >= MarkerSeparationDistance)
				{
					separatingFromMarker[playerIndex] = false;
					directionChangeTimers[playerIndex] = 0.0f;
				}
				continue;
			}
			separatingFromMarker[playerIndex] = false;
		}

		olc::vf2d avoidanceDirection{};
		float nearestDefenderDistanceSquared =
			DefenderAvoidanceDistance * DefenderAvoidanceDistance;
		for (std::size_t i = 0; i < players.size(); ++i)
		{
			if (players[i].homeTeam == carrier.homeTeam)
				continue;
			const olc::vf2d away{
				players[playerIndex].position.x - players[i].position.x,
				players[playerIndex].position.z - players[i].position.z
			};
			const float distanceSquared = away.mag2();
			if (distanceSquared < nearestDefenderDistanceSquared)
			{
				nearestDefenderDistanceSquared = distanceSquared;
				avoidanceDirection = away;
			}
		}
		const bool defenderNearby =
			nearestDefenderDistanceSquared < DefenderAvoidanceDistance * DefenderAvoidanceDistance;
		if (avoidanceDirection.mag2() > 0.0001f)
			avoidanceDirection = avoidanceDirection.norm();

		constexpr float teammateSpacing = 3.5f;
		bool teammateTooClose = false;
		if (!defenderNearby)
		{
			float nearestTeammateDistanceSquared =
				teammateSpacing * teammateSpacing;
			for (std::size_t i = 0; i < players.size(); ++i)
			{
				if (i == playerIndex ||
					players[i].homeTeam != carrier.homeTeam)
					continue;
				const olc::vf2d away{
					players[playerIndex].position.x - players[i].position.x,
					players[playerIndex].position.z - players[i].position.z
				};
				const float distanceSquared = away.mag2();
				if (distanceSquared < nearestTeammateDistanceSquared)
				{
					nearestTeammateDistanceSquared = distanceSquared;
					avoidanceDirection = away;
					teammateTooClose = true;
				}
			}
			if (teammateTooClose)
			{
				if (avoidanceDirection.mag2() > 0.0001f)
					avoidanceDirection = avoidanceDirection.norm();
				else
					avoidanceDirection = teammateDirections[playerIndex];
			}
		}
		const bool avoiding = defenderNearby || teammateTooClose;

		if (isZXOption || isAttacker)
		{
			directionChangeTimers[playerIndex] -= elapsedTime;
			if (directionChangeTimers[playerIndex] <= 0.0f)
				ChooseRandomDirection(playerIndex, 0.5f,
					isAttacker && !isZXOption ? 1.0f : 1.5f);

			olc::vf2d desired = avoiding
				? avoidanceDirection : teammateDirections[playerIndex];
			// Near the circle edge, lean back inside so the player doesn't hug it.
			if (circleLean > 0.0f && towardCircleCentre.mag2() > 0.0001f)
			{
				const float lean = circleLean;
				const olc::vf2d blended = desired.mag2() > 0.0001f
					? desired.norm() * (1.0f - lean) + towardCircleCentre.norm() * lean
					: towardCircleCentre.norm();
				if (blended.mag2() > 0.0001f)
					desired = blended.norm();
			}
			// Ease the heading so switching between wandering and avoiding
			// (or hitting a boundary) turns the player instead of snapping.
			olc::vf2d& heading = smoothedDirections[playerIndex];
			if (heading.mag2() < 0.0001f)
				heading = desired;
			else
				heading += (desired - heading) * std::min(1.0f, 5.0f * elapsedTime);
			const olc::vf2d direction = heading.mag2() > 0.0001f
				? heading.norm() : desired;
			bool moved = MovePlayer(playerIndex,
				direction * (playerMovementSpeed * elapsedTime),
				false, false);
			if (!moved && avoiding)
				moved = MoveAwayFromDefenders(playerIndex, direction,
					playerMovementSpeed * elapsedTime, false);
			if (!moved)
			{
				ChooseRandomDirection(playerIndex, 0.5f,
					isAttacker ? 1.0f : 1.5f);
				if (isZXOption)
				{
					// Turn back toward the passer when the pass radius blocks us.
					const olc::vf2d inward{
						carrier.position.x - players[playerIndex].position.x,
						carrier.position.z - players[playerIndex].position.z };
					if (inward.mag2() > 0.0001f)
					{
						olc::vf2d blended = teammateDirections[playerIndex] +
							inward.norm() * 1.5f;
						teammateDirections[playerIndex] = blended.norm();
					}
				}
			}
			continue;
		}

		const olc::vf2d direction = avoiding
			? avoidanceDirection : olc::vf2d{ 0.0f, goalDirection };
		if (avoiding)
			MoveAwayFromDefenders(playerIndex, direction,
				playerMovementSpeed * elapsedTime, false);
		else
			MovePlayer(playerIndex,
				direction * (playerMovementSpeed * elapsedTime), false);
	}
}

void NetballGame::UpdateLooseBallChasers(const float elapsedTime)
{
	constexpr float chaseSpeed = 6.0f;
	constexpr std::array<bool, 2> Teams{ true, false };

	for (std::size_t teamIndex = 0; teamIndex < Teams.size(); ++teamIndex)
	{
		const bool homeTeam = Teams[teamIndex];
		std::array<std::pair<float, std::size_t>, netball::TeamSize> candidates{};
		std::size_t candidateCount = 0;
		for (std::size_t i = 0; i < players.size(); ++i)
		{
			if (players[i].homeTeam != homeTeam)
				continue;

			const float dx = ballPosition.x - players[i].position.x;
			const float dz = ballPosition.z - players[i].position.z;
			candidates[candidateCount++] = { dx * dx + dz * dz, i };
		}
		std::sort(candidates.begin(), candidates.begin() + candidateCount,
			[](const auto& left, const auto& right)
			{
				return left.first < right.first;
			});

		if (candidateCount == 0)
			continue;

		if (homeTeam && !aiAttackMode)
		{
			selectedPlayer = candidates[0].second;
			continue;
		}

		bool moved = false;
		for (std::size_t candidateIndex = 0;
			candidateIndex < candidateCount; ++candidateIndex)
		{
			const std::size_t playerIndex = candidates[candidateIndex].second;
			std::size_t& chaser = looseBallChasers[teamIndex];
			const netball::Player& player = players[playerIndex];
			const olc::vf2d toBall{
				ballPosition.x - player.position.x,
				ballPosition.z - player.position.z
			};
			const float distance = toBall.mag();
			if (distance <= 0.01f)
			{
				if (chaser != playerIndex)
				{
					chaser = playerIndex;
					LogEvent("LOOSE_BALL_CHASER team=" +
						std::string(homeTeam ? "HOME" : "AWAY") +
						" player=" +
						netball::Positions[player.role].abbreviation);
				}
				moved = true;
				break;
			}

			const olc::vf2d movement = toBall *
				(std::min(distance, chaseSpeed * elapsedTime) / distance);
			const olc::vf4d previousPosition = player.position;
			const bool completedMove = MovePlayer(playerIndex, movement, false);
			const bool madeProgress =
				player.position.x != previousPosition.x ||
				player.position.z != previousPosition.z;
			if (completedMove || madeProgress)
			{
				if (chaser != playerIndex)
				{
					chaser = playerIndex;
					LogEvent("LOOSE_BALL_CHASER team=" +
						std::string(homeTeam ? "HOME" : "AWAY") +
						" player=" +
						netball::Positions[player.role].abbreviation +
						(candidateIndex == 0
							? " reason=nearest"
							: " reason=alternate_after_block"));
				}
				moved = true;
				looseBallBlockedChasers[teamIndex] = netball::PlayerCount;
				break;
			}

			if (frameNumber - looseBallChaseBlockedFrames[teamIndex] >= 30)
			{
				LogEvent("LOOSE_BALL_CHASE_BLOCKED team=" +
					std::string(homeTeam ? "HOME" : "AWAY") +
					" player=" + netball::Positions[player.role].abbreviation +
					" ball=(" + std::to_string(ballPosition.x) + "," +
					std::to_string(ballPosition.z) + ")");
				looseBallChaseBlockedFrames[teamIndex] = frameNumber;
			}
		}
		if (!moved &&
			frameNumber - looseBallChaseBlockedFrames[teamIndex] >= 30)
			looseBallChaseBlockedFrames[teamIndex] = frameNumber;
	}
}

void NetballGame::UpdateAIAttack(const float elapsedTime)
{
	if (!IsAIAttacking() || passInFlight || looseBall ||
		shootingMeterActive || shotInFlight || shotResetPending ||
		aiAttackActionDelay > 0.0f)
		return;

	if (players[ballCarrier].role <= 1)
	{
		const netball::Player& shooter = players[ballCarrier];
		const float goalLine = shooter.homeTeam
			? netball::CourtHalfLength : -netball::CourtHalfLength;
		const auto poleDistance = [&](const olc::vf4d& position)
		{
			const float dx = position.x;
			const float dz = position.z - goalLine;
			return std::sqrt(dx * dx + dz * dz);
		};
		const float farDistance = 0.55f * std::sqrt(
			netball::GoalCircleRadiusX * netball::GoalCircleRadiusX +
			(netball::GoalCircleRadiusZ + netball::GoalCircleCenterInset) *
				(netball::GoalCircleRadiusZ + netball::GoalCircleCenterInset));
		const float carrierDistance = poleDistance(shooter.position);
		const bool farFromGoal = carrierDistance > farDistance;

		if (aiShooterPauseFor != ballCarrier)
		{
			aiShooterPauseFor = ballCarrier;
			aiShooterPauseRemaining = 0.0f;
			std::uniform_real_distribution<float> randomChance(0.0f, 1.0f);
			if (farFromGoal && randomChance(randomEngine) < 0.8f)
			{
				aiShooterPauseRemaining = 0.7f;
				LogEvent("AI_SHOOTER_PAUSE shooter=" +
					std::string(netball::Positions[shooter.role].abbreviation) +
					" pole_distance=" + std::to_string(carrierDistance));
			}
		}

		if (aiShooterPauseRemaining > 0.0f)
		{
			aiShooterPauseRemaining =
				std::max(0.0f, aiShooterPauseRemaining - elapsedTime);
			return;
		}

		if (farFromGoal)
		{
			constexpr float maxPassRange = netball::CourtLength * 0.45f;
			std::size_t closerShooter = netball::PlayerCount;
			float bestDistance = carrierDistance - 1.0f;
			for (std::size_t i = 0; i < players.size(); ++i)
			{
				if (i == ballCarrier ||
					players[i].homeTeam != shooter.homeTeam ||
					players[i].role > 1)
					continue;

				const float dx = players[i].position.x - shooter.position.x;
				const float dz = players[i].position.z - shooter.position.z;
				if (dx * dx + dz * dz > maxPassRange * maxPassRange)
					continue;

				const float distance = poleDistance(players[i].position);
				if (distance < bestDistance)
				{
					bestDistance = distance;
					closerShooter = i;
				}
			}
			if (closerShooter < netball::PlayerCount)
			{
				aiNoReceiverLogged = false;
				LogEvent("AI_SHOOTER_PASS_CLOSER shooter=" +
					std::string(netball::Positions[shooter.role].abbreviation) +
					" receiver=" +
					netball::Positions[players[closerShooter].role].abbreviation);
				StartPassToPlayer(closerShooter);
				aiAttackActionDelay = passDuration + 0.3f;
				return;
			}
		}
	}

	if (CanShoot())
	{
		const netball::Player& shooter = players[ballCarrier];
		float nearestDefenderDistance = std::numeric_limits<float>::max();
		const netball::Player* nearestDefender = nullptr;
		for (const netball::Player& defender : players)
		{
			if (defender.homeTeam == shooter.homeTeam ||
				(defender.role != 5 && defender.role != 6))
				continue;

			const float dx = defender.position.x - shooter.position.x;
			const float dz = defender.position.z - shooter.position.z;
			const float distance = std::sqrt(dx * dx + dz * dz);
			if (distance < nearestDefenderDistance)
			{
				nearestDefenderDistance = distance;
				nearestDefender = &defender;
			}
		}

		constexpr float baseMissChance = 0.10f;
		constexpr float maxDefenderMissBonus = 0.40f;
		constexpr float defenderInfluenceDistance = 5.0f;
		const float defenderPressure = nearestDefender
			? std::clamp(
				(defenderInfluenceDistance - nearestDefenderDistance) /
					defenderInfluenceDistance,
				0.0f, 1.0f)
			: 0.0f;
		const float missChance = Difficulty().aiFixedMissChance >= 0.0f
			? Difficulty().aiFixedMissChance
			: std::min(1.0f, Difficulty().aiMissScale * std::max(0.0f,
				1.0f - 1.3f * (1.0f -
					(baseMissChance + maxDefenderMissBonus * defenderPressure))));
		std::uniform_real_distribution<float> randomChance(0.0f, 1.0f);
		aiShotMustMiss = randomChance(randomEngine) < missChance;

		std::ostringstream event;
		event << std::fixed << std::setprecision(2)
			<< "AI_SHOT_PRESSURE shooter="
			<< netball::Positions[shooter.role].abbreviation
			<< " nearest_defender="
			<< (nearestDefender
				? netball::Positions[nearestDefender->role].abbreviation
				: "NONE")
			<< " defender_distance=" << nearestDefenderDistance
			<< " miss_chance_percent=" << missChance * 100.0f
			<< " forced_miss=" << (aiShotMustMiss ? "yes" : "no");
		LogEvent(event.str());

		shootingMeterPosition = 0.72f;
		shootingMeterGuaranteedShot = !aiShotMustMiss;
		if (StartShot())
			aiAttackActionDelay = passDuration + 0.3f;
		return;
	}

	const netball::Player& carrier = players[ballCarrier];
	const float carrierProgress = carrier.homeTeam
		? carrier.position.z : -carrier.position.z;
	constexpr float maxPassDistance = netball::CourtLength * 0.3f;
	const float maxPassDistanceSquared = maxPassDistance * maxPassDistance;
	std::size_t forwardReceiver = netball::PlayerCount;
	// In the attacking third, prefer a shooter who is already inside the
	// circle, even if they are behind the carrier.
	if (IsBallInAttackingThird(ballPosition, carrier.homeTeam))
	{
		float bestDistanceSquared = maxPassDistanceSquared * 2.25f;
		for (std::size_t i = 0; i < players.size(); ++i)
		{
			if (i == ballCarrier || players[i].homeTeam != carrier.homeTeam ||
				players[i].role > 1 ||
				!netball::IsInsideGoalCircle(players[i].position, carrier.homeTeam))
				continue;

			const float dx = players[i].position.x - carrier.position.x;
			const float dz = players[i].position.z - carrier.position.z;
			const float distanceSquared = dx * dx + dz * dz;
			if (distanceSquared < bestDistanceSquared)
			{
				bestDistanceSquared = distanceSquared;
				forwardReceiver = i;
			}
		}
	}
	// Retry with a longer range so a stalled attack can still advance
	// instead of looping short passes between two players.
	for (const float rangeScale : { 1.0f, 1.5f })
	{
		if (forwardReceiver < netball::PlayerCount)
			break;
		const float rangeSquared = maxPassDistanceSquared * rangeScale * rangeScale;
		float bestProgress = carrierProgress;
		for (std::size_t i = 0; i < players.size(); ++i)
		{
			if (i == ballCarrier || players[i].homeTeam != carrier.homeTeam)
				continue;

			const float dx = players[i].position.x - carrier.position.x;
			const float dz = players[i].position.z - carrier.position.z;
			if (dx * dx + dz * dz > rangeSquared)
				continue;

			const float progress = carrier.homeTeam
				? players[i].position.z : -players[i].position.z;
			if (progress > bestProgress)
			{
				bestProgress = progress;
				forwardReceiver = i;
			}
		}
		if (forwardReceiver < netball::PlayerCount)
			break;
	}

	if (forwardReceiver < netball::PlayerCount)
	{
		aiNoReceiverLogged = false;
		StartPassToPlayer(forwardReceiver);
		aiAttackActionDelay = passDuration + 0.3f;
		return;
	}

	std::size_t fallbackReceiver = netball::PlayerCount;
	float fallbackDistanceSquared = maxPassDistanceSquared;
	const bool isShooter = carrier.role == 0 || carrier.role == 1;
	for (std::size_t i = 0; i < players.size(); ++i)
	{
		if (i == ballCarrier || players[i].homeTeam != carrier.homeTeam)
			continue;
		if (isShooter && players[i].role != 2 && players[i].role != 3)
			continue;

		const float dx = players[i].position.x - carrier.position.x;
		const float dz = players[i].position.z - carrier.position.z;
		const float distanceSquared = dx * dx + dz * dz;
		if (distanceSquared > maxPassDistanceSquared ||
			distanceSquared >= fallbackDistanceSquared)
			continue;

		fallbackReceiver = i;
		fallbackDistanceSquared = distanceSquared;
	}

	if (fallbackReceiver < netball::PlayerCount)
	{
		aiNoReceiverLogged = false;
		StartPassToPlayer(fallbackReceiver);
		aiAttackActionDelay = passDuration + 0.3f;
		LogEvent("AI_PASS_FALLBACK reason=no_forward_receiver receiver=" +
			std::string(netball::Positions[players[fallbackReceiver].role].abbreviation));
		return;
	}

	if (!aiNoReceiverLogged)
	{
		LogEvent("AI_ATTACK_BLOCKED reason=no_teammate_within_pass_radius team=" +
			std::string(carrier.homeTeam ? "HOME" : "AWAY") +
			" carrier=" + netball::Positions[carrier.role].abbreviation +
			" max_distance=" + std::to_string(maxPassDistance));
		aiNoReceiverLogged = true;
	}
}

bool NetballGame::IsAIAttacking() const
{
	return !players[ballCarrier].homeTeam || aiAttackMode;
}

bool NetballGame::MoveOutOfCourtCorner(const std::size_t playerIndex,
	const float distance, const bool constrainToShootingCircle)
{
	if (distance <= 0.0f)
		return false;

	const netball::Player& player = players[playerIndex];
	const float sideClearance = netball::CourtHalfWidth - std::abs(player.position.x);
	const float endClearance = netball::CourtHalfLength - std::abs(player.position.z);
	if (!escapingCourtEdge[playerIndex])
	{
		const bool nearSide = sideClearance <= CourtCornerMargin;
		const bool nearEnd = endClearance <= CourtCornerMargin;
		if (!nearSide && !nearEnd)
			return false;

		courtEdgeEscapeDirections[playerIndex] = {
			nearSide ? -std::copysign(1.0f, player.position.x) : 0.0f,
			nearEnd ? -std::copysign(1.0f, player.position.z) : 0.0f
		};
		courtEdgeEscapeDirections[playerIndex] =
			courtEdgeEscapeDirections[playerIndex].norm();
		escapingCourtEdge[playerIndex] = true;
	}

	if (sideClearance > CourtEdgeReleaseMargin &&
		endClearance > CourtEdgeReleaseMargin)
	{
		escapingCourtEdge[playerIndex] = false;
		courtEdgeEscapeDirections[playerIndex] = {};
		return false;
	}

	MovePlayer(playerIndex,
		courtEdgeEscapeDirections[playerIndex] * distance, false,
		constrainToShootingCircle);
	return true;
}

bool NetballGame::MoveAwayFromDefenders(const std::size_t playerIndex,
	const olc::vf2d& preferredDirection, const float distance,
	const bool constrainToShootingCircle)
{
	if (distance <= 0.0f)
		return false;

	constexpr int directionCount = 16;
	constexpr float avoidDistanceSquared =
		DefenderAvoidanceDistance * DefenderAvoidanceDistance;
	std::array<olc::vf2d, directionCount + 1> directions{};
	if (preferredDirection.mag2() > 0.0001f)
		directions[0] = preferredDirection.norm();
	for (int i = 0; i < directionCount; ++i)
	{
		const float angle = 2.0f * Pi * float(i) / float(directionCount);
		directions[std::size_t(i + 1)] = { std::cos(angle), std::sin(angle) };
	}

	const netball::Player& player = players[playerIndex];
	const bool ignoreDefenders = defenderClipDurations[playerIndex] > 0.0f;
	float bestNearestDistanceSquared = -1.0f;
	float bestTotalDistanceSquared = -1.0f;
	float bestAlignment = -2.0f;
	olc::vf2d bestDirection{};
	for (const olc::vf2d& direction : directions)
	{
		if (direction.mag2() <= 0.0001f)
			continue;
		const olc::vf4d candidate{
			player.position.x + direction.x * distance,
			player.position.y,
			player.position.z + direction.y * distance
		};
		if (!netball::IsLegalPosition(player, candidate) ||
			(constrainToShootingCircle &&
				!netball::IsMovingIntoGoalCircle(
					player.position, candidate, player.homeTeam)) ||
			!IsWithinPassTargetRadius(playerIndex, candidate) ||
			netball::OverlapsAnotherPlayer(players, playerIndex, candidate,
				players[ballCarrier].homeTeam, ignoreDefenders))
			continue;

		float nearestDistanceSquared = avoidDistanceSquared;
		float totalDistanceSquared = 0.0f;
		for (std::size_t i = 0; i < players.size(); ++i)
		{
			if (players[i].homeTeam == player.homeTeam)
				continue;
			const float dx = candidate.x - players[i].position.x;
			const float dz = candidate.z - players[i].position.z;
			const float distanceSquared = dx * dx + dz * dz;
			nearestDistanceSquared =
				std::min(nearestDistanceSquared, distanceSquared);
			totalDistanceSquared += distanceSquared;
		}
		const float alignment = preferredDirection.mag2() > 0.0001f
			? direction.x * preferredDirection.x + direction.y * preferredDirection.y
			: 0.0f;
		if (nearestDistanceSquared > bestNearestDistanceSquared ||
			(nearestDistanceSquared == bestNearestDistanceSquared &&
				totalDistanceSquared > bestTotalDistanceSquared) ||
			(nearestDistanceSquared == bestNearestDistanceSquared &&
				totalDistanceSquared == bestTotalDistanceSquared &&
				alignment > bestAlignment))
		{
			bestNearestDistanceSquared = nearestDistanceSquared;
			bestTotalDistanceSquared = totalDistanceSquared;
			bestAlignment = alignment;
			bestDirection = direction;
		}
	}

	return bestDirection.mag2() > 0.0001f &&
		MovePlayer(playerIndex, bestDirection * distance, false,
			constrainToShootingCircle);
}

bool NetballGame::IsWithinPassTargetRadius(const std::size_t playerIndex,
	const olc::vf4d& position) const
{
	if (looseBall)
		return true;

	if (playerIndex != passTargets[0] && playerIndex != passTargets[1])
		return true;

	const float dx = position.x - players[ballCarrier].position.x;
	const float dz = position.z - players[ballCarrier].position.z;
	const float candidateDistanceSquared = dx * dx + dz * dz;
	const float currentDx =
		players[playerIndex].position.x - players[ballCarrier].position.x;
	const float currentDz =
		players[playerIndex].position.z - players[ballCarrier].position.z;
	const float currentDistanceSquared = currentDx * currentDx + currentDz * currentDz;
	const float maxDistanceSquared = PassTargetRadius * PassTargetRadius;
	return currentDistanceSquared > maxDistanceSquared
		? candidateDistanceSquared <= currentDistanceSquared
		: candidateDistanceSquared <= maxDistanceSquared;
}
