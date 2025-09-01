#ifndef GAME_HPP
#define GAME_HPP

#include "knowledge_base.hpp"
#include "stateful_session.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>

// Forward-declare to avoid including sol.hpp in the header
namespace sol {
    class state;
}

using PlayerId = int64_t;

class Game {
public:
    Game();
    ~Game();

    void initialize();
    void run_simulation();

private:
    // --- Engine and State ---
    std::shared_ptr<KnowledgeBase> kb_;
    std::unique_ptr<StatefulSession> session_;
    std::set<std::pair<PlayerId, std::string>> granted_achievements_;

    // --- Private Setup Methods ---
    void setup_engine_and_rules();
    void setup_lua_bindings();

    // --- C++ Logic callable from Lua ---
    void grant_achievement(PlayerId playerId, std::string const& achievementName);

    // --- Fact Creation API ---
    void player_visits_zone(PlayerId playerId, std::string const& zoneName);
    void player_defeats_monster(PlayerId playerId, std::string const& monsterType);
};

#endif   // GAME_HPP
