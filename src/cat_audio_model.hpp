#pragma once

#include "pet_state.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace cat::audio {

// Resource IDs are 301 + the cue value. Keep the original eight values stable
// and append variants, so each user-supplied sound has its own immutable slot.
enum class Cue {
    Hungry, Meal, Treat, Refuse, Pet, Jump, Punch, Wake,
    FoodRequest, MealFish, MealNogari, PunchSecond, Gesture, Greeting,
    Count
};
inline constexpr std::size_t CueCount = static_cast<std::size_t>(Cue::Count);
inline bool IsValid(Cue cue) noexcept {
    return static_cast<unsigned>(cue) < static_cast<unsigned>(Cue::Count);
}

struct Settings { bool enabled = true; unsigned volume = 25; };
inline bool IsValid(const Settings& value) noexcept { return value.volume <= 100; }

// Sound preferences are separate from the lifetime pet and nutrition records.
inline std::string SerializeSettings(const Settings& settings) {
    if (!IsValid(settings)) return {};
    const std::string body = std::string("schema=1\nenabled=") + (settings.enabled ? "1\n" : "0\n") +
        "volume=" + std::to_string(settings.volume) + "\n";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << body << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << cat::Checksum(body) << '\n';
    return out.str();
}

inline bool ParseSettings(const std::string& text, Settings& destination, std::string& error) {
    const auto fail = [&](const char* why) { error = why; return false; };
    if (text.empty() || text.size() > 1024) return fail("Empty or oversized sound settings");
    const auto at = text.rfind("checksum=");
    if (at == std::string::npos || at == 0 || text[at - 1] != '\n') return fail("Missing sound checksum");
    const std::string body = text.substr(0, at);
    std::string hash = text.substr(at + 9);
    const bool crlf = body.size() >= 2 && body[body.size() - 2] == '\r';
    if (!hash.empty() && hash.back() == '\n') {
        hash.pop_back();
        const bool hashCrlf = !hash.empty() && hash.back() == '\r';
        if (hashCrlf != crlf) return fail("Mixed sound setting newlines");
        if (hashCrlf) hash.pop_back();
    }
    if (hash.size() != 16 || hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return fail("Malformed sound checksum");
    std::uint64_t expected = 0;
    std::istringstream in(hash);
    in.imbue(std::locale::classic());
    in >> std::hex >> expected;
    if (!in || expected != cat::Checksum(body)) return fail("Sound checksum mismatch");
    const std::string ending = crlf ? "\r\n" : "\n";
    for (bool enabled : {false, true}) {
        const std::string prefix = "schema=1" + ending + "enabled=" + (enabled ? "1" : "0") + ending + "volume=";
        if (body.compare(0, prefix.size(), prefix) != 0 || body.size() <= prefix.size() + ending.size()) continue;
        const std::string number = body.substr(prefix.size(), body.size() - prefix.size() - ending.size());
        if (number.size() > 3 || number.find_first_not_of("0123456789") != std::string::npos ||
            (number.size() > 1 && number[0] == '0')) continue;
        unsigned volume = 0;
        for (char digit : number) volume = volume * 10 + static_cast<unsigned>(digit - '0');
        const Settings parsed{enabled, volume};
        if (!IsValid(parsed)) continue;
        if (body != prefix + std::to_string(volume) + ending) continue;
        destination = parsed;
        error.clear();
        return true;
    }
    return fail("Unsupported or malformed sound settings");
}

inline bool ParseSettings(const std::string& text, Settings& destination) {
    std::string error;
    return ParseSettings(text, destination, error);
}

// The caller commits only after a real buffer has been queued successfully.
// Muting, missing resources, missing hardware, or a rejected cue consume no slot.
class RateGate {
    bool hasPlayed_ = false, hasAutomatic_ = false;
    std::uint64_t lastPlayed_ = 0, lastAutomatic_ = 0;
public:
    static constexpr std::uint64_t GlobalGapMs = 2500;
    static constexpr std::uint64_t AutomaticGapMs = 60000;
    bool Allows(const Settings& settings, std::uint64_t now, bool automatic) const noexcept {
        if (!IsValid(settings) || !settings.enabled || settings.volume == 0) return false;
        if (hasPlayed_ && (now < lastPlayed_ || now - lastPlayed_ < GlobalGapMs)) return false;
        if (automatic && hasAutomatic_ && (now < lastAutomatic_ || now - lastAutomatic_ < AutomaticGapMs)) return false;
        return true;
    }
    void MarkPlayed(std::uint64_t now, bool automatic) noexcept {
        lastPlayed_ = hasPlayed_ ? std::max(now, lastPlayed_) : now;
        hasPlayed_ = true;
        if (automatic) {
            lastAutomatic_ = hasAutomatic_ ? std::max(now, lastAutomatic_) : now;
            hasAutomatic_ = true;
        }
    }
};

struct Clip {
    unsigned sampleRate = 0, channels = 0;
    std::vector<std::int16_t> samples;
};
inline constexpr std::size_t MaximumWaveBytes = 1024 * 1024;
inline constexpr unsigned MaximumClipSeconds = 4;

// Strict, bounded PCM16 RIFF/WAVE reader. Ancillary chunks may precede or follow
// the audio, but repeated fmt/data chunks, truncated padding and wrong rates fail.
inline bool ParseWave(const std::uint8_t* data, std::size_t size, Clip& destination, std::string& error) {
    const auto fail = [&](const char* why) { error = why; return false; };
    if (!data || size < 44 || size > MaximumWaveBytes) return fail("Invalid WAV size");
    const auto u16 = [&](std::size_t at) { return static_cast<std::uint16_t>(data[at] | (static_cast<unsigned>(data[at + 1]) << 8)); };
    const auto u32 = [&](std::size_t at) { return static_cast<std::uint32_t>(data[at]) |
        (static_cast<std::uint32_t>(data[at + 1]) << 8) | (static_cast<std::uint32_t>(data[at + 2]) << 16) |
        (static_cast<std::uint32_t>(data[at + 3]) << 24); };
    if (std::memcmp(data, "RIFF", 4) || std::memcmp(data + 8, "WAVE", 4) || u32(4) != size - 8)
        return fail("Invalid RIFF/WAVE header");
    bool hasFormat = false, hasData = false;
    unsigned channels = 0, rate = 0, alignment = 0;
    std::size_t audioAt = 0, audioBytes = 0, at = 12;
    while (at < size) {
        if (size - at < 8) return fail("Truncated WAV chunk header");
        const auto length = static_cast<std::size_t>(u32(at + 4));
        const auto chunk = at + 8;
        if (length > size - chunk) return fail("WAV chunk exceeds file");
        if (!std::memcmp(data + at, "fmt ", 4)) {
            if (hasFormat || (length != 16 && length != 18)) return fail("Invalid WAV format chunk");
            hasFormat = true;
            channels = u16(chunk + 2); rate = u32(chunk + 4); alignment = u16(chunk + 12);
            if (u16(chunk) != 1 || (channels != 1 && channels != 2) || rate < 8000 || rate > 48000 ||
                u16(chunk + 14) != 16 || alignment != channels * 2 || u32(chunk + 8) != rate * alignment ||
                (length == 18 && u16(chunk + 16) != 0)) return fail("WAV must be bounded PCM16 audio");
        } else if (!std::memcmp(data + at, "data", 4)) {
            if (hasData) return fail("Duplicate WAV data chunk");
            hasData = true; audioAt = chunk; audioBytes = length;
        }
        at = chunk + length;
        if (length & 1U) {
            if (at == size) return fail("Missing WAV chunk padding");
            ++at;
        }
    }
    if (!hasFormat || !hasData || audioBytes == 0 || audioBytes % alignment != 0 ||
        audioBytes / alignment > static_cast<std::size_t>(rate) * MaximumClipSeconds)
        return fail("Missing, misaligned or too long WAV audio");
    Clip parsed;
    parsed.sampleRate = rate; parsed.channels = channels;
    parsed.samples.resize(audioBytes / 2);
    for (std::size_t i = 0; i < parsed.samples.size(); ++i) {
        const auto word = u16(audioAt + i * 2);
        const auto signedValue = word >= 0x8000 ? static_cast<std::int32_t>(word) - 0x10000 : static_cast<std::int32_t>(word);
        parsed.samples[i] = static_cast<std::int16_t>(signedValue);
    }
    destination = std::move(parsed);
    error.clear();
    return true;
}

inline std::vector<std::int16_t> ApplyGain(const Clip& clip, unsigned volume) {
    const auto gain = static_cast<std::int32_t>(std::min(volume, 100U));
    std::vector<std::int16_t> output(clip.samples.size());
    for (std::size_t i = 0; i < output.size(); ++i)
        output[i] = static_cast<std::int16_t>(static_cast<std::int32_t>(clip.samples[i]) * gain / 100);
    return output;
}

} // namespace cat::audio
