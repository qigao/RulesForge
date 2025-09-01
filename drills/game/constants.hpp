#ifndef CONSTANTS_HPP
#define CONSTANTS_HPP

#include <string_view>

namespace constants {

    using namespace std::literals;

    // Simplified rule names to match our engine's capabilities
    namespace RuleName {
        constexpr auto DragonSlayer = "Dragon Slayer"sv;
        constexpr auto NoviceExplorer = "Novice Explorer"sv;
        constexpr auto MonsterSlayerApprentice = "Monster Slayer Apprentice"sv;
    }   // namespace RuleName

    namespace FactType {
        constexpr auto Player = "Player"sv;
        constexpr auto PlayerEvent = "PlayerEvent"sv;
        constexpr auto GameState = "GameState"sv;   // A singleton fact for global state
    }   // namespace FactType

    namespace Field {
        // Common
        constexpr auto Type = "type"sv;
        constexpr auto Id = "id"sv;
        constexpr auto PlayerId = "playerId"sv;
        // PlayerEvent
        constexpr auto EventType = "eventType"sv;
        constexpr auto MonsterType = "monsterType"sv;
        constexpr auto ZoneName = "zoneName"sv;
        // GameState
        constexpr auto KillCount = "killCount"sv;
        constexpr auto VisitedZoneCount = "visitedZoneCount"sv;
    }   // namespace Field

    // Lua-callable C++ function names
    namespace LuaBinding {
        constexpr auto GrantAchievement = "grant_achievement"sv;
    }

}   // namespace constants

#endif   // CONSTANTS_HPP
