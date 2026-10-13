#pragma once

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace netball
{
	struct PlayerProfile
	{
		std::string name;
		bool homeTeam = true;
		int portrait = 0;
		int shooting = 3;
		int passing = 3;
		int speed = 3;
		int defence = 3;
		int goals = 0;
		int assists = 0;
		int rebounds = 0;
		int turnovers = 0;
	};

	// Rating 3 is neutral; each step is worth 1/3 of a unit either way.
	inline float RatingFactor(const int rating)
	{
		return std::clamp(float(rating), 0.0f, 5.0f) / 3.0f;
	}

	inline std::vector<PlayerProfile> DefaultProfiles()
	{
		static const char* names[18] = { "Kate", "Jane", "Kris", "Ella", "Evie",
			"Elody", "Sam", "Penny", "Amy", "Betsy", "Harriet", "Elsa",
			"Simone", "Tamara", "Annaliese", "Bobby", "Rachael", "Elaine" };
		// Slots 0-6 home court, 7-13 away court, 14-15 home subs, 16-17 away subs.
		std::vector<PlayerProfile> profiles(18);
		for (int i = 0; i < 18; ++i)
		{
			profiles[i].name = names[i];
			profiles[i].homeTeam = i < 7 || (i >= 14 && i < 16);
			profiles[i].portrait = i;
		}
		return profiles;
	}

	inline void SaveProfiles(const std::string& path,
		const std::vector<PlayerProfile>& profiles)
	{
		std::ofstream file(path);
		file << "name,team,portrait,shooting,passing,speed,defence\n";
		for (const PlayerProfile& p : profiles)
			file << p.name << ',' << (p.homeTeam ? "Home" : "Away") << ','
				<< p.portrait << ',' << p.shooting << ',' << p.passing << ','
				<< p.speed << ',' << p.defence << '\n';
	}

	// Reads rows in slot order (7 home court, 7 away court, 2 home subs,
	// 2 away subs). Falls back to defaults and writes the CSV if absent.
	inline std::vector<PlayerProfile> LoadProfiles(const std::string& path)
	{
		std::vector<PlayerProfile> profiles = DefaultProfiles();
		std::ifstream file(path);
		if (!file)
		{
			SaveProfiles(path, profiles);
			return profiles;
		}
		std::string line;
		std::getline(file, line);
		std::size_t row = 0;
		while (row < profiles.size() && std::getline(file, line))
		{
			if (line.empty())
				continue;
			std::vector<std::string> cells;
			std::stringstream stream(line);
			std::string cell;
			while (std::getline(stream, cell, ','))
				cells.push_back(cell);
			if (cells.size() < 7)
				continue;
			try
			{
				PlayerProfile& p = profiles[row];
				p.name = cells[0];
				p.homeTeam = cells[1] == "Home";
				p.portrait = std::stoi(cells[2]);
				p.shooting = std::stoi(cells[3]);
				p.passing = std::stoi(cells[4]);
				p.speed = std::stoi(cells[5]);
				p.defence = std::stoi(cells[6]);
			}
			catch (...) {}
			++row;
		}
		return profiles;
	}
}
