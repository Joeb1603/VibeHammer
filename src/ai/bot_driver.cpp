#include "bot_driver.hpp"

#include <chrono>
#include <utility>
#include <variant>

#include "state_hash.hpp"

namespace wh40k::ai {

double speed_factor(BotSpeed speed) {
    switch (speed) {
        case BotSpeed::Slow: return 3.0;  // M48 (D-059): was 2.0
        case BotSpeed::Normal: return 1.0;
        case BotSpeed::Fast: return 0.4;
        case BotSpeed::Instant: return 0.0;
    }
    return 1.0;
}

double base_pause_seconds(const core::GameAction& action) {
    using namespace core::action;
    constexpr double kActivation = 0.8;
    constexpr double kPlacement = 0.12;
    constexpr double kCommit = 0.4;
    constexpr double kReactive = 0.6;
    if (std::holds_alternative<ProposeModelMove>(action)) return kPlacement;
    if (std::holds_alternative<DeclareAllocationOrder>(action) || std::holds_alternative<OrderCasualties>(action) ||
        std::holds_alternative<RemoveModelForCoherency>(action)) {
        return kReactive;
    }
    if (std::holds_alternative<StartMove>(action) || std::holds_alternative<Shoot>(action) ||
        std::holds_alternative<DeclareCharge>(action) || std::holds_alternative<StartChargeMove>(action) ||
        std::holds_alternative<StartPileIn>(action) || std::holds_alternative<SelectToFight>(action) ||
        std::holds_alternative<Fight>(action) || std::holds_alternative<StartConsolidation>(action)) {
        return kActivation;
    }
    return kCommit;  // CommitUnitMove, and the step buttons (End..., Pass, Skip...)
}

double pause_seconds(const core::GameAction& action, BotSpeed speed) {
    return base_pause_seconds(action) * speed_factor(speed);
}

double bot_dice_roll_pause_seconds(BotSpeed speed) { return 0.35 * speed_factor(speed); }
double bot_dice_dwell_seconds(BotSpeed speed) { return 1.2 * speed_factor(speed); }

BotDriver::BotDriver(BotConfig config, BotSpeed speed, Threading threading)
    : bot_(config), config_(config), speed_(speed), threading_(threading) {}

BotDriver::~BotDriver() {
    if (job_ && job_->future.valid()) job_->future.wait();  // join the worker cleanly
}

void BotDriver::invalidate() {
    ++generation_;
    forget_ = true;
    stopped_ = false;
    stop_reasons_.clear();
}

void BotDriver::stop(std::vector<std::string> reasons) {
    stopped_ = true;
    stop_reasons_ = std::move(reasons);
}

void BotDriver::set_config(BotConfig config) {
    config_ = config;
    reconfigure_ = true;
}

void BotDriver::apply_deferred() {
    if (reconfigure_) {
        bot_.set_config(config_);  // also forgets
        reconfigure_ = false;
        forget_ = false;
    }
    if (forget_) {
        bot_.forget();
        forget_ = false;
    }
}

BotChoice BotDriver::decide_now(const core::GameState& state, core::Side side) {
    apply_deferred();
    return bot_.decide(state, side);
}

void BotDriver::start_job(double now, const core::GameState& state, core::Side side, std::uint64_t key) {
    apply_deferred();
    Job job;
    job.generation = generation_;
    job.key = key;
    job.side = side;
    job.seen_at = now;
    if (threading_ == Threading::Synchronous) {
        job.result = bot_.decide(state, side);
    } else {
        // A copy of the state for the worker; the bot is only touched by one worker at a time (a new job
        // starts only once the previous one has finished).
        job.future = std::async(std::launch::async, [this, copy = state, side] { return bot_.decide(copy, side); });
    }
    job_ = std::move(job);
}

std::optional<BotChoice> BotDriver::tick(double now, const DriverInputs& in) {
    if (in.state == nullptr || !in.state->pending || stopped_) return std::nullopt;
    const core::Side side = in.state->pending->side;
    if (!in.bot_sides[core::side_index(side)]) return std::nullopt;  // a human's decision

    Fnv1a keyed;
    keyed.u64(state_hash(*in.state));
    keyed.u64(in.step);
    const std::uint64_t key = keyed.value();
    const bool current = job_ && job_->generation == generation_ && job_->key == key && job_->side == side;
    if (!current) {
        // A stale worker (undo, load, a human's input) finishes before the next one starts; its result is dropped.
        if (job_ && job_->future.valid() &&
            job_->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return std::nullopt;
        }
        start_job(now, *in.state, side, key);
    }
    Job& job = *job_;
    if (!job.result && job.future.valid() &&
        job.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        job.result = job.future.get();
    }
    if (job.delivered) return std::nullopt;
    // The pause runs only while nothing is in the way: a pop-up or a pause restarts it.
    if (in.popup_open) {
        job.seen_at = now;
        return std::nullopt;
    }
    if (in.paused && !in.step_once) {
        job.seen_at = now;
        return std::nullopt;
    }
    if (!job.result) return std::nullopt;  // still thinking
    if (!in.step_once && now - job.seen_at < pause_seconds(job.result->action, speed_)) return std::nullopt;
    job.delivered = true;
    return job.result;
}

bool BotDriver::wait_for_decision(double seconds) {
    if (!job_ || job_->generation != generation_ || job_->delivered) return false;
    if (job_->result) return true;
    if (!job_->future.valid()) return false;
    const auto wait = std::chrono::duration<double>(seconds > 0.0 ? seconds : 0.0);
    if (job_->future.wait_for(wait) != std::future_status::ready) return false;
    job_->result = job_->future.get();
    return true;
}

std::optional<std::string> BotDriver::pending_reason() const {
    if (!job_ || !job_->result || job_->delivered || job_->generation != generation_) return std::nullopt;
    return job_->result->reason;
}

}  // namespace wh40k::ai
