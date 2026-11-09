#pragma once

// M39 (D-054): pacing the bot in a live game. Presentation only: the driver decides *when* the bot's next
// action is handed to the client (thinking pauses per action kind, scaled by the bot speed, never while a
// pop-up is open, Pause and Step), while *what* it does is Bot::decide, a pure function of the state.
// raylib-free: the client passes the time in. decide runs on a worker thread with a copy of the state; a
// generation counter (bumped by invalidate() on an undo, a new game or a load) drops results that went stale.

#include <array>
#include <cstdint>
#include <future>
#include <optional>
#include <string>

#include "bot.hpp"
#include "core/engine/actions.hpp"
#include "core/state/game_state.hpp"
#include "settings.hpp"

namespace wh40k::ai {

using BotSpeed = client::BotSpeed;

// How much longer (or shorter) every pause is than at Normal speed: Slow 3 (2 before M48, D-059), Normal 1,
// Fast 0.4, Instant 0.
// The one place the speeds are defined (the client scales the bot's dice pop-ups with it too).
[[nodiscard]] double speed_factor(BotSpeed speed);

// The pause before the bot's `action`, at Normal speed: 0.8 s before a unit's activation (selecting it to move,
// shoot, charge or fight; D-081: setting it up), 0.12 s between model placements, 0.4 s before a commit (and other
// step buttons), 0.6 s for reactive allocation and casualty answers.
[[nodiscard]] double base_pause_seconds(const core::GameAction& action);
[[nodiscard]] double pause_seconds(const core::GameAction& action, BotSpeed speed);

// The bot's own dice pop-ups (D-033) while the bots aren't paused (M51, D-062): the pause before the
// roll, and how long the result stays up before it continues (about 1.2 s at Normal speed).
[[nodiscard]] double bot_dice_roll_pause_seconds(BotSpeed speed);
[[nodiscard]] double bot_dice_dwell_seconds(BotSpeed speed);

struct DriverInputs {
    const core::GameState* state = nullptr;  // the live game now (GameSession::state())
    std::uint64_t step = 0;                  // the session's history size (a new decision even if the state repeats)
    std::array<bool, 2> bot_sides{};         // indexed by side_index
    bool popup_open = false;                 // dice pop-ups, the end-of-shooting warning, ...
    bool paused = false;
    bool step_once = false;  // Step: hand over the next action now, paused or not, without its pause
};

class BotDriver {
public:
    enum class Threading { Worker, Synchronous };  // Synchronous: decide on the calling thread (tests, Instant)

    explicit BotDriver(BotConfig config = {}, BotSpeed speed = BotSpeed::Normal,
                       Threading threading = Threading::Worker);
    ~BotDriver();
    BotDriver(const BotDriver&) = delete;
    BotDriver& operator=(const BotDriver&) = delete;

    // The bot's next action, once it is due: the pending decision belongs to a bot side, no pop-up is open,
    // the pause for the action has passed since the decision appeared (or the last pop-up closed), and it
    // isn't paused (unless step_once). Each decision's action is handed over once; submit it through the
    // session. `now` is in seconds, from any steady clock.
    [[nodiscard]] std::optional<BotChoice> tick(double now, const DriverInputs& inputs);

    // Drop any decision in progress or waiting (an undo, a new game, a load) and the bot's memo.
    void invalidate();
    // The session rejected the bot's action (it shouldn't): stop until invalidate(), showing `reasons`.
    void stop(std::vector<std::string> reasons);
    [[nodiscard]] bool stopped() const noexcept { return stopped_; }
    [[nodiscard]] const std::vector<std::string>& stop_reasons() const noexcept { return stop_reasons_; }

    void set_speed(BotSpeed speed) noexcept { speed_ = speed; }
    [[nodiscard]] BotSpeed speed() const noexcept { return speed_; }
    void set_config(BotConfig config);
    [[nodiscard]] BotConfig config() const noexcept { return config_; }
    void set_threading(Threading t) noexcept { threading_ = t; }

    // M54: on the worker thread, waits up to `seconds` for the decision in progress (none started: no wait). True
    // once a decision is ready to be handed over by tick (its pause aside). The client's Instant speed uses it to
    // take several quick decisions in one frame without ever blocking for long on a slow one.
    bool wait_for_decision(double seconds);

    // What the bot is about to do, once it has decided (for "Bot (Tyranids) is thinking... - <reason>").
    [[nodiscard]] std::optional<std::string> pending_reason() const;
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

    // Bot::decide on the calling thread, for --bot-steps and scripted runs (the memo is shared with tick).
    // Precondition: no worker is running (call it before the first tick, or after waiting()).
    [[nodiscard]] BotChoice decide_now(const core::GameState& state, core::Side side);

private:
    struct Job {
        std::uint64_t generation = 0;
        std::uint64_t key = 0;  // state_hash of the state decided on
        core::Side side = core::Side::A;
        double seen_at = 0.0;   // when the decision appeared, or the last pop-up closed (the pause starts here)
        std::future<BotChoice> future;
        std::optional<BotChoice> result;
        bool delivered = false;
    };

    void start_job(double now, const core::GameState& state, core::Side side, std::uint64_t key);
    void apply_deferred();  // forget / reconfigure the bot (only while no worker is running)

    Bot bot_;
    BotConfig config_;
    bool reconfigure_ = false;
    bool forget_ = false;
    BotSpeed speed_;
    Threading threading_;
    std::uint64_t generation_ = 0;
    std::optional<Job> job_;
    bool stopped_ = false;
    std::vector<std::string> stop_reasons_;
};

}  // namespace wh40k::ai
