#pragma once

#include "GameRules.h"

#include <array>

namespace netball
{
	inline constexpr std::size_t TeamSize = PositionCount;
	inline constexpr std::size_t PlayerCount = TeamSize * 2;
	inline constexpr float PlayerHalfWidth = 0.6f;
	inline constexpr float PlayerCollisionHalfWidth = PlayerHalfWidth * 0.8f;

	using Teams = std::array<Player, PlayerCount>;

	enum class PassDirection
	{
		TowardGoal,
		AwayFromGoal
	};

	Teams CreateTeams();
	void SetPlayerCollisionScale(float scale);
	bool OverlapsAnotherPlayer(const Teams& players, std::size_t movingPlayer,
		const olc::vf4d& position, bool attackingTeamHome,
		bool ignoreDefenders = false);
	std::size_t FindPassTarget(const Teams& players, std::size_t passer,
		PassDirection direction, std::size_t rank = 0);
}
