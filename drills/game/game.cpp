// Game.cpp
#include "game.hpp"

#include "constants.hpp"
#include "drools_parser.hpp"   // For build_knowledge_base
#include "errors.hpp"          // For ParsingResult

#include <iostream>
#include <sol/sol.hpp>

namespace {
    // The DRL rules defining game logic.
    std::string const game_achievement_rules = R"rules(
    // --- Fact Type Declarations ---
    declare Player
        id : long
    end

    declare PlayerEvent
        eventType : String
        playerId : long
        zoneName : String
        monsterType : String
    end

    declare GameState
        playerId : long
        killCount : long
    end

    // --- Rules ---
    rule "Dragon Slayer" salience 30
    when
        $p : Player( id == 1 )
        $e : PlayerEvent( eventType == "DefeatedMonster", monsterType == "Dragon", playerId == $p.id )
    then
        -- Call a C++ function bound to Lua to grant the achievement.
        grant_achievement($p.id, "Dragon Slayer")
    end

    rule "Novice Explorer" salience 10
    when
        $p : Player( id == 1 )
        $e : PlayerEvent( eventType == "VisitedZone", zoneName == "Duskwood", playerId == $p.id )
    then
        grant_achievement($p.id, "Novice Explorer")
    end

    rule "Monster Slayer Apprentice" salience 20
    when
        $p : Player( id == 1 )
        // Match the GameState fact when the kill count is exactly 5.
        $gs : GameState( killCount == 5, playerId == $p.id )
    then
        grant_achievement($p.id, "Monster Slayer Apprentice")
    end
)rules";
}   // anonymous namespace

Game::Game() = default;
Game::~Game() = default;

void Game::initialize() {
    std::cout << "[SYSTEM] Initializing game engine...\n";
    setup_engine_and_rules();
    std::cout << "[SYSTEM] Initialization complete.\n\n";
}

void Game::setup_engine_and_rules() {
    ParsingResult result;
    kb_ = build_knowledge_base(game_achievement_rules, result, "game_rules.drl");

    if (!result.success || !kb_) {
        for (auto const& err : result.errors) { std::cerr << err.to_string() << std::endl; }
        throw std::runtime_error("Failed to parse and build game rules.");
    }

    session_ = kb_->create_session();
    setup_lua_bindings();
}

void Game::setup_lua_bindings() {
    // Get a reference to the Lua state managed by the session
    auto& lua = session_->get_lua_state();

    // Bind the C++ grant_achievement member function to a global Lua function.
    // Use a lambda to correctly handle the 'this' pointer.
    lua.set_function(std::string(constants::LuaBinding::GrantAchievement),
                     [this](PlayerId id, std::string const& name) { this->grant_achievement(id, name); });
}

void Game::grant_achievement(PlayerId playerId, std::string const& achievementName) {
    if (granted_achievements_.insert({playerId, achievementName}).second) {
        std::cout << "\n=======================================\n";
        std::cout << "[ACHIEVEMENT] Player " << playerId << " unlocked: " << achievementName << "!\n";
        std::cout << "=======================================\n\n";
    }
}

void Game::run_simulation() {
    using namespace constants;

    std::cout << "--- Starting Game Simulation ---\n";

    // Player ID for the simulation
    constexpr PlayerId player_id = 1;

    // Create the initial state for our player
    auto player_fact = std::make_shared<Fact>();
    player_fact->type = std::string(FactType::Player);
    player_fact->fields = {{std::string(Field::Id), player_id}};

    auto player_state_fact = std::make_shared<Fact>();
    player_state_fact->type = std::string(FactType::GameState);
    player_state_fact->fields = {{std::string(Field::PlayerId), player_id},
                                 {std::string(Field::KillCount), (int64_t)0}};

    // Add initial facts to the engine's working memory
    session_->add_fact(player_fact);
    session_->add_fact(player_state_fact);

    player_visits_zone(player_id, "Elwynn Forest");
    player_visits_zone(player_id, "Westfall");
    session_->fire_all_rules();

    std::cout << "\n[SIM] Player defeats some early monsters...\n";
    for (int i = 0; i < 4; ++i) {
        player_defeats_monster(player_id, "Murloc");
        // Use the proper update mechanism instead of re-asserting
        session_->update_fact(player_state_fact,
                              [i](Fact& f) { f.fields[std::string(Field::KillCount)] = (int64_t)(i + 1); });
        session_->fire_all_rules();
    }

    std::cout << "\n[SIM] Player defeats a 5th monster, triggering an achievement...\n";
    player_defeats_monster(player_id, "Gnoll");
    session_->update_fact(player_state_fact, [](Fact& f) { f.fields[std::string(Field::KillCount)] = (int64_t)5; });
    session_->fire_all_rules();   // "Monster Slayer Apprentice" should fire here.

    std::cout << "\n[SIM] Player enters the third zone...\n";
    player_visits_zone(player_id, "Duskwood");
    session_->fire_all_rules();   // "Novice Explorer" should fire here.

    std::cout << "\n[SIM] Player defeats the Dragon!\n";
    player_defeats_monster(player_id, "Dragon");
    session_->fire_all_rules();   // "Dragon Slayer" should fire here.

    std::cout << "\n--- Game Simulation Finished ---\n";
}

// --- Fact Creation Implementation ---

void Game::player_visits_zone(PlayerId playerId, std::string const& zoneName) {
    auto f = std::make_shared<Fact>();
    f->type = std::string(constants::FactType::PlayerEvent);
    f->fields = {{std::string(constants::Field::EventType), std::string("VisitedZone")},
                 {std::string(constants::Field::PlayerId), playerId},
                 {std::string(constants::Field::ZoneName), zoneName}};
    session_->add_fact(f);
}

void Game::player_defeats_monster(PlayerId playerId, std::string const& monsterType) {
    auto f = std::make_shared<Fact>();
    f->type = std::string(constants::FactType::PlayerEvent);
    f->fields = {{std::string(constants::Field::EventType), std::string("DefeatedMonster")},
                 {std::string(constants::Field::PlayerId), playerId},
                 {std::string(constants::Field::MonsterType), monsterType}};
    session_->add_fact(f);
}

int main() {
    try {
        Game game;
        game.initialize();
        game.run_simulation();
    } catch (std::exception const& e) {
        std::cerr << "A critical error occurred: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
