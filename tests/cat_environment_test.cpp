#include "../src/cat_environment.hpp"
#include "../src/cat_species.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error(std::to_string(__LINE__) + ": " #c); } while (false)
using cat::environment::CursorAction;
using cat::environment::CursorInput;
using cat::environment::CursorReaction;

CursorInput Input() {
    CursorInput input;
    input.enabled = input.available = input.canStart = input.canContinue = true;
    input.catX = 400; input.cursorX = 560; input.cursorHeight = 60;
    input.minX = 96; input.maxX = 960; input.interest = 1; input.roll = 0;
    return input;
}

void ApproachAndContactAtEveryFrameRate() {
    for (double fps : {30., 60., 120., 144.}) {
        CursorReaction reaction;
        auto input = Input();
        std::vector<CursorAction> sequence;
        double pawAt = 0;
        for (int frame = 0; frame <= static_cast<int>(10*fps); ++frame) {
            input.nowMs = static_cast<std::uint64_t>(std::round(frame*1000/fps));
            const auto output = reaction.Step(input);
            if (output.changed) sequence.push_back(output.action);
            CHECK(std::abs(output.velocity) <= 45);
            CHECK(output.direction == 1);
            input.catX += output.velocity/fps;
            if (output.action == CursorAction::Paw) {
                if (!pawAt) pawAt = input.nowMs;
                CHECK(std::abs(input.cursorX - input.catX) <= 48);
                CHECK(output.velocity == 0);
            }
        }
        const std::vector<CursorAction> expected{CursorAction::Watch, CursorAction::Hunt,
            CursorAction::Sniff, CursorAction::Paw, CursorAction::None};
        CHECK(sequence == expected);
        CHECK(pawAt > 4200 && pawAt < 4700);
        CHECK(!reaction.Active());
    }
}

void CursorHeightAndBoundsMatter() {
    for (double height : {-15., 180.}) {
        CursorReaction reaction;
        auto input = Input(); input.cursorHeight = height;
        bool watched = false;
        for (int i = 0; i < 240; ++i) {
            input.nowMs = i*16;
            const auto output = reaction.Step(input);
            watched = watched || output.action == CursorAction::Watch;
            CHECK(output.action == CursorAction::Watch || output.action == CursorAction::None);
            CHECK(output.velocity == 0);
        }
        CHECK(watched);
    }
    CursorReaction edge;
    auto input = Input(); input.catX = 120; input.cursorX = 20;
    for (int i = 0; i < 500; ++i) {
        input.nowMs = i*16;
        const auto output = edge.Step(input);
        input.catX += output.velocity*.016;
        CHECK(input.catX >= input.minX - .001);
    }
}

void FastMovementAbandonsAndDoesNotRestart() {
    CursorReaction reaction;
    auto input = Input();
    CHECK(reaction.Step(input).action == CursorAction::Watch);
    bool abandoned = false;
    for (int i = 1; i <= 12; ++i) {
        input.nowMs = i*20; input.cursorX = i%2 ? 510 : 610;
        const auto output = reaction.Step(input);
        abandoned = abandoned || output.action == CursorAction::Abandon;
    }
    CHECK(abandoned);
    for (int i = 13; i < 600; ++i) {
        input.nowMs = i*20;
        const auto output = reaction.Step(input);
        if (input.nowMs >= 1500) CHECK(output.action == CursorAction::None);
    }
    CursorReaction teleport;
    input = Input(); input.cursorX = 300;
    teleport.Step(input); input.nowMs = 16; input.cursorX = 550;
    CHECK(teleport.Step(input).action == CursorAction::Abandon);
}

void ExplicitWorkModeAndManualCommandsTakePrecedence() {
    CursorReaction reaction;
    auto input = Input(); input.enabled = false;
    CHECK(reaction.Step(input).action == CursorAction::None);
    input.enabled = true; input.canStart = false; input.nowMs = 16;
    CHECK(reaction.Step(input).action == CursorAction::None);
    input.canStart = true; input.nowMs = 32;
    CHECK(reaction.Step(input).action == CursorAction::Watch);
    input.canContinue = false; input.nowMs = 48;
    CHECK(reaction.Step(input).action == CursorAction::None);
    input.canContinue = true; input.nowMs = 64;
    CHECK(reaction.Step(input).action == CursorAction::None);
    reaction.Cancel(input.nowMs);
    CHECK(!reaction.Active());
}

void PauseAndInvalidSamplesCannotCauseLatePaw() {
    CursorReaction reaction;
    auto input = Input(); input.cursorX = 430;
    reaction.Step(input);
    for (int i = 1; i <= 50; ++i) { input.nowMs = i*16; reaction.Step(input); }
    CHECK(reaction.Action() == CursorAction::Sniff);
    reaction.Pause(5000);
    input.nowMs += 5000;
    CHECK(reaction.Step(input).action == CursorAction::Sniff);
    input.nowMs = 1;
    CHECK(reaction.Step(input).action == CursorAction::None);
    CursorReaction invalid;
    input = Input(); invalid.Step(input);
    input.cursorX = std::numeric_limits<double>::quiet_NaN(); input.nowMs = 16;
    CHECK(invalid.Step(input).action == CursorAction::None);
    CursorReaction stall;
    input = Input(); stall.Step(input); input.nowMs = 10000;
    CHECK(stall.Step(input).action == CursorAction::None);
}

void LandingSelectionUsesReachabilityAndKeepsFailedDestination() {
    double target = -9;
    CHECK(cat::environment::ChooseLanding(200, 450, 750,
        [](double x) { return x >= 590 && x <= 620; }, target));
    CHECK(target == 600);
    target = -9;
    CHECK(!cat::environment::ChooseLanding(200, 450, 750,
        [](double) { return false; }, target));
    CHECK(target == -9);
    CHECK(!cat::environment::ChooseLanding(200, 100, 200,
        [](double) { return true; }, target));
    CHECK(cat::environment::BalanceStrength(0, 0) == 0);
    CHECK(cat::environment::BalanceStrength(80, 0) > .3);
    CHECK(cat::environment::BalanceStrength(0, 500) == 1);
}

void StillCursorGetsRareShortSitAtEveryFrameRate() {
    for (double fps : {30., 60., 120., 144.}) {
        CursorReaction reaction;
        auto input = Input(); input.cursorX = 430; input.cursorHeight = 30;
        std::vector<CursorAction> sequence;
        std::uint64_t sitAt = 0, releasedAt = 0;
        for (int frame = 0; frame <= static_cast<int>(10*fps); ++frame) {
            input.nowMs = static_cast<std::uint64_t>(std::round(frame*1000/fps));
            const auto output = reaction.Step(input);
            if (output.changed) sequence.push_back(output.action);
            input.catX += output.velocity/fps;
            CHECK(input.catX >= input.minX && input.catX <= input.maxX);
            if (output.action == CursorAction::Sit) {
                if (!sitAt) sitAt = input.nowMs;
                CHECK(std::abs(input.catX - input.cursorX) <= 2);
                CHECK(output.velocity == 0);
            }
            if (sitAt && !reaction.Active() && !releasedAt) releasedAt = input.nowMs;
        }
        const std::vector<CursorAction> expected{CursorAction::Watch, CursorAction::Sniff,
            CursorAction::ApproachSit, CursorAction::Sit, CursorAction::None};
        CHECK(sequence == expected);
        CHECK(sitAt > 1700 && sitAt < 3500);
        CHECK(releasedAt - sitAt >= 2200 && releasedAt - sitAt <= 2240);
    }
}

void SittingRequiresStillLowReachableCursorAndEndsWhenUserMoves() {
    for (int unsuitable = 0; unsuitable < 3; ++unsuitable) {
        CursorReaction reaction;
        auto input = Input(); input.cursorX = 430; input.cursorHeight = 30;
        if (unsuitable == 0) input.cursorHeight = 70;
        if (unsuitable == 1) { input.cursorX = 80; input.catX = 110; }
        if (unsuitable == 2) input.roll = .4; // Ordinary outcome, not rare sitting.
        for (int i = 0; i < 500; ++i) {
            input.nowMs = i*16;
            const auto output = reaction.Step(input);
            CHECK(output.action != CursorAction::Sit && output.action != CursorAction::ApproachSit);
        }
    }
    for (bool disable : {false, true}) {
        CursorReaction reaction;
        auto input = Input(); input.cursorX = 430; input.cursorHeight = 30;
        for (int i = 0; i < 400 && reaction.Action() != CursorAction::Sit; ++i) {
            input.nowMs = i*16;
            input.catX += reaction.Step(input).velocity*.016;
        }
        CHECK(reaction.Action() == CursorAction::Sit);
        input.nowMs += 16;
        if (disable) input.enabled = false;
        else input.cursorX += 11;
        CHECK(reaction.Step(input).action == CursorAction::None);
    }
}

void RareSitCooldownAndSpeciesInterestAreReal() {
    CursorReaction reaction;
    auto input = Input(); input.cursorX = 430; input.cursorHeight = 30;
    std::vector<std::uint64_t> sits;
    for (int i = 0; i < 15000; ++i) {
        input.nowMs = i*16;
        const auto output = reaction.Step(input);
        input.catX += output.velocity*.016;
        if (output.changed && output.action == CursorAction::ApproachSit) sits.push_back(input.nowMs);
    }
    CHECK(sits.size() == 2);
    CHECK(sits[1] - sits[0] >= 180000);
    std::uint64_t creamHunt = 0, dogHunt = 0;
    for (auto character : {cat::Character::Cream, cat::Character::Dog}) {
        CursorReaction sample;
        auto opportunity = Input();
        opportunity.interest = .5 * cat::species::ProfileFor(character).cursorInterest;
        opportunity.roll = .999; // Even an unlucky roll must visibly acknowledge ON.
        CHECK(sample.Step(opportunity).action == CursorAction::Watch);
        for (int i = 1; i < 100; ++i) {
            opportunity.nowMs = i*16;
            if (sample.Step(opportunity).action != CursorAction::Hunt) continue;
            (character == cat::Character::Dog ? dogHunt : creamHunt) = opportunity.nowMs;
            break;
        }
    }
    CHECK(dogHunt > 0 && dogHunt < creamHunt);
}

void WindowRiseBudgetUsesWorkAreaAndSpecies() {
    using cat::environment::WindowJumpRiseLimit;
    CHECK(WindowJumpRiseLimit(1200, 1) == 900);
    CHECK(WindowJumpRiseLimit(1200, .78) == 702);
    CHECK(WindowJumpRiseLimit(1200, 1.18) == 1062);
    CHECK(WindowJumpRiseLimit(1000, 1) == 795);
    CHECK(WindowJumpRiseLimit(1200, 1) >= 800);
    CHECK(WindowJumpRiseLimit(1200, .78) < 800);
    CHECK(WindowJumpRiseLimit(1200, 100) <= cat::environment::WindowJumpRiseLimit(1200, 1.2));
    CHECK(WindowJumpRiseLimit(100, 1) == 0);
    CHECK(WindowJumpRiseLimit(1200, 0) == 0);
    CHECK(WindowJumpRiseLimit(std::numeric_limits<double>::quiet_NaN(), 1) == 0);
}
}

int main() {
    try {
        ApproachAndContactAtEveryFrameRate(); CursorHeightAndBoundsMatter();
        FastMovementAbandonsAndDoesNotRestart(); ExplicitWorkModeAndManualCommandsTakePrecedence();
        PauseAndInvalidSamplesCannotCauseLatePaw(); LandingSelectionUsesReachabilityAndKeepsFailedDestination();
        StillCursorGetsRareShortSitAtEveryFrameRate(); SittingRequiresStillLowReachableCursorAndEndsWhenUserMoves();
        RareSitCooldownAndSpeciesInterestAreReal();
        WindowRiseBudgetUsesWorkAreaAndSpecies();
        std::cout << "Environment reactions: " << checks << " checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
