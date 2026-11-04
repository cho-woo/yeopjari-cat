#include "../src/cat_audio_model.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)
using Bytes = std::vector<std::uint8_t>;
void U16(Bytes& out, unsigned value) { out.push_back(value & 255); out.push_back((value >> 8) & 255); }
void U32(Bytes& out, std::uint32_t value) { for (unsigned shift = 0; shift < 32; shift += 8) out.push_back((value >> shift) & 255); }
void Put32(Bytes& out, std::size_t at, std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) out.at(at + i) = (value >> (i * 8)) & 255; }
void Tag(Bytes& out, const char* text) { for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(text[i])); }
void Chunk(Bytes& out, const char* tag, const Bytes& bytes) {
    Tag(out, tag); U32(out, static_cast<std::uint32_t>(bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end()); if (bytes.size() & 1U) out.push_back(0);
}
Bytes Wave(const std::vector<std::int16_t>& samples = {-32768, -16000, -1, 0, 1, 16000, 32767, 0},
           unsigned channels = 1, unsigned rate = 24000, bool ancillary = false, bool dataFirst = false,
           bool extendedPcm = false) {
    Bytes out; Tag(out, "RIFF"); U32(out, 0); Tag(out, "WAVE");
    Bytes format; U16(format, 1); U16(format, channels); U32(format, rate);
    U32(format, rate * channels * 2); U16(format, channels * 2); U16(format, 16);
    if (extendedPcm) U16(format, 0);
    Bytes data; for (auto sample : samples) U16(data, static_cast<std::uint16_t>(sample));
    if (ancillary) Chunk(out, "JUNK", {1, 2, 3});
    if (dataFirst) { Chunk(out, "data", data); Chunk(out, "fmt ", format); }
    else { Chunk(out, "fmt ", format); Chunk(out, "data", data); }
    if (ancillary) Chunk(out, "LIST", {4, 5, 6, 7});
    Put32(out, 4, static_cast<std::uint32_t>(out.size() - 8));
    return out;
}
std::string Signed(const std::string& body, const std::string& ending = "\n") {
    std::ostringstream out;
    out << body << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << cat::Checksum(body) << ending;
    return out.str();
}
void RejectSettings(const std::string& text) {
    cat::audio::Settings value{false, 70}; std::string error;
    CHECK(!cat::audio::ParseSettings(text, value, error));
    CHECK(!error.empty()); CHECK(!value.enabled && value.volume == 70);
}
void RejectWave(const Bytes& bytes) {
    cat::audio::Clip value{8000, 1, {123, -123}}; std::string error;
    CHECK(!cat::audio::ParseWave(bytes.data(), bytes.size(), value, error)); CHECK(!error.empty());
    CHECK(value.sampleRate == 8000 && value.channels == 1 && value.samples == std::vector<std::int16_t>({123, -123}));
}

void PreferencesRoundTrip() {
    using namespace cat::audio;
    CHECK(Settings{}.enabled && Settings{}.volume == 25);
    for (bool enabled : {false, true}) for (unsigned volume = 0; volume <= 100; ++volume) {
        Settings original{enabled, volume}, restored; std::string error = "old error";
        const auto text = SerializeSettings(original);
        CHECK(IsValid(original)); CHECK(!text.empty());
        CHECK(ParseSettings(text, restored, error)); CHECK(error.empty());
        CHECK(restored.enabled == enabled && restored.volume == volume);
        CHECK(SerializeSettings(restored) == text);
    }
    CHECK(!IsValid(Settings{true, 101})); CHECK(SerializeSettings({true, 101}).empty());
    CHECK(IsValid(Cue::Hungry) && IsValid(Cue::Wake)); CHECK(!IsValid(Cue::Count));
    // The last new resource must be loadable; the count/sentinels must never
    // index the player's fixed clip array. Legacy resource offsets stay stable.
    CHECK(IsValid(Cue::FoodRequest) && IsValid(Cue::Greeting));
    CHECK(static_cast<unsigned>(Cue::Wake) == 7);
    CHECK(!IsValid(static_cast<Cue>(static_cast<unsigned>(Cue::Count) + 1)));
    CHECK(!IsValid(static_cast<Cue>(-1)));
}
void PreferenceCorruption() {
    const auto good = cat::audio::SerializeSettings({true, 45});
    for (std::size_t i = 0; i + 1 < good.size(); ++i) RejectSettings(good.substr(0, i));
    cat::audio::Settings value;
    CHECK(cat::audio::ParseSettings(good.substr(0, good.size() - 1), value));
    CHECK(value.enabled && value.volume == 45);
    for (const auto* body : {
        "schema=2\nenabled=1\nvolume=25\n", "schema=1\nenabled=2\nvolume=25\n",
        "schema=1\nenabled=true\nvolume=25\n", "schema=1\nenabled=1\nvolume=101\n",
        "schema=1\nenabled=1\nvolume=025\n", "schema=1\nenabled=1\nvolume=-1\n",
        "schema=1\nenabled=1\nvolume=25.0\n", "schema=1\nenabled=1\nvolume=25 \n",
        "schema=1\nenabled=1\nvolume=\n", "schema=1\nenabled=1\nvolume=25\nextra=1\n",
        "schema=1\nenabled=1\nvolume=25\nvolume=70\n", "schema=1\nvolume=25\nenabled=1\n",
        "schema=1\r\nenabled=1\nvolume=25\n"}) RejectSettings(Signed(body));
    RejectSettings(good + "\n"); RejectSettings(good + "extra"); RejectSettings(std::string(1025, 'x'));
    auto corrupt = good; corrupt[corrupt.find("45")] = '7'; RejectSettings(corrupt);
    const auto crlf = Signed("schema=1\r\nenabled=0\r\nvolume=70\r\n", "\r\n");
    CHECK(cat::audio::ParseSettings(crlf, value)); CHECK(!value.enabled && value.volume == 70);
    RejectSettings(Signed("schema=1\r\nenabled=0\r\nvolume=70\r\n", "\n"));
}
void QuietScheduling() {
    using namespace cat::audio;
    RateGate gate;
    CHECK(gate.Allows({}, 0, false)); CHECK(gate.Allows({}, 0, true));
    CHECK(!gate.Allows({false, 25}, 0, false)); CHECK(!gate.Allows({true, 0}, 0, false));
    CHECK(!gate.Allows({true, 101}, 0, false));
    gate.MarkPlayed(0, false);
    CHECK(!gate.Allows({}, 0, false)); CHECK(!gate.Allows({}, 2499, false));
    CHECK(gate.Allows({}, 2500, false)); CHECK(gate.Allows({}, 2500, true));
    gate.MarkPlayed(2500, true);
    CHECK(!gate.Allows({}, 4999, false)); CHECK(gate.Allows({}, 5000, false));
    CHECK(!gate.Allows({}, 62499, true)); CHECK(gate.Allows({}, 62500, true));
    // Merely checking/dropping a cue never spends an automatic or interaction slot.
    for (unsigned i = 0; i < 20; ++i) CHECK(gate.Allows({}, 5000, false));
    CHECK(!gate.Allows({}, 62499, true));
    gate.MarkPlayed(10000, false);
    CHECK(!gate.Allows({}, 11000, false)); CHECK(gate.Allows({}, 12500, false));
    CHECK(gate.Allows({}, 62500, true));
    // A mute/unmute cycle does not bypass the last sound's quiet interval.
    CHECK(!gate.Allows({false, 25}, 11000, false)); CHECK(!gate.Allows({true, 25}, 11000, false));
}
void LongUptimeAndRollback() {
    using namespace cat::audio;
    RateGate gate;
    const auto longUptime = (std::uint64_t{1} << 45) + 111;
    gate.MarkPlayed(longUptime, true);
    CHECK(!gate.Allows({}, longUptime - 1, false));
    CHECK(!gate.Allows({}, longUptime + 2499, false));
    CHECK(gate.Allows({}, longUptime + 2500, false));
    CHECK(!gate.Allows({}, longUptime + 59999, true));
    CHECK(gate.Allows({}, longUptime + 60000, true));
    gate.MarkPlayed(longUptime - 1000, true);
    CHECK(!gate.Allows({}, longUptime + 59999, true));
    CHECK(gate.Allows({}, longUptime + 60000, true));
    RateGate high;
    high.MarkPlayed(std::numeric_limits<std::uint64_t>::max() - 100, false);
    CHECK(!high.Allows({}, 1, false)); CHECK(!high.Allows({}, std::numeric_limits<std::uint64_t>::max(), false));
}
void ValidPcmLayouts() {
    using namespace cat::audio;
    for (unsigned rate : {8000U, 22050U, 24000U, 44100U, 48000U})
        for (unsigned channels : {1U, 2U})
            for (bool dataFirst : {false, true}) for (bool ancillary : {false, true}) {
                Clip clip; std::string error;
                auto bytes = Wave({-32768, -16000, -1, 0, 1, 16000, 32767, 0}, channels, rate, ancillary, dataFirst, true);
                CHECK(ParseWave(bytes.data(), bytes.size(), clip, error)); CHECK(error.empty());
                CHECK(clip.sampleRate == rate && clip.channels == channels);
                CHECK(clip.samples == std::vector<std::int16_t>({-32768, -16000, -1, 0, 1, 16000, 32767, 0}));
            }
    Clip clip; std::string error;
    auto bytes = Wave(); CHECK(ParseWave(bytes.data(), bytes.size(), clip, error));
    std::vector<std::int16_t> fourSeconds(4 * 48000 * 2, 1);
    bytes = Wave(fourSeconds, 2, 48000); CHECK(ParseWave(bytes.data(), bytes.size(), clip, error));
    CHECK(clip.samples.size() == fourSeconds.size());
}
void MalformedPcmBounds() {
    using namespace cat::audio;
    const auto good = Wave();
    for (std::size_t cut = 0; cut < good.size(); ++cut) RejectWave(Bytes(good.begin(), good.begin() + cut));
    auto changed = good; changed[0] = 'X'; RejectWave(changed);
    changed = good; changed[8] = 'X'; RejectWave(changed);
    changed = good; changed.push_back(0); RejectWave(changed);
    changed = good; Put32(changed, 16, 0xffffffff); RejectWave(changed);
    changed = good; Put32(changed, 40, 0xffffffff); RejectWave(changed);
    changed = good; changed[20] = 3; RejectWave(changed); // IEEE float.
    changed = good; changed[22] = 3; RejectWave(changed); // Unsupported channel count.
    changed = good; changed[34] = 8; RejectWave(changed); // PCM8 unsupported.
    changed = good; changed[32] = 4; RejectWave(changed); // Wrong frame alignment.
    changed = good; Put32(changed, 28, 24000); RejectWave(changed); // Wrong byte rate.
    changed = Wave({}, 1, 24000); RejectWave(changed);
    changed = Wave({1, 2}, 1, 7999); RejectWave(changed);
    changed = Wave({1, 2}, 1, 48001); RejectWave(changed);
    changed = good; Chunk(changed, "data", {0, 0}); Put32(changed, 4, changed.size() - 8); RejectWave(changed);
    changed = good; changed.insert(changed.end(), good.begin() + 12, good.begin() + 36); Put32(changed, 4, changed.size() - 8); RejectWave(changed);
    changed = good; Tag(changed, "JUNK"); U32(changed, 1); changed.push_back(1); Put32(changed, 4, changed.size() - 8); RejectWave(changed);
    changed = good; changed.push_back(0); Put32(changed, 4, changed.size() - 8); RejectWave(changed);
    RejectWave(Wave(std::vector<std::int16_t>(4 * 8000 + 1, 1), 1, 8000));
    RejectWave(Bytes(MaximumWaveBytes + 1, 0));
    Clip clip; std::string error; CHECK(!ParseWave(nullptr, good.size(), clip, error));
}
void PerClipGain() {
    using namespace cat::audio;
    Clip clip{24000, 1, {-32768, -16000, -1, 0, 1, 16000, 32767}};
    const auto source = clip.samples;
    CHECK(ApplyGain(clip, 100) == source); CHECK(ApplyGain(clip, 1000) == source);
    CHECK(ApplyGain(clip, 0) == std::vector<std::int16_t>(source.size(), 0));
    CHECK(ApplyGain(clip, 25) == std::vector<std::int16_t>({-8192, -4000, 0, 0, 0, 4000, 8191}));
    for (unsigned volume : {25U, 45U, 70U}) {
        const auto out = ApplyGain(clip, volume);
        CHECK(out.size() == source.size());
        for (std::size_t i = 0; i < source.size(); ++i) CHECK(std::abs(static_cast<int>(out[i])) <= std::abs(static_cast<int>(source[i])));
        CHECK(clip.samples == source);
    }
}
void PetRecordsRemainIndependent() {
    const auto born = cat::ToUnix({2026, 11, 3, 3, 0, 0});
    auto pet = cat::NewPet(born); cat::Feed(pet, born + 86400, false);
    const auto saved = cat::Serialize(pet);
    for (const auto settings : {cat::audio::Settings{true, 25}, {true, 70}, {false, 45}}) {
        cat::audio::Settings restored;
        CHECK(cat::audio::ParseSettings(cat::audio::SerializeSettings(settings), restored));
        CHECK(cat::Serialize(pet) == saved);
    }
}
} // namespace

int main() {
    try {
        PreferencesRoundTrip(); PreferenceCorruption(); QuietScheduling(); LongUptimeAndRollback();
        ValidPcmLayouts(); MalformedPcmBounds(); PerClipGain(); PetRecordsRemainIndependent();
        std::cout << "cat audio: " << checks << " checks passed (8 scenario groups)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
