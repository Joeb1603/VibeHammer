#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace wh40k::core {

// Seeded PRNG state, stored in GameState (CLAUDE.md, Determinism). The generator is counter-based
// SplitMix64: output n depends only on (seed, n), so this pair is the generator's entire state and
// round-trips through JSON exactly.
struct DiceState {
    std::uint64_t seed = 0;
    std::uint64_t position = 0;  // number of 64-bit outputs consumed so far
    friend bool operator==(const DiceState&, const DiceState&) = default;
};

// 01.05: all randomness in the engine goes through this interface.
class DiceSource {
public:
    virtual ~DiceSource() = default;
    // One D6: 1..6.
    [[nodiscard]] virtual int roll_d6() = 0;

protected:
    DiceSource() = default;
    DiceSource(const DiceSource&) = default;
    DiceSource& operator=(const DiceSource&) = default;
};

// Production dice: advances the DiceState it is given (normally GameState::dice).
class SeededDice final : public DiceSource {
public:
    explicit SeededDice(DiceState& state) : state_(&state) {}
    [[nodiscard]] int roll_d6() override;

private:
    DiceState* state_;
};

// Test dice: returns exactly the scripted rolls, in order. Rolling past the end, or scripting a value
// outside 1..6, is a programmer error (std::logic_error).
class ScriptedDice final : public DiceSource {
public:
    ScriptedDice(std::initializer_list<int> rolls);
    explicit ScriptedDice(std::vector<int> rolls);
    [[nodiscard]] int roll_d6() override;
    [[nodiscard]] std::size_t consumed() const noexcept { return next_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return rolls_.size() - next_; }

private:
    std::vector<int> rolls_;
    std::size_t next_ = 0;
};

// 01.05: D3 = one D6 halved, rounding up.
[[nodiscard]] int roll_d3(DiceSource& dice);

// 01.05: 2D6 = roll two D6 and add them.
struct TwoD6 {
    std::array<int, 2> dice{};
    [[nodiscard]] int total() const noexcept { return dice[0] + dice[1]; }
    friend bool operator==(const TwoD6&, const TwoD6&) = default;
};
[[nodiscard]] TwoD6 roll_2d6(DiceSource& dice);

}  // namespace wh40k::core
