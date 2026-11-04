#pragma once

#include "cat_audio_model.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <memory>
#include <new>

namespace cat::audio {

// Single UI-thread owner. Poll from the existing UI timer; no callbacks, audio
// threads, queues, filesystem reads, network access, or system-volume changes.
class Player {
    struct Voice {
        HWAVEOUT device = nullptr;
        WAVEHDR header{};
        std::vector<std::int16_t> samples;
        bool prepared = false, submitted = false;
    };
    std::array<Clip, CueCount> clips_;
    Settings settings_;
    RateGate gate_;
    Voice* voice_ = nullptr;
    bool shutdown_ = false, deviceFailed_ = false;
    std::uint64_t failedAt_ = 0;

    bool Retire() noexcept {
        if (!voice_) return true;
        if (voice_->submitted && !(voice_->header.dwFlags & WHDR_DONE)) return false;
        if (voice_->prepared) {
            if (waveOutUnprepareHeader(voice_->device, &voice_->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)
                return false; // The driver may still own both header and sample memory.
            voice_->prepared = false;
        }
        if (voice_->device) {
            if (waveOutClose(voice_->device) != MMSYSERR_NOERROR) return false;
            voice_->device = nullptr;
        }
        delete voice_; voice_ = nullptr;
        return true;
    }
    void DeviceFailure(std::uint64_t now) noexcept {
        deviceFailed_ = true; failedAt_ = now;
        Stop();
    }
public:
    Player() = default;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;
    ~Player() {
        Shutdown();
        // If a broken device refuses reset/unprepare, retain its one outstanding
        // allocation until process teardown. Freeing driver-owned memory would
        // be unsafe. There is never a second outstanding allocation or retry queue.
    }

    const Settings& GetSettings() const noexcept { return settings_; }
    void SetSettings(const Settings& settings) noexcept {
        const Settings safe = IsValid(settings) ? settings : Settings{false, 25};
        if (safe.enabled == settings_.enabled && safe.volume == settings_.volume) return;
        Stop();
        settings_ = safe;
    }
    bool HasClip(Cue cue) const noexcept {
        return IsValid(cue) && !clips_[static_cast<std::size_t>(cue)].samples.empty();
    }
    bool LoadWave(Cue cue, const void* bytes, std::size_t size) noexcept {
        if (shutdown_ || !IsValid(cue)) return false;
        try {
            Clip parsed;
            std::string error;
            if (!ParseWave(static_cast<const std::uint8_t*>(bytes), size, parsed, error)) return false;
            clips_[static_cast<std::size_t>(cue)] = std::move(parsed);
            return true;
        } catch (...) { return false; }
    }
    bool LoadResource(Cue cue, HINSTANCE instance, int resourceId) noexcept {
        if (shutdown_ || !IsValid(cue) || resourceId <= 0 || resourceId > 65535) return false;
        const HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));
        if (!resource) return false;
        const DWORD size = SizeofResource(instance, resource);
        if (!size || size > MaximumWaveBytes) return false;
        const HGLOBAL loaded = ::LoadResource(instance, resource);
        return loaded && LoadWave(cue, LockResource(loaded), size);
    }
    void Poll() noexcept { Retire(); }
    void Stop() noexcept {
        if (!voice_) return;
        if (voice_->device && waveOutReset(voice_->device) == MMSYSERR_NOERROR)
            voice_->submitted = false; // Reset returns every pending block to us.
        Retire();
    }
    void Shutdown() noexcept { shutdown_ = true; Stop(); }
    bool IsPlaying() const noexcept {
        return voice_ && voice_->submitted && !(voice_->header.dwFlags & WHDR_DONE);
    }
    bool Play(Cue cue, std::uint64_t ticks, bool automatic = false) noexcept {
        if (shutdown_ || !HasClip(cue) || !gate_.Allows(settings_, ticks, automatic)) return false;
        Poll();
        if (voice_) return false; // Do not interrupt, overlap, or queue stale sounds.
        if (deviceFailed_ && (ticks < failedAt_ || ticks - failedAt_ < 30000)) return false;
        try {
            const Clip& clip = clips_[static_cast<std::size_t>(cue)];
            auto candidate = std::make_unique<Voice>();
            candidate->samples = ApplyGain(clip, settings_.volume);
            candidate->header.lpData = reinterpret_cast<LPSTR>(candidate->samples.data());
            candidate->header.dwBufferLength = static_cast<DWORD>(candidate->samples.size() * sizeof(std::int16_t));
            WAVEFORMATEX format{};
            format.wFormatTag = WAVE_FORMAT_PCM;
            format.nChannels = static_cast<WORD>(clip.channels);
            format.nSamplesPerSec = clip.sampleRate;
            format.wBitsPerSample = 16;
            format.nBlockAlign = static_cast<WORD>(clip.channels * 2);
            format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
            voice_ = candidate.release();
            if (waveOutOpen(&voice_->device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
                // Failed opens do not return a usable handle. Do not retain an
                // undefined output value and prevent recovery after device reconnect.
                voice_->device = nullptr;
                DeviceFailure(ticks); return false;
            }
            if (waveOutPrepareHeader(voice_->device, &voice_->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                DeviceFailure(ticks); return false;
            }
            voice_->prepared = true;
            if (waveOutWrite(voice_->device, &voice_->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                DeviceFailure(ticks); return false;
            }
            voice_->submitted = true;
            deviceFailed_ = false;
            gate_.MarkPlayed(ticks, automatic);
            return true;
        } catch (...) {
            DeviceFailure(ticks);
            return false;
        }
    }
};

} // namespace cat::audio
