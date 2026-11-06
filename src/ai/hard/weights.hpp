#pragma once

// M44 (D-054): every weight level Hard plays with, in one place. Tuned with Release arena runs; the values, what
// was swept and the results are recorded in D-054 ("M44 notes"). Normal and Easy don't read this struct: Normal
// keeps M43's MoveWeights{} defaults, so it stays the regression baseline.

#include <cstddef>

#include "normal/move_score.hpp"

namespace wh40k::ai::hard {

// The game-aware evaluator (evaluator.hpp): the chain's payoffs on top of V = P(win) - P(loss).
struct ChainWeights {
    // Time preference (D-007): a win in half-turn h of H is worth tempo x (H - h) / H more, a loss that much less.
    // The winning side pushes for the wipe-out sooner, the losing side holds on for longer (and for the draw).
    double tempo = 0.2;
    // Material at the end of the chain: material x (our share of wounds left - theirs). A tie-break between lines
    // that end the same way (as the winner, fewer wounds lost; as the loser, more wounds dealt).
    double material = 0.05;
    friend bool operator==(const ChainWeights&, const ChainWeights&) = default;
};

// The lookahead (lookahead.hpp): an engine-backed Monte Carlo of the rest of our turn and the enemy's reply.
// D-054: its budget counts simulated actions, never time.
struct LookaheadWeights {
    std::size_t plans = 3;               // move plans (per move) checked by the Monte Carlo
    std::size_t rollouts = 6;            // rollouts per plan (the same seeds for every plan)
    std::size_t rollout_actions = 400;   // simulated actions per rollout at most
    std::size_t budget_actions = 6000;   // simulated actions per Monte Carlo (all its rollouts) at most
    double share = 0.5;                  // a plan's value: (1 - share) x the chain's value + share x the rollouts'
    friend bool operator==(const LookaheadWeights&, const LookaheadWeights&) = default;
};

struct BotWeights {
    normal::MoveWeights move{};  // M43's whole-turn score: Hard ranks its move candidates with it first
    ChainWeights chain{};
    LookaheadWeights lookahead{};
    std::size_t static_plans = 4;  // move candidates simulated in full and valued with the chain, per move
    // Hard starts from Normal's answer (M41-M43) and only plays something else when the evaluator values it more
    // than `margin` higher (score units; one half-turn of tempo is tempo / 10 in a five-round game). Negative:
    // Normal's answer is just one more candidate.
    double margin = 0.01;
};

}  // namespace wh40k::ai::hard
