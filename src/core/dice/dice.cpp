#include "core/dice/dice.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace wh40k::core {

namespace {

// SplitMix64 output number `index` for `seed` (Steele, Lea & Flood 2014; public-domain constants).
std::uint64_t splitmix64_at(std::uint64_t seed, std::uint64_t index) {
    std::uint64_t z = seed + (index + 1) * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// 2^64 mod 6 == 4, so rejecting the top 4 values makes x % 6 unbiased.
constexpr std::uint64_t kUnbiasedLimit = 0xFFFFFFFFFFFFFFFCull;

void check_face(int v) {
    if (v < 1 || v > 6) {
        throw std::logic_error("ScriptedDice: roll " + std::to_string(v) + " is not a D6 result");
    }
}

}  // namespace

int SeededDice::roll_d6() {
    for (;;) {
        const std::uint64_t x = splitmix64_at(state_->seed, state_->position++);
        if (x < kUnbiasedLimit) {
            return static_cast<int>(x % 6u) + 1;
        }
    }
}

ScriptedDice::ScriptedDice(std::initializer_list<int> rolls) : ScriptedDice(std::vector<int>(rolls)) {}

ScriptedDice::ScriptedDice(std::vector<int> rolls) : rolls_(std::move(rolls)) {
    for (const int v : rolls_) {
        check_face(v);
    }
}

int ScriptedDice::roll_d6() {
    if (next_ >= rolls_.size()) {
        throw std::logic_error("ScriptedDice: script exhausted after " + std::to_string(rolls_.size()) +
                               " rolls");
    }
    return rolls_[next_++];
}

int roll_d3(DiceSource& dice) {
    return (dice.roll_d6() + 1) / 2;
}

TwoD6 roll_2d6(DiceSource& dice) {
    TwoD6 r;
    r.dice[0] = dice.roll_d6();
    r.dice[1] = dice.roll_d6();
    return r;
}

}  // namespace wh40k::core
