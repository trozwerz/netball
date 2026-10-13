#pragma once

#include "olcPixelGameEngine3.h"

#include <array>
#include <cstddef>

namespace netball
{
	inline constexpr float CourtWidth = 15.25f;
	inline constexpr float CourtLength = 30.5f;
	inline constexpr float CourtHalfWidth = CourtWidth * 0.5f;
	inline constexpr float CourtHalfLength = CourtLength * 0.5f;
	inline constexpr float GoalCircleRadiusX = 4.9f;
	inline constexpr float GoalCircleRadiusZ = 5.2f;
	inline constexpr float GoalCircleCenterInset = GoalCircleRadiusX * 0.15f;
	inline constexpr float HoopHeight = 8.0f;
	inline constexpr float HoopRadius = 0.627f;
	inline constexpr float HoopTubeRadius = 0.0875f;
	inline constexpr float HoopNetDepth = 1.15f;
	inline constexpr float HoopPoleHalfWidth = 0.2f;
	inline constexpr float HoopCenterInset =
		HoopRadius + HoopTubeRadius + HoopPoleHalfWidth;
	inline constexpr float BallRadius = 0.51f;
	inline constexpr std::size_t PositionCount = 7;

	// Home wears orange-yellow (a little deeper than the blonde hair texture);
	// away wears black. The "display" colours are lighter for use on dark panels.
	inline const olc::Pixel HomeTeamColour(255, 150, 15);
	inline const olc::Pixel AwayTeamColour(34, 34, 40);
	inline const olc::Pixel AwayTeamDisplayColour(165, 170, 185);

	struct Position
	{
		const char* abbreviation;
		const char* name;
	};

	inline constexpr std::array<Position, PositionCount> Positions{{
		{ "GS", "Goal Shooter" },
		{ "GA", "Goal Attack" },
		{ "WA", "Wing Attack" },
		{ "C",  "Centre" },
		{ "WD", "Wing Defence" },
		{ "GD", "Goal Defence" },
		{ "GK", "Goal Keeper" },
	}};

	struct Player
	{
		olc::vf4d position;
		std::size_t role = 0;
		bool homeTeam = true;
	};

	inline float GoalCircleMetric(const olc::vf4d& position, const bool positiveEnd)
	{
		const float goalLine = positiveEnd ? CourtHalfLength : -CourtHalfLength;
		const float circleCenterZ = goalLine -
			std::copysign(GoalCircleCenterInset, goalLine);
		const float normalizedX = position.x / GoalCircleRadiusX;
		const float normalizedZ = (position.z - circleCenterZ) / GoalCircleRadiusZ;
		return normalizedX * normalizedX + normalizedZ * normalizedZ;
	}

	inline bool IsInsideGoalCircle(const olc::vf4d& position, const bool positiveEnd)
	{
		return GoalCircleMetric(position, positiveEnd) <= 1.0f;
	}

	// The 2-point zone is the courtside half of the outer 1.9 m band of the circle.
	inline constexpr float SuperShotRadiusScale = (4.9f - 1.9f) / 4.9f;

	inline bool IsInSuperShotZone(const olc::vf4d& position, const bool positiveEnd)
	{
		const float goalLine = positiveEnd ? CourtHalfLength : -CourtHalfLength;
		const float circleCenterZ = goalLine -
			std::copysign(GoalCircleCenterInset, goalLine);
		const bool courtside = positiveEnd
			? position.z <= circleCenterZ : position.z >= circleCenterZ;
		const float metric = GoalCircleMetric(position, positiveEnd);
		return courtside && metric <= 1.0f &&
			metric > SuperShotRadiusScale * SuperShotRadiusScale;
	}

	// A shooter already outside the circle may still move toward it.
	inline bool IsMovingIntoGoalCircle(const olc::vf4d& from, const olc::vf4d& to,
		const bool positiveEnd)
	{
		return IsInsideGoalCircle(to, positiveEnd) ||
			GoalCircleMetric(to, positiveEnd) <= GoalCircleMetric(from, positiveEnd);
	}

	inline bool IsLegalPosition(const Player& player, const olc::vf4d& position)
	{
		if (position.x < -CourtHalfWidth || position.x > CourtHalfWidth ||
			position.z < -CourtHalfLength || position.z > CourtHalfLength)
			return false;

		const bool attacksPositiveEnd = player.homeTeam;
		const float thirdLength = CourtLength / 3.0f;
		const float attackBoundary = attacksPositiveEnd
			? CourtHalfLength - thirdLength
			: -CourtHalfLength + thirdLength;
		const float defenceBoundary = -attackBoundary;
		const bool inAttackingThird = attacksPositiveEnd
			? position.z >= attackBoundary
			: position.z <= attackBoundary;
		const bool inDefendingThird = attacksPositiveEnd
			? position.z <= defenceBoundary
			: position.z >= defenceBoundary;
		const bool inCentreThird = !inAttackingThird && !inDefendingThird;
		const bool inAttackCircle = IsInsideGoalCircle(position, attacksPositiveEnd);
		const bool inDefenceCircle = IsInsideGoalCircle(position, !attacksPositiveEnd);

		switch (player.role)
		{
		case 0: return inAttackingThird;
		case 1: return inCentreThird || inAttackingThird;
		case 2: return (inCentreThird || inAttackingThird) && !inAttackCircle;
		case 3: return !inAttackCircle && !inDefenceCircle;
		case 4: return (inCentreThird || inDefendingThird) && !inDefenceCircle;
		case 5: return inCentreThird || inDefendingThird;
		case 6: return inDefendingThird;
		default: return false;
		}
	}
}
