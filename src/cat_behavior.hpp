#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace cat::behavior {

enum class Action {
    Idle, Walk, Sleep, Groom, Stretch, Watch, Stalk, Pounce, Zoomies, TailChase,
    Punch, Jump, EarFlick, TailRaise, BellyRoll, Loaf, Knead, Peek, Scratch, Sneeze
};

enum class Archetype { Affectionate, Cool, Curious, Mischievous, Sleepy, Energetic };

inline const wchar_t* ArchetypeName(Archetype type) noexcept {
    switch (type) {
        case Archetype::Affectionate: return L"개냥이";
        case Archetype::Cool: return L"시크냥";
        case Archetype::Curious: return L"호기심쟁이";
        case Archetype::Mischievous: return L"장난꾸러기";
        case Archetype::Sleepy: return L"잠보";
        case Archetype::Energetic: return L"우다다냥";
        default: return L"고양이";
    }
}

struct Context {
    std::uint64_t ticks = 0; // Monotonic milliseconds, not a calendar timestamp.
    int ageYears = 0;
    double hunger = 88.0, affection = 85.0;
    int localHour = 12;
    bool cursorNear = false, roamingPaused = false;
};

struct Plan {
    Action action = Action::Idle;
    std::uint64_t durationMs = 15000;
    double speed = 0.0; // Logical pixels per second; the renderer applies DPI scaling.
};

struct Personality {
    double curiosity = 0.5;
    double energy = 0.5;
    double sociability = 0.5;
    double mischief = 0.5;
};

// Birthday-derived traits stay stable for the life of the pet. The daily/random
// seed only changes the order of today's actions, never the cat's personality.
class Brain {
public:
    static constexpr std::uint64_t PunchMinGapMs = 12 * 60 * 1000;
    static constexpr std::uint64_t SurpriseMinGapMs = 4 * 60 * 1000;
    static constexpr std::uint64_t StalkMinGapMs = 90 * 1000;
    static constexpr std::uint64_t GestureMinGapMs = 15 * 1000;

    explicit Brain(std::int64_t bornUtc, std::uint64_t randomSeed = 0)
        : rng_(randomSeed ? randomSeed : Mix(static_cast<std::uint64_t>(bornUtc))) {
        const auto birth = static_cast<std::uint64_t>(bornUtc);
        traits_.curiosity = Trait(birth ^ 0x0F145E98C332A11DULL);
        traits_.energy = Trait(birth ^ 0x52B83617A90284CDULL);
        traits_.sociability = Trait(birth ^ 0x8C01EF22D453AB39ULL);
        traits_.mischief = Trait(birth ^ 0xE7D25A90136CFB48ULL);
        archetype_ = Classify(traits_);
    }

    Personality GetPersonality() const { return traits_; }
    Archetype GetArchetype() const { return archetype_; }

    void Fed(std::uint64_t ticks, bool snack) {
        Observe(ticks);
        pendingGroom_ = true;
        pendingPet_ = false;
        queueCount_ = 0;
        contentUntil_ = Add(ticks, snack ? 60000 : 150000);
    }

    // A user action replaces the current story, rather than letting an old
    // queued jump/walk resume after an unrelated interaction. Personality and
    // cooldowns remain intact; this is transient scheduler state only.
    void Interrupt(std::uint64_t ticks) {
        Observe(ticks);
        queueCount_ = 0;
        pendingGroom_ = pendingPet_ = false;
        lastAction_ = Action::Idle;
    }

    void Pet(std::uint64_t ticks) {
        Observe(ticks);
        queueCount_ = 0;
        lastAction_ = Action::Idle;
        pendingPet_ = true;
    }

    void MarkPunch(std::uint64_t ticks) {
        Observe(ticks);
        nextPunch_ = Add(ticks, PunchDelay());
        nextSurprise_ = std::max(nextSurprise_, Add(ticks, SurpriseMinGapMs));
        queueCount_ = 0;
        pendingGroom_ = pendingPet_ = false;
        lastAction_ = Action::Punch;
    }

    Plan Next(const Context& context) {
        Observe(context.ticks);
        const auto now = context.ticks;
        const bool paused = context.roamingPaused;
        const int age = std::clamp(context.ageYears, 0, 100);
        const double ageActivity = age == 0 ? 1.15 : (age < 7 ? 1.0 : std::max(0.45, 1.0 - (age - 6) * 0.065));
        const double hunger = std::isfinite(context.hunger) ? std::clamp(context.hunger, 0.0, 100.0) : 88.0;
        const double affection = std::isfinite(context.affection) ? std::clamp(context.affection, 0.0, 100.0) : 85.0;
        const int hour = (context.localHour % 24 + 24) % 24;
        const bool twilight = (hour >= 5 && hour < 8) || (hour >= 18 && hour < 21);
        const bool midday = hour >= 11 && hour < 16;
        const bool content = now < contentUntil_;

        if (pendingGroom_) {
            pendingGroom_ = false;
            pendingPet_ = false;
            return Commit(Action::Groom, ageActivity);
        }
        if (pendingPet_) {
            pendingPet_ = false;
            if (traits_.sociability > .58 && Unit() < .55) {
                Queue(Action::Loaf);
                return Commit(Action::Knead, ageActivity);
            }
            return Commit(Unit() < 0.45 + traits_.sociability * 0.4 ? Action::Watch : Action::Groom, ageActivity);
        }
        while (queueCount_ > 0) {
            const Action queued = PopQueue();
            // Conditions may have changed since the story was selected.
            // In particular, pausing after zoomies must not launch its jump.
            if (!paused || !RequiresRoaming(queued)) return Commit(queued, ageActivity);
        }

        // Natural micro-sequences after actions that were not explicitly queued.
        if (lastAction_ == Action::Sleep) return Commit(Action::Stretch, ageActivity);
        if (lastAction_ == Action::Punch) return Commit(Action::Idle, ageActivity);
        if (lastAction_ == Action::Stalk)
            return Commit(!paused && context.cursorNear ? Action::Pounce : Action::Watch, ageActivity);
        if (lastAction_ == Action::Pounce) return Commit(Action::Watch, ageActivity);
        if (lastAction_ == Action::EarFlick || lastAction_ == Action::TailRaise)
            return Commit(Unit() < 0.65 ? Action::Idle : Action::Watch, ageActivity);
        if (lastAction_ == Action::Zoomies || lastAction_ == Action::TailChase)
            return Commit(Unit() < 0.7 ? Action::Groom : Action::Idle, ageActivity);

        const double archetypeEnergy = archetype_ == Archetype::Energetic ? 1.3 : archetype_ == Archetype::Sleepy ? .75 : 1.0;
        const double archetypeRest = archetype_ == Archetype::Sleepy ? 1.5 : archetype_ == Archetype::Energetic ? .72 : 1.0;
        const double lively = ageActivity * (0.65 + traits_.energy * 0.7) * (twilight ? 1.4 : 1.0) *
            (content ? 0.65 : 1.0) * archetypeEnergy;
        const double rest = (midday ? 1.5 : (twilight ? 0.6 : 1.0)) * (1.0 + std::min(age, 20) * 0.025) *
            (content ? 1.35 : 1.0) * archetypeRest;
        const bool canSurprise = !paused && now >= nextSurprise_ && !content;
        const bool canStalk = !paused && context.cursorNear && now >= nextStalk_ && !content;
        const bool canPunch = !paused && now >= nextPunch_ && now >= nextSurprise_ && !content;
        const bool canGesture = now >= nextGesture_;
        const double affectionate = archetype_ == Archetype::Affectionate ? 1.65 : 1.0;
        const double curious = archetype_ == Archetype::Curious ? 1.65 : 1.0;
        const double mischievous = archetype_ == Archetype::Mischievous ? 1.65 : 1.0;
        const double sleepy = archetype_ == Archetype::Sleepy ? 1.65 : 1.0;
        const double cool = archetype_ == Archetype::Cool ? 1.45 : 1.0;

        const std::array<std::pair<Action, double>, 19> weights{{
            {Action::Idle, 23.0 * cool},
            {Action::Walk, paused ? 0.0 : 23.0 * lively},
            {Action::Sleep, 20.0 * rest},
            {Action::Groom, lastAction_ == Action::Groom ? 3.0 : 9.5},
            {Action::Stretch, lastAction_ == Action::Stretch ? 0.0 : 2.8},
            {Action::Watch, (6.0 + (context.cursorNear ? (10.0 + traits_.curiosity * 15.0) : 0.0)
                + (hunger < 45.0 ? 4.0 : 0.0) + (affection > 80.0 ? traits_.sociability * 2.0 : 0.0)) * curious},
            {Action::Stalk, canStalk ? (3.0 + traits_.curiosity * 6.0) * lively * curious : 0.0},
            {Action::Zoomies, canSurprise ? (2.8 + traits_.energy * 4.8) * lively : 0.0},
            {Action::TailChase, canSurprise ? (1.0 + traits_.mischief * 3.0) * ageActivity * mischievous : 0.0},
            {Action::Punch, canPunch ? (5.0 + traits_.mischief * 5.0) * mischievous : 0.0},
            {Action::Jump, canSurprise ? (2.2 + traits_.curiosity * 3.0) * lively : 0.0},
            {Action::EarFlick, canGesture ? 3.2 + traits_.curiosity * 2.0 : 0.0},
            {Action::TailRaise, canGesture ? (3.0 + traits_.sociability * 4.0 + (affection > 80.0 ? 1.0 : 0.0)) * affectionate : 0.0},
            {Action::BellyRoll, canSurprise ? (1.2 + traits_.sociability * 2.2 + traits_.mischief) * ageActivity * affectionate : 0.0},
            {Action::Loaf, (4.0 + rest * 2.0) * sleepy * cool},
            {Action::Knead, canGesture ? (1.8 + traits_.sociability * 3.5) * affectionate : 0.0},
            {Action::Peek, canSurprise ? (1.5 + traits_.curiosity * 2.8 + traits_.mischief * 1.5) * curious * mischievous : 0.0},
            {Action::Scratch, canGesture ? (1.4 + traits_.curiosity * 1.7 + traits_.mischief) * curious : 0.0},
            {Action::Sneeze, canGesture ? 0.8 : 0.0}
        }};

        double total = 0.0;
        for (const auto& item : weights) total += item.second;
        double draw = Unit() * total;
        Action action = Action::Idle;
        for (const auto& item : weights) {
            if (draw < item.second) { action = item.first; break; }
            draw -= item.second;
        }

        if (action == Action::Punch) MarkPunch(now);
        if (action == Action::Zoomies || action == Action::TailChase || action == Action::Jump ||
            action == Action::BellyRoll || action == Action::Peek) {
            nextSurprise_ = Add(now, Range(SurpriseMinGapMs, 8 * 60 * 1000));
        }
        if (action == Action::Stalk) nextStalk_ = Add(now, Range(StalkMinGapMs, 4 * 60 * 1000));
        if (action == Action::EarFlick || action == Action::TailRaise || action == Action::Knead ||
            action == Action::Scratch || action == Action::Sneeze) {
            nextGesture_ = Add(now, Range(GestureMinGapMs, 35000));
        }

        // A few actions deliberately become tiny stories instead of isolated poses.
        if (action == Action::Zoomies) {
            if (Unit() < .55) Queue(Action::Jump);
            Queue(Action::Watch);
        } else if (action == Action::Jump) {
            Queue(Unit() < .55 ? Action::Watch : Action::Groom);
        } else if (action == Action::BellyRoll) {
            Queue(Action::Groom);
        } else if (action == Action::Knead) {
            Queue(Action::Loaf);
        } else if (action == Action::Peek) {
            Queue(Unit() < .45 && !paused ? Action::Walk : Action::Watch);
        } else if (action == Action::Scratch) {
            Queue(Action::Groom);
        } else if (action == Action::Sneeze) {
            Queue(Action::Idle);
        } else if (action == Action::TailRaise && !paused && Unit() < .45) {
            Queue(Action::Walk);
        }
        return Commit(action, ageActivity);
    }

private:
    std::uint64_t rng_;
    Personality traits_{};
    Archetype archetype_ = Archetype::Cool;
    bool initialized_ = false, pendingGroom_ = false, pendingPet_ = false;
    std::uint64_t seen_ = 0, nextPunch_ = 0, nextSurprise_ = 0, nextStalk_ = 0, nextGesture_ = 0, contentUntil_ = 0;
    Action lastAction_ = Action::Idle;
    std::array<Action, 3> queue_{};
    int queueCount_ = 0;

    static bool RequiresRoaming(Action action) {
        return action == Action::Walk || action == Action::Stalk ||
            action == Action::Pounce || action == Action::Zoomies || action == Action::Jump;
    }
    static std::uint64_t Mix(std::uint64_t n) {
        n = (n ^ (n >> 30)) * 0xBF58476D1CE4E5B9ULL;
        n = (n ^ (n >> 27)) * 0x94D049BB133111EBULL;
        return n ^ (n >> 31);
    }
    static double Trait(std::uint64_t n) {
        return 0.18 + 0.64 * static_cast<double>(Mix(n) >> 11) * (1.0 / 9007199254740992.0);
    }
    static Archetype Classify(const Personality& p) {
        const std::array<std::pair<Archetype, double>, 6> scores{{
            {Archetype::Affectionate, p.sociability},
            {Archetype::Cool, 1.08 - (p.curiosity + p.energy + p.sociability + p.mischief) * .23},
            {Archetype::Curious, p.curiosity},
            {Archetype::Mischievous, p.mischief},
            {Archetype::Sleepy, 1.0 - p.energy},
            {Archetype::Energetic, p.energy}
        }};
        auto best = scores.front();
        for (const auto& score : scores) if (score.second > best.second) best = score;
        return best.first;
    }
    static std::uint64_t Add(std::uint64_t a, std::uint64_t b) {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        return a > maximum - b ? maximum : a + b;
    }
    double Unit() {
        rng_ += 0x9E3779B97F4A7C15ULL;
        return static_cast<double>(Mix(rng_) >> 11) * (1.0 / 9007199254740992.0);
    }
    std::uint64_t Range(std::uint64_t low, std::uint64_t high) {
        return low + static_cast<std::uint64_t>(Unit() * static_cast<double>(high - low + 1));
    }
    std::uint64_t PunchDelay() { return Range(PunchMinGapMs, 22 * 60 * 1000); }
    void Queue(Action action) {
        if (queueCount_ < static_cast<int>(queue_.size())) queue_[queueCount_++] = action;
    }
    Action PopQueue() {
        const Action action = queue_[0];
        for (int i = 1; i < queueCount_; ++i) queue_[i - 1] = queue_[i];
        --queueCount_;
        return action;
    }
    void Observe(std::uint64_t ticks) {
        if (!initialized_) {
            initialized_ = true;
            nextPunch_ = Add(ticks, PunchDelay());
            nextSurprise_ = Add(ticks, Range(45000, 100000));
            nextStalk_ = Add(ticks, Range(10000, 25000));
            nextGesture_ = Add(ticks, Range(GestureMinGapMs, 35000));
        } else if (ticks < seen_) {
            auto rebase = [&](std::uint64_t deadline) { return Add(ticks, deadline > seen_ ? deadline - seen_ : 0); };
            nextPunch_ = rebase(nextPunch_);
            nextSurprise_ = rebase(nextSurprise_);
            nextStalk_ = rebase(nextStalk_);
            nextGesture_ = rebase(nextGesture_);
            contentUntil_ = rebase(contentUntil_);
        }
        seen_ = ticks;
    }
    Plan Commit(Action action, double ageActivity) {
        Plan result; result.action = action;
        switch (action) {
        case Action::Idle: result.durationMs = Range(10000, 38000); break;
        case Action::Walk: result.durationMs = Range(5000, 17000); result.speed = (15.0 + Unit() * 14.0) * ageActivity; break;
        case Action::Sleep: result.durationMs = Range(35000, 120000); break;
        case Action::Groom: result.durationMs = Range(4000, 8000); break;
        case Action::Stretch: result.durationMs = Range(2400, 3800); break;
        case Action::Watch: result.durationMs = Range(5000, 15000); break;
        case Action::Stalk: result.durationMs = Range(1800, 3200); result.speed = (9.0 + Unit() * 7.0) * ageActivity; break;
        case Action::Pounce: result.durationMs = 1050; result.speed = (70.0 + Unit() * 30.0) * ageActivity; break;
        case Action::Zoomies: result.durationMs = Range(3000, 5000); result.speed = (130.0 + Unit() * 70.0) * ageActivity; break;
        case Action::TailChase: result.durationMs = Range(2800, 4200); break;
        case Action::Punch: result.durationMs = 2700; break;
        case Action::Jump: result.durationMs = 1450; break;
        case Action::EarFlick: result.durationMs = 2200; break;
        case Action::TailRaise: result.durationMs = 3400; break;
        case Action::BellyRoll: result.durationMs = Range(3600, 5200); break;
        case Action::Loaf: result.durationMs = Range(8000, 26000); break;
        case Action::Knead: result.durationMs = Range(4200, 6500); break;
        case Action::Peek: result.durationMs = Range(2600, 4300); break;
        case Action::Scratch: result.durationMs = Range(2400, 3800); break;
        case Action::Sneeze: result.durationMs = 1700; break;
        }
        lastAction_ = action;
        return result;
    }
};

} // namespace cat::behavior
