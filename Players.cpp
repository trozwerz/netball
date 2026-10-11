#include "Players.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace netball
{
	Teams CreateTeams()
	{
		const std::array<olc::vf4d, TeamSize> homeFormation{{
			{ 0.0f, 0.0f, 11.0f },
			{ -2.0f, 0.0f, 7.0f },
			{ 5.6f, 0.0f, 6.0f },
			{ -1.0f, 0.0f, 0.0f },
			{ 5.6f, 0.0f, -6.0f },
			{ 2.0f, 0.0f, -7.0f },
			{ 0.0f, 0.0f, -11.0f },
		}};

		Teams players{};
		for (std::size_t role = 0; role < TeamSize; ++role)
		{
			players[role] = { homeFormation[role], role, true };
			olc::vf4d awayPosition = homeFormation[role];
			awayPosition.z = -awayPosition.z;
			awayPosition.x = -awayPosition.x + 1.2f;
			players[role + TeamSize] = { awayPosition, role, false };
		}
		return players;
	}

	namespace
	{
		float collisionScale = 1.0f;
	}

	void SetPlayerCollisionScale(const float scale)
	{
		collisionScale = scale;
	}

	bool OverlapsAnotherPlayer(const Teams& players, const std::size_t movingPlayer,
		const olc::vf4d& position, const bool attackingTeamHome,
		const bool ignoreDefenders)
	{
		const float fullWidth = PlayerCollisionHalfWidth * collisionScale;
		const float movingHalfWidth = players[movingPlayer].homeTeam == attackingTeamHome
			? fullWidth : fullWidth * 0.5f;
		for (std::size_t i = 0; i < players.size(); ++i)
		{
			if (i == movingPlayer)
				continue;
			if (ignoreDefenders && players[i].homeTeam != attackingTeamHome)
				continue;

			const float otherHalfWidth = players[i].homeTeam == attackingTeamHome
				? fullWidth : fullWidth * 0.5f;
			const float minSeparation = movingHalfWidth + otherHalfWidth;
			const float dx = position.x - players[i].position.x;
			const float dz = position.z - players[i].position.z;
			if (std::abs(dx) < minSeparation && std::abs(dz) < minSeparation)
				return true;
		}
		return false;
	}

	std::size_t FindPassTarget(const Teams& players, const std::size_t passer,
		const PassDirection direction, const std::size_t rank)
	{
		if (passer >= players.size())
			return PlayerCount;

		const Player& carrier = players[passer];
		const float carrierProgress = carrier.homeTeam
			? carrier.position.z
			: -carrier.position.z;
		std::array<std::pair<float, std::size_t>, TeamSize - 1> candidates{};
		std::size_t count = 0;

		for (std::size_t i = 0; i < players.size(); ++i)
		{
			if (i == passer || players[i].homeTeam != carrier.homeTeam)
				continue;

			const float targetProgress = carrier.homeTeam
				? players[i].position.z
				: -players[i].position.z;
			const bool isAhead = targetProgress > carrierProgress;
			const bool isBehind = targetProgress < carrierProgress;
			if ((direction == PassDirection::TowardGoal && !isAhead) ||
				(direction == PassDirection::AwayFromGoal && !isBehind))
				continue;

			const float dx = players[i].position.x - carrier.position.x;
			const float dz = players[i].position.z - carrier.position.z;
			candidates[count++] = { dx * dx + dz * dz, i };
		}

		std::sort(candidates.begin(), candidates.begin() + count,
			[](const auto& left, const auto& right)
			{
				return left.first < right.first;
			});

		return rank < count ? candidates[rank].second : PlayerCount;
	}
}
