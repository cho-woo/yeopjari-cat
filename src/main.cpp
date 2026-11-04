#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <utility>
#include "app_version.h"
#include "pet_state.hpp"
#include "cat_nutrition.hpp"
#include "cat_behavior.hpp"
#include "cat_memory.hpp"
#include "cat_memory_motion.hpp"
#include "cat_memory_storage.hpp"
#include "cat_routine.hpp"
#include "cat_animation.hpp"
#include "cat_motion_gate.hpp"
#include "cat_physics.hpp"
#include "cat_toy.hpp"
#include "cat_render_geometry.hpp"
#include "cat_render_placement.hpp"
#include "cat_belly_rig.hpp"
#include "cat_preferences.hpp"
#include "cat_audio.hpp"
#include "screen_prank.hpp"
#include "update_client.hpp"
#include "update_swap.hpp"
#include "cat_update_notice.hpp"

using namespace Gdiplus;
constexpr wchar_t kClass[] = L"YeopjariCat.Native.v1";
constexpr wchar_t kTitle[] = L"옆자리 고양이";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowExisting = WM_APP + 2;
constexpr UINT_PTR kTickTimer = 1;
constexpr int kFeedHotkey = 1, kSnackHotkey = 2, kHideHotkey = 3;
enum Command { FeedCommand = 1010, SnackCommand, StatusCommand, PauseCommand, HideCommand, BackupCommand, FolderCommand, QuitCommand, PunchCommand,
               JumpCommand, CreamCommand, SilverCommand, EarCommand, TailCommand, UpdateCheckCommand, UpdateViewCommand, UpdateAutomaticCommand,
               SoundToggleCommand, SoundQuietCommand, SoundNormalCommand, SoundLouderCommand, SoundPreviewCommand,
               OutfitNoneCommand, OutfitBowCommand, OutfitScarfCommand, OutfitCrownCommand,
               ToyBallCommand, ToyBoxCommand, ToyClearCommand, LoafCommand, KneadCommand, BellyCommand };
using Motion = cat::animation::Motion;
constexpr cat::update::Version kVersion{YEOPJARI_VERSION_MAJOR, YEOPJARI_VERSION_MINOR, YEOPJARI_VERSION_PATCH};

struct UpdateState {
    enum class Operation { None, Checking, Downloading } operation = Operation::None;
    struct Result {
        bool success = false;
        cat::update::Manifest manifest;
        std::wstring stagedExe, error;
    } result;
    std::thread worker;
    std::atomic_bool cancel{false}, finished{false};
    std::wstring executable, feedError;
    std::string feed;
    cat::update::Announcement announcement;
    cat::update::Manifest available;
    bool hasUpdate = false, automatic = true, manual = false, applying = false;
    ULONGLONG nextCheck = 0;
    void Stop() {
        cancel.store(true);
        if (worker.joinable()) worker.join();
        if (!result.stagedExe.empty()) {
            cat::update::CleanupStagedDownload(result.stagedExe);
            result.stagedExe.clear();
        }
    }
    ~UpdateState() { Stop(); }
} updates;

struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr, old = nullptr;
    BYTE* pixels = nullptr;
    int width = 0, height = 0;
    std::unique_ptr<Bitmap> image;
    void Reset() {
        image.reset();
        if (dc && old) SelectObject(dc, old);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        dc = nullptr; bitmap = old = nullptr; pixels = nullptr; width = height = 0;
    }
    bool Create(int w, int h) {
        Reset();
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr); dc = CreateCompatibleDC(screen);
        bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (!dc || !bitmap || !pixels) { Reset(); return false; }
        old = static_cast<HBITMAP>(SelectObject(dc, bitmap)); width = w; height = h;
        image = std::make_unique<Bitmap>(w, h, w * 4, PixelFormat32bppPARGB, pixels);
        if (image->GetLastStatus() != Ok) { Reset(); return false; }
        return true;
    }
    ~Surface() { Reset(); }
};

struct SpriteAtlas {
    IStream* stream = nullptr;
    std::unique_ptr<Bitmap> image;
    std::array<Rect, 8> frames{};
    int standingHeight = 1;
    struct Skin {
        int width = 0, height = 0;
        std::vector<std::uint32_t> source, deformed, body, head;
        std::unique_ptr<Bitmap> image, bodyImage, headImage;
        std::array<int, 8> controls{};
        bool valid = false, partsValid = false;
    };
    std::array<Skin, 8> skins;
    void Reset() {
        for (auto& skin : skins) {
            skin.image.reset(); skin.bodyImage.reset(); skin.headImage.reset();
            skin.source.clear(); skin.deformed.clear(); skin.body.clear(); skin.head.clear(); skin.valid = skin.partsValid = false;
        }
        image.reset();
        if (stream) { stream->Release(); stream = nullptr; }
        standingHeight = 1;
    }
};
struct CharacterArt { SpriteAtlas base, actions; };

struct WindowPerch {
    HWND hwnd = nullptr;
    RECT rect{};
    bool active = false;
    bool transitioning = false;
    bool landingOnWindow = false;
    bool preparing = false;
    ULONGLONG nextTry = 0, leaveAfter = 0;
    double targetX = 0.0;
};

using ToyKind = cat::toy::Kind;

struct App {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HANDLE instanceMutex = nullptr;
    ULONG_PTR gdiplusToken = 0;
    std::array<CharacterArt, 2> characters;
    SpriteAtlas jumpTreat;
    SpriteAtlas gestures;
    SpriteAtlas recline;
    std::array<int, 2> extraStandingHeight{1, 1};
    std::array<int, 2> gestureStandingHeight{1, 1};
    cat::Preferences preferences;
    bool preferencesRecovered = false, preferencesInvalid = false;
    ToyKind toy = ToyKind::None;
    double toyX = 0.0;
    bool approachingToy = false, toyContact = false;
    int pendingCare = -1;
    cat::toy::Ball toyBall;
    HWND toyWindow = nullptr;
    Surface toySurface;
    ULONGLONG nextToyAction = 0;
    cat::audio::Settings soundSettings;
    cat::audio::Player audio;
    bool soundSettingsRecovered = false, soundSettingsInvalid = false;
    bool motionSoundPending = false, motionSoundAutomatic = true, punchSoundAutomatic = true;
    bool gestureSoundPending = false, gestureSoundAutomatic = true;
    cat::audio::Cue punchCue = cat::audio::Cue::Punch;
    ULONGLONG ambientSoundAfter = 0;
    ULONGLONG nextSoundAfter = 0, nextAutomaticSoundAfter = 0;
    HICON trayIcon = nullptr;
    NOTIFYICONDATAW tray{};
    UINT taskbarCreated = 0;
    Surface surface;
    Surface blendFrom, blendTo;
    cat::ScreenPrank prank;
    WindowPerch perch;
    cat::PetState pet;
    cat::memory::storage::Store memory;
    bool memorySaveErrorShown = false;
    ULONGLONG nextMemoryRoutineAttempt = 0;
    std::wstring memoryNotice;
    bool approachingUser = false;
    double userTargetX = 0;
    std::array<Motion, 3> memorySequence{};
    unsigned memorySequenceCount = 0;
    cat::nutrition::State nutrition;
    cat::physics::Controller physics;
    cat::animation::Locomotion gait;
    bool physicsReady = false, jumpPending = false, jumpLaunched = false;
    bool waitingForStop = false, stopSoundPending = false, stopSoundAutomatic = false;
    cat::audio::Cue stopSoundCue = cat::audio::Cue::Pet;
    bool preciseTimer = false;
    UINT timerInterval = 0;
    double lastPhysicsTime = 0, landingStrength = 0;
    ULONGLONG landedAt = 0;
    Motion airborneMotion = Motion::Jump;
    Motion walkingMotion = Motion::Walk;
    std::unique_ptr<cat::behavior::Brain> brain;
    std::wstring directory, savePath, backupPath;
    std::wstring nutritionPath, nutritionBackupPath;
    std::wstring nutritionNotice;
    bool nutritionReady = false, nutritionSaveErrorShown = false;
    bool nutritionMainPreserved = false, nutritionBackupPreserved = false;
    bool saveErrorShown = false, restoredBackup = false;
    bool hidden = false, menuOpen = false, dragging = false, moved = false;
    bool initialized = false, closing = false;
    HMONITOR monitor = nullptr;
    RECT work{};
    double dpiScale = 1.0, centerX = 0.0, dragOffset = 0.0;
    POINT dragStart{};
    Motion motion = Motion::Idle;
    int direction = 1, facing = 1;
    double speed = 24.0;
    int punchHits = 0;
    ULONGLONG lastTick = 0, nextDecision = 0, nextMaintenance = 0, nextSave = 0, motionStart = 0, bubbleUntil = 0, nextCuriosity = 0;
    ULONGLONG foodRequestAfter = 0, nextCareReply = 0;
    std::wstring bubble;
    std::vector<std::wstring> hotkeyFailures;
    std::mt19937 random{std::random_device{}()};
} g;

// A monotonic, high resolution clock drives a fixed-step physics simulation.
// It is independent of the UTC clock that owns the cat's age and care history.
double MotionClock() {
    static const double frequency = [] {
        LARGE_INTEGER value{}; QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();
    LARGE_INTEGER value{}; QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart) / frequency;
}
void ResetMovement();
void RefreshAnimationTimer();
LONG CurrentGroundY(ULONGLONG ticks);
void CancelPerch();

std::size_t CharacterIndex() {
    return g.preferences.character == cat::Character::SilverTabby ? 1U : 0U;
}
CharacterArt& CurrentArt() { return g.characters[CharacterIndex()]; }

std::int64_t UtcNow() {
    FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{}; value.LowPart = ft.dwLowDateTime; value.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>((value.QuadPart - 116444736000000000ULL) / 10000000ULL);
}
std::wstring Integer(std::uint64_t n) { return std::to_wstring(n); }
bool Exists(const std::wstring& file) { return GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool ReadBytes(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 1 || size.QuadPart > 65536) { CloseHandle(file); return false; }
    out.resize(static_cast<size_t>(size.QuadPart)); DWORD count = 0;
    bool ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &count, nullptr) && count == out.size();
    CloseHandle(file); return ok;
}
bool WriteBytes(const std::wstring& path, const std::string& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD count = 0;
    bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &count, nullptr) && count == text.size();
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file)) ok = false;
    return ok;
}
bool ValidSave(const std::wstring& path, cat::PetState& pet) {
    std::string data, error;
    return ReadBytes(path, data) && cat::Parse(data, pet, error);
}
std::wstring DateText(std::int64_t timestamp) {
    const auto c = cat::FromUnix(timestamp);
    SYSTEMTIME utc{}, local{};
    utc.wYear = static_cast<WORD>(c.year); utc.wMonth = static_cast<WORD>(c.month); utc.wDay = static_cast<WORD>(c.day);
    utc.wHour = static_cast<WORD>(c.hour); utc.wMinute = static_cast<WORD>(c.minute); utc.wSecond = static_cast<WORD>(c.second);
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) local = utc;
    wchar_t text[40]{}; swprintf(text, 40, L"%04u년 %u월 %u일", local.wYear, local.wMonth, local.wDay); return text;
}
void Balloon(const std::wstring& title, const std::wstring& text, bool warning = false) {
    if (!g.window || !g.tray.hWnd) return;
    NOTIFYICONDATAW note = g.tray;
    note.uFlags = NIF_INFO;
    wcsncpy(note.szInfoTitle, title.c_str(), ARRAYSIZE(note.szInfoTitle) - 1);
    wcsncpy(note.szInfo, text.c_str(), ARRAYSIZE(note.szInfo) - 1);
    note.dwInfoFlags = (warning ? NIIF_WARNING : NIIF_INFO) | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &note);
}
void SyncNutrition(std::int64_t now) {
    if (!g.nutritionReady) { cat::Advance(g.pet, now); return; }
    // Both records use one monotonic snapshot time, including after a clock rollback.
    const auto snapshot = std::max({now, g.pet.updatedUtc, g.nutrition.updatedUtc});
    cat::Advance(g.pet, snapshot);
    cat::nutrition::Advance(g.nutrition, snapshot);
    g.pet.hunger = g.nutrition.fullness;
}
bool ValidNutrition(const std::wstring& path, cat::nutrition::State& state) {
    std::string bytes, error;
    cat::nutrition::State candidate;
    if (!ReadBytes(path, bytes) || !cat::nutrition::Parse(bytes, candidate, error) ||
        candidate.bornUtc != g.pet.bornUtc) return false;
    state = candidate;
    return true;
}
bool PreserveNutrition(const std::wstring& path, const wchar_t* name, bool& preserved) {
    if (preserved || !Exists(path)) return true;
    static std::uint64_t sequence = 0;
    const auto destination = g.directory + L"\\" + name + L"-" + std::to_wstring(UtcNow()) +
        L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) +
        L"-" + std::to_wstring(++sequence) + L".ini";
    preserved = CopyFileW(path.c_str(), destination.c_str(), TRUE) != FALSE;
    return preserved;
}
void LoadNutrition() {
    g.nutritionPath = g.directory + L"\\nutrition.ini";
    g.nutritionBackupPath = g.directory + L"\\nutrition.bak";
    cat::nutrition::State main, backup;
    const bool mainValid = ValidNutrition(g.nutritionPath, main);
    const bool backupValid = ValidNutrition(g.nutritionBackupPath, backup);
    const bool badMain = Exists(g.nutritionPath) && !mainValid;
    const bool badBackup = Exists(g.nutritionBackupPath) && !backupValid;
    bool preserved = true;
    if (badMain) preserved = PreserveNutrition(g.nutritionPath, L"nutrition-damaged", g.nutritionMainPreserved) && preserved;
    if (badBackup) preserved = PreserveNutrition(g.nutritionBackupPath, L"nutrition-backup-damaged", g.nutritionBackupPreserved) && preserved;
    if (mainValid) g.nutrition = main;
    else if (backupValid) {
        g.nutrition = backup;
        g.nutritionNotice = L"몸 상태 기록을 이전 백업에서 복구했어요. 생일과 함께한 기록은 그대로예요.";
    } else {
        // Upgrade only from today's fullness. Old care counts and years away must
        // never be replayed as a diet or create an unexpectedly changed body.
        g.nutrition = cat::nutrition::NewState(g.pet.bornUtc, std::max(UtcNow(), g.pet.updatedUtc), g.pet.hunger);
        if (badMain || badBackup)
            g.nutritionNotice = L"몸 상태 기록을 읽지 못해 보통 체형에서 다시 이어가요. 생일과 밥·츄르 기록은 그대로예요.";
    }
    if (mainValid && badBackup) g.nutritionNotice = L"몸 상태의 보조 백업을 읽지 못했어요. 현재 몸 상태와 성장 기록은 안전하게 불러왔어요.";
    if (!g.nutritionNotice.empty() && (badMain || badBackup))
        g.nutritionNotice += preserved ? L" 원본은 기록 폴더에 따로 보관했어요." : L" 원본을 따로 보관하기 전에는 덮어쓰지 않아요. 기록 폴더의 권한과 공간을 확인해 주세요.";
    g.nutritionReady = true;
    SyncNutrition(UtcNow());
}
bool SaveNutrition(bool reportError) {
    if (!g.nutritionReady) return true;
    const auto temporary = g.nutritionPath + L".tmp";
    const auto backupTemporary = g.nutritionBackupPath + L".tmp";
    auto fail = [&]() {
        DeleteFileW(temporary.c_str()); DeleteFileW(backupTemporary.c_str());
        if (reportError && !g.nutritionSaveErrorShown) {
            Balloon(L"몸 상태를 저장하지 못했어요", L"고양이의 생일과 성장 기록은 따로 보관해요. 기록 폴더의 권한과 공간을 확인해 주세요.", true);
            g.nutritionSaveErrorShown = true;
        }
        return false;
    };
    const auto bytes = cat::nutrition::Serialize(g.nutrition);
    if (bytes.empty() || !WriteBytes(temporary, bytes)) return fail();
    cat::nutrition::State prior, priorBackup;
    if (ValidNutrition(g.nutritionPath, prior)) {
        if (Exists(g.nutritionBackupPath) && !ValidNutrition(g.nutritionBackupPath, priorBackup) &&
            !PreserveNutrition(g.nutritionBackupPath, L"nutrition-backup-damaged", g.nutritionBackupPreserved)) return fail();
        std::string previous;
        if (!ReadBytes(g.nutritionPath, previous) || !WriteBytes(backupTemporary, previous) ||
            !MoveFileExW(backupTemporary.c_str(), g.nutritionBackupPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return fail();
        g.nutritionBackupPreserved = false;
    } else if (!PreserveNutrition(g.nutritionPath, L"nutrition-damaged", g.nutritionMainPreserved)) return fail();
    if (!MoveFileExW(temporary.c_str(), g.nutritionPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return fail();
    g.nutritionMainPreserved = false;
    g.nutritionSaveErrorShown = false;
    return true;
}
// The new memory sidecar has its own verified, atomic checkpoint. Legacy
// birthday, nutrition, appearance and update formats remain independent.
struct MemoryIO {
    std::wstring Path(const std::string& name) const {
        return g.directory + L"\\" + std::wstring(name.begin(), name.end());
    }
    bool Exists(const std::string& name) const {
        const auto path = Path(name);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
        const auto error = GetLastError();
        return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
    }
    bool Read(const std::string& name, std::string& bytes) const { return ReadBytes(Path(name), bytes); }
    bool Write(const std::string& name, const std::string& bytes) const {
        const auto path = Path(name);
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
        if (ok) ok = FlushFileBuffers(file) != FALSE;
        if (!CloseHandle(file)) ok = false;
        if (!ok) DeleteFileW(path.c_str());
        return ok;
    }
    bool Replace(const std::string& from, const std::string& to) const {
        return MoveFileExW(Path(from).c_str(), Path(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    bool Remove(const std::string& name) const { return DeleteFileW(Path(name).c_str()) != FALSE; }
    bool Preserve(const std::string& name) const {
        static unsigned sequence = 0;
        const auto backup = Path(name) + L".preserved-" + std::to_wstring(UtcNow()) + L"-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
        return CopyFileW(Path(name).c_str(), backup.c_str(), TRUE) != FALSE;
    }
};
struct MemoryTransaction {
    HANDLE mutex = nullptr;
    bool held = false;
    MemoryTransaction() {
        std::wstring identity = g.directory;
        CharLowerBuffW(identity.data(), static_cast<DWORD>(identity.size()));
        const std::string bytes(reinterpret_cast<const char*>(identity.data()), identity.size() * sizeof(wchar_t));
        const auto name = L"Global\\YeopjariCat.Memory.v1." + std::to_wstring(cat::Checksum(bytes));
        mutex = CreateMutexW(nullptr, FALSE, name.c_str());
        if (mutex) {
            const auto result = WaitForSingleObject(mutex, 3000);
            held = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
    }
    ~MemoryTransaction() { if (held) ReleaseMutex(mutex); if (mutex) CloseHandle(mutex); }
};
void SyncExperience() {
    if (!g.brain) return;
    if (!g.memory.enabled()) { g.brain->SetExperience({}); return; }
    cat::memory::Advance(g.memory.state, UtcNow());
    const auto p = cat::memory::Summarize(g.memory.state);
    g.brain->SetExperience({p.bond, p.petPreference, p.treatPreference,
        p.sociabilityShift, p.energyShift, p.curiosityShift, p.mischiefShift});
}
void LoadMemory() {
    MemoryIO io;
    MemoryTransaction transaction;
    if (transaction.held) g.memory.Load(io, g.pet, UtcNow());
    else g.memory.error = "Memory record is in use or cannot be locked";
    using Outcome = cat::memory::storage::Outcome;
    if (g.memory.outcome == Outcome::Recovered)
        g.memoryNotice = L"친밀도와 기억을 백업에서 복구했어요. 생일과 기존 성장 기록은 그대로예요.";
    else if (g.memory.outcome == Outcome::Blocked)
        g.memoryNotice = L"친밀도 기록을 읽지 못해 원본을 보존했어요. 기존 고양이는 계속 지내지만 기억 학습은 잠시 멈춰요. 기록 폴더의 memory.ini와 memory.bak를 확인해 주세요.";
}
bool SaveMemory(bool reportError = true) {
    if (!g.memory.enabled()) return true; // Never replace unreadable memories with a new pet.
    cat::memory::Advance(g.memory.state, UtcNow());
    MemoryIO io;
    MemoryTransaction transaction;
    if (transaction.held && g.memory.Save(io)) { g.memorySaveErrorShown = false; return true; }
    if (reportError && !g.memorySaveErrorShown)
        Balloon(L"친밀도를 저장하지 못했어요", L"기존 기록을 덮어쓰지 않았어요. 기록 폴더의 권한과 공간을 확인해 주세요.", true);
    g.memorySaveErrorShown = true;
    return false;
}
void Remember(cat::memory::Event event) {
    if (!g.memory.enabled()) return;
    if (cat::memory::Record(g.memory.state, event, UtcNow())) {
        SyncExperience();
        SaveMemory();
    }
}

bool SavePet(bool reportError = true) {
    SyncNutrition(UtcNow());
    if (g.work.right > g.work.left) g.pet.position = std::clamp((g.centerX - g.work.left) / (g.work.right - g.work.left), 0.0, 1.0);
    const std::wstring temp = g.savePath + L".tmp";
    if (!WriteBytes(temp, cat::Serialize(g.pet))) {
        if (reportError && !g.saveErrorShown) Balloon(L"기록을 저장하지 못했어요", L"저장 폴더의 권한이나 디스크 공간을 확인해 주세요. 고양이 메뉴에서 기록을 따로 백업할 수 있어요.", true);
        if (reportError) g.saveErrorShown = true;
        return false;
    }
    // Rotate only a verified healthy record. A corrupt main file must never overwrite recovery data.
    cat::PetState prior;
    if (ValidSave(g.savePath, prior)) {
        const std::wstring backupTemp = g.backupPath + L".tmp";
        std::string bytes;
        if (!ReadBytes(g.savePath, bytes) || !WriteBytes(backupTemp, bytes) || !MoveFileExW(backupTemp.c_str(), g.backupPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temp.c_str());
            if (reportError && !g.saveErrorShown) Balloon(L"기록 백업을 확인해 주세요", L"이전 기록을 안전하게 보관하지 못해 이번 저장을 멈췄어요. 기록 폴더의 권한과 디스크 공간을 확인해 주세요.", true);
            if (reportError) g.saveErrorShown = true;
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), g.savePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (reportError && !g.saveErrorShown) Balloon(L"기록을 저장하지 못했어요", L"고양이 메뉴에서 기록을 백업해 주세요. 이전 저장 파일은 그대로 남아 있어요.", true);
        if (reportError) g.saveErrorShown = true;
        return false;
    }
    g.saveErrorShown = false;
    const auto date = cat::FromUnix(UtcNow());
    wchar_t month[40]{}; swprintf(month, 40, L"\\pet-%04d-%02u.ini", date.year, date.month);
    const std::wstring monthly = g.directory + month;
    if (!Exists(monthly)) CopyFileW(g.savePath.c_str(), monthly.c_str(), TRUE);
    // Nutrition has its own atomic checkpoint; its loss must never invalidate
    // the original schema-1 birthday and lifetime-care record.
    const bool nutritionSaved = SaveNutrition(reportError);
    const bool memorySaved = SaveMemory(reportError);
    return nutritionSaved && memorySaved;
}
std::wstring UpdateText(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count) return L"업데이트 내용을 읽지 못했어요.";
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wide.data(), count);
    return wide;
}
int UpdateMessage(const wchar_t* text, const wchar_t* title, UINT flags) {
    const bool wasMenuOpen = g.menuOpen;
    g.menuOpen = true;
    g.prank.Clear();
    g.audio.Stop();
    const int choice = MessageBoxW(g.window, text, title, flags);
    g.menuOpen = wasMenuOpen;
    return choice;
}
void InitializeUpdates() {
    updates.executable = cat::update::CurrentExePath();
    updates.feed = cat::update::ReadFeed(updates.executable, updates.feedError);
    const std::wstring settings = g.directory + L"\\update-settings.ini";
    if (Exists(settings)) {
        std::string text;
        // A damaged optional setting never opts the user back in to network checks.
        updates.automatic = ReadBytes(settings, text) && text == "schema=1\nautomatic=1\n";
    }
    updates.nextCheck = GetTickCount64() + 30000;
    std::string notice;
    if (ReadBytes(g.directory + L"\\update-notice.ini", notice)) updates.announcement.Restore(notice);
}
void ToggleAutomaticUpdates() {
    const bool enabled = !updates.automatic;
    const std::wstring path = g.directory + L"\\update-settings.ini", temporary = path + L".tmp";
    if (!WriteBytes(temporary, enabled ? "schema=1\nautomatic=1\n" : "schema=1\nautomatic=0\n") ||
        !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        UpdateMessage(L"업데이트 알림 설정을 저장하지 못했어요. 기록 폴더의 접근 권한과 디스크 공간을 확인해 주세요.", kTitle, MB_OK | MB_ICONWARNING);
        return;
    }
    updates.automatic = enabled;
    updates.nextCheck = GetTickCount64() + 30000;
    Balloon(L"업데이트 알림", enabled ? L"새 버전이 있으면 조용히 알려드릴게요. 적용은 직접 선택할 수 있어요." : L"자동 확인을 껐어요. 고양이 메뉴에서 직접 확인할 수 있어요.");
}
void StartUpdateCheck(bool manual) {
    if (updates.operation != UpdateState::Operation::None || g.closing || g.menuOpen || updates.applying) return;
    if (updates.feed.empty()) {
        if (manual) {
            const std::wstring message = updates.feedError.empty()
                ? L"이 배포본에는 업데이트 주소가 아직 연결되지 않았어요.\n\n배포자가 준비한 최신 배포본을 받아 주세요. 고양이는 계속 키울 수 있어요."
                : L"업데이트 주소를 읽지 못했어요.\n\n" + updates.feedError + L"\n\n배포자에게 최신 배포본을 받아 주세요.";
            UpdateMessage(message.c_str(), kTitle, MB_OK | MB_ICONINFORMATION);
        }
        return;
    }
    updates.result = {};
    updates.manual = manual;
    updates.cancel.store(false);
    updates.finished.store(false);
    updates.operation = UpdateState::Operation::Checking;
    updates.nextCheck = GetTickCount64() + 6ULL * 60 * 60 * 1000;
    const std::string feed = updates.feed;
    try {
        updates.worker = std::thread([feed] {
            try {
                updates.result.success = cat::update::Check(feed, updates.result.manifest, updates.result.error, updates.cancel);
            } catch (...) { updates.result.error = L"업데이트를 확인하는 중 오류가 생겼어요. 잠시 뒤 다시 시도해 주세요."; }
            updates.finished.store(true, std::memory_order_release);
        });
        if (manual) Balloon(L"새 버전을 확인하고 있어요", L"고양이를 키우는 동안 확인할게요. 잠시만 기다려 주세요.");
    } catch (...) {
        updates.operation = UpdateState::Operation::None;
        if (manual) UpdateMessage(L"업데이트 확인을 시작하지 못했어요. 잠시 뒤 다시 시도해 주세요.", kTitle, MB_OK | MB_ICONWARNING);
    }
}
void StartUpdateDownload() {
    if (!updates.hasUpdate || updates.operation != UpdateState::Operation::None || g.closing || g.menuOpen || updates.applying) return;
    updates.result = {};
    updates.cancel.store(false);
    updates.finished.store(false);
    updates.operation = UpdateState::Operation::Downloading;
    const auto manifest = updates.available;
    const std::wstring directory = g.directory;
    try {
        updates.worker = std::thread([manifest, directory] {
            try {
                updates.result.success = cat::update::Download(manifest, directory, updates.result.stagedExe, updates.result.error, updates.cancel);
            } catch (...) { updates.result.error = L"다운로드 중 오류가 생겼어요. 잠시 뒤 다시 시도해 주세요."; }
            updates.finished.store(true, std::memory_order_release);
        });
        Balloon(L"업데이트를 받고 있어요", L"파일을 확인한 다음 기록을 저장하고 고양이를 다시 실행할게요.");
    } catch (...) {
        updates.operation = UpdateState::Operation::None;
        UpdateMessage(L"다운로드를 시작하지 못했어요. 잠시 뒤 다시 시도해 주세요.", kTitle, MB_OK | MB_ICONWARNING);
    }
}
INT_PTR CALLBACK UpdateDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        const auto& manifest = *reinterpret_cast<const cat::update::Manifest*>(lParam);
        const auto heading = L"v" + UpdateText(manifest.versionText) + L" 새 버전이 도착했어요";
        const auto versions = std::wstring(L"현재 v") + YEOPJARI_VERSION_WSTRING + L"  →  v" + UpdateText(manifest.versionText);
        auto notes = UpdateText(manifest.notesUtf8);
        std::wstring editText;
        for (std::size_t i = 0; i < notes.size(); ++i) {
            if (notes[i] == L'\n' && (i == 0 || notes[i - 1] != L'\r')) editText += L'\r';
            editText += notes[i];
        }
        if (editText.empty()) editText = L"이 버전의 변경 설명이 등록되지 않았어요.";
        // Keep the native UI explicitly Unicode as well as compiling the RC as
        // UTF-8. Dynamic notes and static dialog labels must follow one path.
        SetWindowTextW(dialog, L"옆자리 고양이 업데이트");
        SetDlgItemTextW(dialog, 2013, L"다운로드를 확인한 뒤 고양이가 잠깐 재시작돼요. 생일과 돌봄 기록은 이어집니다.");
        SetDlgItemTextW(dialog, IDOK, L"업데이트");
        SetDlgItemTextW(dialog, IDCANCEL, L"나중에");
        SetDlgItemTextW(dialog, 2010, heading.c_str());
        SetDlgItemTextW(dialog, 2012, versions.c_str());
        SetDlgItemTextW(dialog, 2011, editText.c_str());
        RECT bounds{}; GetWindowRect(dialog, &bounds);
        MONITORINFO info{}; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(g.monitor, &info)) {
            const auto width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
            SetWindowPos(dialog, HWND_TOP, info.rcWork.left + std::max<LONG>(0, (info.rcWork.right - info.rcWork.left - width) / 2),
                info.rcWork.top + std::max<LONG>(0, (info.rcWork.bottom - info.rcWork.top - height) / 2), 0, 0, SWP_NOSIZE);
        }
        // Avoid selecting the entire change log when the dialog opens.
        SetFocus(GetDlgItem(dialog, IDCANCEL));
        return FALSE;
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
        EndDialog(dialog, LOWORD(wParam)); return TRUE;
    }
    if (message == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    return FALSE;
}
void ShowUpdateDetails() {
    if (!updates.hasUpdate || updates.operation != UpdateState::Operation::None || g.closing || g.menuOpen || updates.applying) return;
    const bool wasMenuOpen = g.menuOpen;
    g.menuOpen = true; g.prank.Clear(); g.audio.Stop();
    const auto choice = DialogBoxParamW(g.instance, MAKEINTRESOURCEW(201), g.window, UpdateDialogProc, reinterpret_cast<LPARAM>(&updates.available));
    g.menuOpen = wasMenuOpen;
    if (choice == IDOK) StartUpdateDownload();
    else if (choice == -1) UpdateMessage(L"업데이트 안내를 열지 못했어요. 잠시 뒤 다시 시도해 주세요.", kTitle, MB_OK | MB_ICONWARNING);
}
void PollUpdates() {
    if (g.closing || g.menuOpen || g.dragging || updates.applying) return;
    if (updates.operation != UpdateState::Operation::None && updates.finished.load(std::memory_order_acquire)) {
        if (updates.worker.joinable()) updates.worker.join();
        const auto completed = updates.operation;
        updates.operation = UpdateState::Operation::None;
        auto result = std::move(updates.result);
        updates.result = {};
        if (!result.success) {
            if (!result.stagedExe.empty()) cat::update::CleanupStagedDownload(result.stagedExe);
            if (updates.manual || completed == UpdateState::Operation::Downloading) {
                const auto error = result.error.empty() ? L"인터넷 연결을 확인한 뒤 다시 시도해 주세요." : result.error;
                UpdateMessage(error.c_str(), L"업데이트를 완료하지 못했어요", MB_OK | MB_ICONWARNING);
            }
        } else if (completed == UpdateState::Operation::Checking) {
            updates.hasUpdate = cat::update::Compare(result.manifest.version, kVersion) > 0;
            updates.announcement.SetAvailable(kVersion, result.manifest.versionText);
            if (updates.hasUpdate) {
                updates.available = result.manifest;
                if (updates.manual) ShowUpdateDetails();
            } else if (updates.manual) {
                const auto message = std::wstring(L"현재 v") + YEOPJARI_VERSION_WSTRING + L"을 사용하고 있어요.\n\n지금 받을 새 업데이트가 없어요.";
                UpdateMessage(message.c_str(), kTitle, MB_OK | MB_ICONINFORMATION);
            }
        } else {
            std::wstring error;
            if (!SavePet()) {
                UpdateMessage(L"성장 기록을 먼저 저장해야 해서 업데이트를 멈췄어요. 기록 폴더의 권한과 디스크 공간을 확인한 뒤 다시 시도해 주세요.", kTitle, MB_OK | MB_ICONWARNING);
                cat::update::CleanupStagedDownload(result.stagedExe);
            } else if (!cat::update::LaunchUpdateHelper(updates.executable, result.stagedExe, updates.available, error)) {
                UpdateMessage(error.c_str(), L"업데이트를 적용하지 못했어요", MB_OK | MB_ICONWARNING);
                cat::update::CleanupStagedDownload(result.stagedExe);
            } else {
                // The verified helper now owns the staged files and waits for our process to exit.
                updates.applying = true;
                SendMessageW(g.window, WM_CLOSE, 0, 0);
                return;
            }
        }
    }
    if (updates.operation == UpdateState::Operation::None && updates.automatic && !updates.feed.empty() && GetTickCount64() >= updates.nextCheck)
        StartUpdateCheck(false);
}

bool LoadPet() {
    wchar_t local[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, local))) {
        MessageBoxW(nullptr, L"윈도우 개인 저장 폴더를 찾을 수 없어요. 기록을 잃지 않도록 실행을 멈춥니다.", kTitle, MB_ICONERROR); return false;
    }
    g.directory = std::wstring(local) + L"\\YeopjariCat";
    if (!CreateDirectoryW(g.directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"고양이 기록 폴더를 만들 수 없어요. 폴더 접근 권한과 디스크 공간을 확인해 주세요.", kTitle, MB_ICONERROR); return false;
    }
    g.savePath = g.directory + L"\\pet.ini"; g.backupPath = g.directory + L"\\pet.bak";
    if (ValidSave(g.savePath, g.pet)) { cat::Advance(g.pet, UtcNow()); return true; }
    if (ValidSave(g.backupPath, g.pet)) {
        if (Exists(g.savePath)) CopyFileW(g.savePath.c_str(), (g.directory + L"\\pet-damaged-" + std::to_wstring(UtcNow()) + L".ini").c_str(), TRUE);
        g.restoredBackup = true; cat::Advance(g.pet, UtcNow()); return true;
    }
    // Monthly checkpoints are also part of the pet's identity. Their presence must not lead to a new birthday.
    std::vector<std::wstring> checkpoints;
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((g.directory + L"\\pet-????" L"-??.ini").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do { if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) checkpoints.push_back(g.directory + L"\\" + found.cFileName); } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    std::sort(checkpoints.rbegin(), checkpoints.rend());
    for (const auto& checkpoint : checkpoints) if (ValidSave(checkpoint, g.pet)) {
        const auto suffix = std::to_wstring(UtcNow());
        if (Exists(g.savePath)) CopyFileW(g.savePath.c_str(), (g.directory + L"\\pet-damaged-" + suffix + L".ini").c_str(), TRUE);
        if (Exists(g.backupPath)) CopyFileW(g.backupPath.c_str(), (g.directory + L"\\backup-damaged-" + suffix + L".ini").c_str(), TRUE);
        g.restoredBackup = true; cat::Advance(g.pet, UtcNow()); return true;
    }
    if (Exists(g.savePath) || Exists(g.backupPath) || !checkpoints.empty()) {
        const std::wstring suffix = std::to_wstring(UtcNow());
        if (Exists(g.savePath)) CopyFileW(g.savePath.c_str(), (g.directory + L"\\pet-unreadable-" + suffix + L".ini").c_str(), TRUE);
        if (Exists(g.backupPath)) CopyFileW(g.backupPath.c_str(), (g.directory + L"\\backup-unreadable-" + suffix + L".ini").c_str(), TRUE);
        std::wstring message = L"기존 고양이 기록과 백업을 읽을 수 없어요. 생일을 바꾸거나 새 고양이로 덮어쓰지 않고 실행을 멈췄어요.\n\n월별 백업 또는 따로 보관한 기록을 확인해 주세요. 기존 파일은 보존했어요.\n\n기록 폴더:\n" + g.directory;
        MessageBoxW(nullptr, message.c_str(), kTitle, MB_OK | MB_ICONERROR); return false;
    }
    const auto now = UtcNow();
    if (now < 946684800LL || now > 32503680000LL) {
        MessageBoxW(nullptr, L"컴퓨터의 날짜를 먼저 확인해 주세요. 정확한 생일을 기록한 다음 함께 살기 시작할게요.", kTitle, MB_ICONWARNING); return false;
    }
    g.pet = cat::NewPet(now); return true;
}

bool ReadPreferences(const std::wstring& path, cat::Preferences& preferences) {
    std::string bytes;
    return ReadBytes(path, bytes) && cat::ParsePreferences(bytes, preferences);
}
void LoadPreferences() {
    const std::wstring path = g.directory + L"\\preferences.ini";
    const std::wstring backup = g.directory + L"\\preferences.bak";
    if (ReadPreferences(path, g.preferences)) return;
    if (ReadPreferences(backup, g.preferences)) { g.preferencesRecovered = true; return; }
    g.preferencesInvalid = Exists(path) || Exists(backup);
    g.preferences = {};
}
bool SavePreferences(const cat::Preferences& preferences) {
    const std::wstring path = g.directory + L"\\preferences.ini";
    const std::wstring backup = g.directory + L"\\preferences.bak";
    const std::wstring temporary = path + L".tmp";
    const auto bytes = cat::SerializePreferences(preferences);
    auto fail = [&]() {
        DeleteFileW(temporary.c_str());
        Balloon(L"꾸미기 설정을 저장하지 못했어요", L"저장 폴더의 권한과 공간을 확인해 주세요. 현재 고양이와 성장 기록은 그대로 유지해요.", true);
        return false;
    };
    if (bytes.empty() || !WriteBytes(temporary, bytes)) return fail();
    cat::Preferences prior;
    if (ReadPreferences(path, prior)) {
        std::string previous;
        const auto backupTemporary = backup + L".tmp";
        if (!ReadBytes(path, previous) || !WriteBytes(backupTemporary, previous) ||
            !MoveFileExW(backupTemporary.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return fail();
    } else if (Exists(path)) {
        const auto damaged = g.directory + L"\\preferences-damaged-" + std::to_wstring(UtcNow()) + L".ini";
        if (!CopyFileW(path.c_str(), damaged.c_str(), TRUE)) return fail();
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return fail();
    return true;
}

bool ReadSoundSettings(const std::wstring& path, cat::audio::Settings& settings) {
    std::string bytes, error;
    return ReadBytes(path, bytes) && cat::audio::ParseSettings(bytes, settings, error);
}
void LoadSoundSettings() {
    const auto path = g.directory + L"\\sound-settings.ini";
    const auto backup = g.directory + L"\\sound-settings.bak";
    if (!ReadSoundSettings(path, g.soundSettings)) {
        if (ReadSoundSettings(backup, g.soundSettings)) g.soundSettingsRecovered = true;
        else {
            g.soundSettings = {};
            g.soundSettingsInvalid = Exists(path) || Exists(backup);
            // An unreadable old preference must not unexpectedly unmute a pet.
            if (g.soundSettingsInvalid) g.soundSettings.enabled = false;
        }
    }
    g.audio.SetSettings(g.soundSettings);
    for (unsigned index = 0; index < static_cast<unsigned>(cat::audio::Cue::Count); ++index)
        g.audio.LoadResource(static_cast<cat::audio::Cue>(index), g.instance, 301 + static_cast<int>(index));
}
bool SaveSoundSettings(const cat::audio::Settings& settings, bool reportError = true) {
    const auto path = g.directory + L"\\sound-settings.ini";
    const auto backup = g.directory + L"\\sound-settings.bak";
    const auto temporary = path + L".tmp", backupTemporary = backup + L".tmp";
    const auto bytes = cat::audio::SerializeSettings(settings);
    auto fail = [&]() {
        DeleteFileW(temporary.c_str()); DeleteFileW(backupTemporary.c_str());
        if (reportError) Balloon(L"소리 설정을 저장하지 못했어요", L"현재 소리 설정을 유지해요. 기록 폴더의 권한과 공간을 확인해 주세요.", true);
        return false;
    };
    if (bytes.empty() || !WriteBytes(temporary, bytes)) return fail();
    cat::audio::Settings previous, previousBackup;
    const bool currentValid = ReadSoundSettings(path, previous);
    const bool backupValid = ReadSoundSettings(backup, previousBackup);
    const auto preserve = [&](const std::wstring& source, const wchar_t* label) {
        if (!Exists(source)) return true;
        static std::uint64_t sequence = 0;
        const auto damaged = g.directory + L"\\" + label + L"-" + std::to_wstring(UtcNow()) + L"-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) +
            L"-" + std::to_wstring(++sequence) + L".ini";
        return CopyFileW(source.c_str(), damaged.c_str(), TRUE) != FALSE;
    };
    if (!currentValid && !preserve(path, L"sound-settings-damaged")) return fail();
    if (!backupValid && !preserve(backup, L"sound-settings-backup-damaged")) return fail();
    if (currentValid || !backupValid) {
        std::string checkpoint = bytes;
        if (currentValid && !ReadBytes(path, checkpoint)) return fail();
        if (!WriteBytes(backupTemporary, checkpoint) ||
            !MoveFileExW(backupTemporary.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return fail();
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return fail();
    return true;
}
void PlayCatSound(cat::audio::Cue cue, bool automatic = false) {
    if (!g.initialized || g.hidden || g.menuOpen || g.dragging || g.closing || updates.applying) return;
    if (g.waitingForStop) {
        g.stopSoundPending = true; g.stopSoundCue = cue; g.stopSoundAutomatic = automatic;
        return;
    }
    const auto ticks = GetTickCount64();
    if (automatic && ticks < g.ambientSoundAfter) return;
    if (g.audio.Play(cue, ticks, automatic)) {
        g.nextSoundAfter = ticks + cat::audio::RateGate::GlobalGapMs;
        if (automatic) g.nextAutomaticSoundAfter = ticks + cat::audio::RateGate::AutomaticGapMs;
    }
}
void ChangeSoundSettings(bool toggle, unsigned volume = 25) {
    if (g.closing || updates.applying || g.menuOpen) return;
    auto settings = g.soundSettings;
    if (toggle) settings.enabled = !settings.enabled;
    else settings.volume = volume;
    if (!SaveSoundSettings(settings)) return;
    g.soundSettings = settings;
    g.soundSettingsRecovered = g.soundSettingsInvalid = false;
    g.audio.SetSettings(settings);
}

bool LoadAtlasResource(int resourceId, SpriteAtlas& sheet, int columns = 4, int frameCount = 8) {
    auto& stream = sheet.stream; auto& atlas = sheet.image;
    auto& frames = sheet.frames; auto& standingHeight = sheet.standingHeight;
    HRSRC resource = FindResourceW(g.instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(g.instance, resource);
    const DWORD size = SizeofResource(g.instance, resource);
    const void* bytes = LockResource(loaded);
    if (!bytes || !size) return false;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size); if (!memory) return false;
    void* target = GlobalLock(memory); if (!target) { GlobalFree(memory); return false; }
    std::memcpy(target, bytes, size); GlobalUnlock(memory);
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) { GlobalFree(memory); return false; }
    atlas.reset(Bitmap::FromStream(stream, FALSE));
    if (!atlas || atlas->GetLastStatus() != Ok || atlas->GetWidth() < 8 || atlas->GetHeight() < 4) return false;
    const int width = static_cast<int>(atlas->GetWidth()), height = static_cast<int>(atlas->GetHeight());
    Rect whole(0, 0, width, height); BitmapData data{};
    if (atlas->LockBits(&whole, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) return false;
    // Ignore almost-transparent generative-background speckles at runtime; the original PNG stays untouched.
    for (int y = 0; y < height; ++y) {
        BYTE* row = static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
        for (int x = 0; x < width; ++x) if (row[x * 4 + 3] <= 16) std::memset(row + x * 4, 0, 4);
    }
    const int rows = frameCount / columns;
    std::vector<int> rowCuts(rows + 1);
    for (int row = 0; row <= rows; ++row) rowCuts[row] = row * height / rows;
    // Portrait sheets have deliberately generous, sometimes uneven gutters.
    // Locate the nearest transparent scanline so a tall ear cannot leak into
    // the sprite above it. Original artwork bytes remain unchanged.
    if (columns == 2) for (int split = 1; split < rows; ++split) {
        const int target = split * height / rows, radius = height / rows / 4;
        int best = target, fewest = width + 1;
        for (int y = std::max(rowCuts[split - 1] + 1, target - radius);
             y <= std::min(height - (rows - split), target + radius); ++y) {
            const BYTE* pixels = static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
            int occupied = 0;
            for (int x = 0; x < width; ++x) if (pixels[x * 4 + 3] > 16) ++occupied;
            if (occupied < fewest || (occupied == fewest && std::abs(y - target) < std::abs(best - target))) {
                fewest = occupied; best = y;
            }
        }
        rowCuts[split] = best;
    }
    for (int index = 0; index < frameCount; ++index) {
        const int col = index % columns, rowIndex = index / columns;
        const int x0 = col * width / columns, x1 = (col + 1) * width / columns;
        const int y0 = rowCuts[rowIndex], y1 = rowCuts[rowIndex + 1];
        int left = x1, top = y1, right = x0 - 1, bottom = y0 - 1;
        for (int y = y0; y < y1; ++y) {
            const BYTE* row = static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
            for (int x = x0; x < x1; ++x) if (row[x * 4 + 3] > 16) {
                left = std::min(left, x); right = std::max(right, x); top = std::min(top, y); bottom = std::max(bottom, y);
            }
        }
        if (right < left || bottom < top) { atlas->UnlockBits(&data); return false; }
        left = std::max(x0, left - 2); top = std::max(y0, top - 2); right = std::min(x1 - 1, right + 2); bottom = std::min(y1 - 1, bottom + 2);
        frames[index] = Rect(left, top, right - left + 1, bottom - top + 1);
        if (index < 6) standingHeight = std::max(standingHeight, bottom - top + 1);
    }
    // Prepare reusable premultiplied crops once. Joint animation deforms a
    // cropped texture, never allocates a full atlas on every animation frame.
    for (int index = 0; index < frameCount; ++index) {
        const bool needsSkin = resourceId == 107 ||
            (resourceId == 105 && (index == 0 || index == 4)) ||
            ((resourceId == 101 || resourceId == 103) && index == 4) ||
            ((resourceId == 102 || resourceId == 104) && (index == 1 || index == 2));
        if (!needsSkin) continue;
        const auto& source = frames[index];
        auto& skin = sheet.skins[index];
        skin.width = source.Width; skin.height = source.Height;
        skin.source.resize(static_cast<std::size_t>(skin.width) * skin.height);
        skin.deformed.resize(skin.source.size());
        skin.body.resize(skin.source.size()); skin.head.resize(skin.source.size());
        for (int y = 0; y < skin.height; ++y) {
            const auto* row = static_cast<const BYTE*>(data.Scan0) +
                static_cast<ptrdiff_t>(y + source.Y) * data.Stride + source.X * 4;
            for (int x = 0; x < skin.width; ++x) {
                const auto* pixel = row + x * 4;
                const unsigned a = pixel[3];
                skin.source[static_cast<std::size_t>(y) * skin.width + x] =
                    (a << 24) | ((pixel[2] * a + 127) / 255 << 16) |
                    ((pixel[1] * a + 127) / 255 << 8) | ((pixel[0] * a + 127) / 255);
            }
        }
        skin.image = std::make_unique<Bitmap>(skin.width, skin.height, skin.width * 4,
            PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(skin.deformed.data()));
        skin.bodyImage = std::make_unique<Bitmap>(skin.width, skin.height, skin.width * 4,
            PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(skin.body.data()));
        skin.headImage = std::make_unique<Bitmap>(skin.width, skin.height, skin.width * 4,
            PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(skin.head.data()));
    }
    atlas->UnlockBits(&data); return true;
}
bool LoadAtlas() {
    for (int i = 0; i < 2; ++i) {
        auto& art = g.characters[i];
        if (!LoadAtlasResource(101 + i * 2, art.base) || !LoadAtlasResource(102 + i * 2, art.actions)) return false;
        // The relaxed sitting pose is the size reference across a character's sheets.
        art.actions.standingHeight = std::max(1, art.actions.frames[6].Height);
    }
    if (!LoadAtlasResource(105, g.jumpTreat, 2) || !LoadAtlasResource(106, g.gestures, 2) ||
        !LoadAtlasResource(107, g.recline, 2, 4)) return false;
    for (int i = 0; i < 2; ++i) {
        g.extraStandingHeight[i] = std::max(g.jumpTreat.frames[i * 4 + 2].Height, g.jumpTreat.frames[i * 4 + 3].Height);
        g.gestureStandingHeight[i] = std::max(g.gestures.frames[i * 4].Height, g.gestures.frames[i * 4 + 1].Height);
    }
    return true;
}
void ReleaseAtlases() {
    for (auto& art : g.characters) { art.base.Reset(); art.actions.Reset(); }
    g.jumpTreat.Reset(); g.gestures.Reset(); g.recline.Reset();
}

UINT MonitorDpi(HMONITOR monitor) {
    using DpiProc = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static HMODULE shcore = LoadLibraryW(L"Shcore.dll");
    static DpiProc proc = shcore ? reinterpret_cast<DpiProc>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
    UINT x = 96, y = 96;
    if (proc && SUCCEEDED(proc(monitor, 0, &x, &y))) return std::clamp(x, 96U, 480U);
    return 96;
}
std::pair<double, double> MovementBoundsLogical() {
    const double width = (g.work.right - g.work.left) / g.dpiScale;
    if (g.perch.active && !g.perch.transitioning && IsWindow(g.perch.hwnd)) {
        const double left = (std::max(g.perch.rect.left, g.work.left) - g.work.left) / g.dpiScale;
        const double right = (std::min(g.perch.rect.right, g.work.right) - g.work.left) / g.dpiScale;
        const double platformWidth = std::max(1.0, right - left);
        const double margin = std::min(70.0, platformWidth / 2.0);
        return {left + margin, right - margin};
    }
    const double margin = std::min(96.0, width / 2.0);
    return {margin, width - margin};
}
void ResetMovement() {
    if (g.work.right <= g.work.left || g.dpiScale <= 0) return;
    const double width = (g.work.right - g.work.left) / g.dpiScale;
    const double margin = std::min(96.0, width / 2.0);
    const double low = margin, high = width - margin;
    const double current = std::clamp((g.centerX - g.work.left) / g.dpiScale, low, high);
    g.centerX = g.work.left + current * g.dpiScale;
    g.physics.Reset(current, low, high);
    // Reserve space for the tallest pose/head. A rapidly raised window can
    // impart a real upward velocity; the work-area roof keeps it visible.
    g.physics.SetCeiling(std::max(1.0, (g.work.bottom - g.work.top) / g.dpiScale - 160.0));
    // Only an explicit drag/monitor reset rebuilds the world. Transfers and
    // moving windows retain the same dynamic body and its momentum.
    g.perch.hwnd = nullptr;
    g.perch.active = g.perch.transitioning = g.perch.preparing = false;
    g.perch.landingOnWindow = false;
    g.perch.rect = {};
    g.physicsReady = true;
    g.gait.Reset();
    g.jumpPending = g.jumpLaunched = false;
    g.waitingForStop = false; g.stopSoundPending = false;
    g.landedAt = 0;
    g.facing = g.direction;
    g.lastPhysicsTime = MotionClock();
}
double WalkingVelocity(const cat::physics::Pose& pose) {
    return pose.velocityX - pose.supportVelocityX;
}

void RefreshAnimationTimer() {
    if (!g.window) return;
    const bool resting = (g.motion == Motion::Sleep || g.motion == Motion::Loaf || g.motion == Motion::BoxSit) &&
        (!g.physicsReady || (g.physics.CurrentPose().grounded && std::abs(WalkingVelocity(g.physics.CurrentPose())) < .5));
    const UINT interval = g.hidden ? 250 : (g.menuOpen || g.dragging || resting ? 40 : 16);
    const bool precise = interval == 16;
    if (precise && !g.preciseTimer) g.preciseTimer = timeBeginPeriod(1) == TIMERR_NOERROR;
    else if (!precise && g.preciseTimer) { timeEndPeriod(1); g.preciseTimer = false; }
    if (interval != g.timerInterval) {
        SetTimer(g.window, kTickTimer, interval, nullptr);
        g.timerInterval = interval;
    }
}
void KeepOnWorkArea(bool reposition = false) {
    MONITORINFO info{}; info.cbSize = sizeof(info);
    if (!g.monitor || !GetMonitorInfoW(g.monitor, &info)) {
        POINT cursor{}; GetCursorPos(&cursor); g.monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        GetMonitorInfoW(g.monitor, &info); reposition = true;
    }
    const double oldWidth = std::max(1L, g.work.right - g.work.left);
    const double fraction = g.initialized ? std::clamp((g.centerX - g.work.left) / oldWidth, 0.0, 1.0) : g.pet.position;
    const bool changed = !EqualRect(&g.work, &info.rcWork);
    if (changed || reposition) { g.prank.Clear(); g.toy = ToyKind::None; if (g.perch.active || g.perch.transitioning) CancelPerch(); }
    g.work = info.rcWork;
    const double dpi = MonitorDpi(g.monitor) / 96.0;
    const bool dpiChanged = !g.surface.image || std::abs(dpi - g.dpiScale) > 0.01;
    if (dpiChanged) {
        g.dpiScale = dpi;
        g.prank.Clear();
        if (!g.surface.Create(static_cast<int>(std::round(320 * dpi)), static_cast<int>(std::round(300 * dpi)))) {
            PostMessageW(g.window, WM_CLOSE, 0, 0); return;
        }
        g.blendFrom.Create(g.surface.width, g.surface.height);
        g.blendTo.Create(g.surface.width, g.surface.height);
    }
    if (changed || reposition || !g.initialized) g.centerX = g.work.left + fraction * (g.work.right - g.work.left);
    const double margin = 96 * g.dpiScale;
    if (g.work.right - g.work.left > margin * 2) g.centerX = std::clamp(g.centerX, g.work.left + margin, g.work.right - margin);
    else g.centerX = (g.work.left + g.work.right) / 2.0;
    if (changed || dpiChanged || reposition || !g.physicsReady) ResetMovement();
}
double Random(double low, double high) { return std::uniform_real_distribution<double>(low, high)(g.random); }
void SetMotion(Motion motion, ULONGLONG duration) {
    if (motion == Motion::Peek) duration = cat::animation::kPeekDurationMs;
    if (motion == Motion::BellyRoll) duration = cat::animation::kBellyRollDurationMs;
    if (g.motion == Motion::Walk || g.motion == Motion::Stalk || g.motion == Motion::Zoomies)
        g.walkingMotion = g.motion;
    g.waitingForStop = g.physicsReady && cat::motion::NeedsStop(motion,
        g.physics.CurrentPose().grounded, WalkingVelocity(g.physics.CurrentPose()), g.perch.transitioning);
    g.stopSoundPending = false;
    const auto ticks = GetTickCount64(); g.motion = motion; g.motionStart = ticks; g.nextDecision = ticks + duration;
    g.punchHits = 0;
    g.motionSoundPending = false;
    g.gestureSoundPending = false;
    g.jumpPending = motion == Motion::Jump || motion == Motion::Pounce;
    g.jumpLaunched = false;
    g.approachingToy = false; g.toyContact = false;
    g.approachingUser = false; g.memorySequenceCount = 0;
    if (motion == Motion::Sleep || g.waitingForStop) g.audio.Stop();
    RefreshAnimationTimer();
}
struct PerchCandidate { HWND hwnd = nullptr; RECT rect{}; double score = 0.0; };

bool QueryPerchRect(HWND hwnd, RECT& rect) {
    if (!hwnd || hwnd == g.window || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd) || IsZoomed(hwnd)) return false;
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(style & WS_VISIBLE) || (style & WS_CHILD) || (exStyle & WS_EX_TOOLWINDOW) || GetWindow(hwnd, GW_OWNER)) return false;
    wchar_t cls[80]{}; GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
    if (wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
        wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) return false;
    if (GetWindowTextLengthW(hwnd) <= 0) return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;
    RECT frame{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
        if (!GetWindowRect(hwnd, &frame)) return false;
    }
    if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != g.monitor) return false;
    const LONG width = frame.right - frame.left, height = frame.bottom - frame.top;
    const LONG minWidth = static_cast<LONG>(std::round(300 * g.dpiScale));
    const LONG minHeight = static_cast<LONG>(std::round(160 * g.dpiScale));
    const LONG edge = static_cast<LONG>(std::round(70 * g.dpiScale));
    if (width < minWidth || height < minHeight || width <= edge * 2) return false;
    if (frame.top <= g.work.top + static_cast<LONG>(200 * g.dpiScale) ||
        frame.top >= g.work.bottom - static_cast<LONG>(115 * g.dpiScale)) return false;
    if (frame.right <= g.work.left + edge || frame.left >= g.work.right - edge) return false;
    if (std::min(frame.right, g.work.right) - std::max(frame.left, g.work.left) < minWidth) return false;
    rect = frame;
    return true;
}

BOOL CALLBACK CollectPerchWindows(HWND hwnd, LPARAM parameter) {
    auto* list = reinterpret_cast<std::vector<PerchCandidate>*>(parameter);
    RECT rect{};
    if (!QueryPerchRect(hwnd, rect)) return TRUE;
    const double left = std::max(rect.left, g.work.left) + 72 * g.dpiScale;
    const double right = std::min(rect.right, g.work.right) - 72 * g.dpiScale;
    if (right <= left) return TRUE;
    const double targetX = std::clamp(g.centerX, left, right);
    if (!g.physics.CanLaunchTo((targetX - g.work.left) / g.dpiScale,
            (g.work.bottom - rect.top) / g.dpiScale)) return TRUE;
    const double horizontal = std::abs(targetX - g.centerX);
    const double vertical = std::max(0L, g.work.bottom - rect.top);
    // Prefer a nearby window, with a small bias toward lower/easier ledges.
    list->push_back({hwnd, rect, horizontal + vertical * .18});
    return TRUE;
}

bool FindPerch(PerchCandidate& choice) {
    std::vector<PerchCandidate> candidates;
    EnumWindows(CollectPerchWindows, reinterpret_cast<LPARAM>(&candidates));
    if (candidates.empty()) return false;
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.score < b.score; });
    const std::size_t pool = std::min<std::size_t>(3, candidates.size());
    choice = candidates[static_cast<std::size_t>(Random(0, static_cast<double>(pool) - .0001))];
    return true;
}

LONG CurrentGroundY(ULONGLONG) {
    // One coordinate system for the taskbar, ledges and airborne motion.
    // Move the layered window with the body so high jumps cannot clip the art
    // against the small offscreen drawing surface.
    return cat::render::BuildPlacement(g.work.bottom, g.surface.height, g.dpiScale,
        g.physics.RenderPose().lift, 0, g.dpiScale).screenFeetY;
}

void CancelPerch() {
    g.perch.hwnd = nullptr;
    g.perch.active = g.perch.transitioning = g.perch.preparing = false;
    g.perch.landingOnWindow = false;
    g.perch.rect = {};
    if (g.physicsReady) g.physics.RemovePlatform();
}

void SetPerchCollider(const RECT& rect) {
    g.physics.SetPlatform((std::max(rect.left, g.work.left) - g.work.left) / g.dpiScale,
        (std::min(rect.right, g.work.right) - g.work.left) / g.dpiScale,
        (g.work.bottom - rect.top) / g.dpiScale);
}

void BeginPerchTransition(HWND hwnd, const RECT& rect, bool landingOnWindow) {
    const auto ticks = GetTickCount64();
    if (landingOnWindow) {
        const double margin = 78 * g.dpiScale;
        const double low = std::max(rect.left, g.work.left) + margin;
        const double high = std::min(rect.right, g.work.right) - margin;
        if (high < low) return;
        const double desired = std::clamp(g.centerX + Random(-130, 130) * g.dpiScale, low, high);
        const double target = (desired - g.work.left) / g.dpiScale;
        if (!g.physics.CanLaunchTo(target, (g.work.bottom - rect.top) / g.dpiScale)) {
            g.perch.nextTry = ticks + 60000;
            return;
        }
        g.perch.targetX = target;
        g.perch.hwnd = hwnd;
        g.perch.rect = rect;
        SetPerchCollider(rect);
        g.direction = desired < g.centerX ? -1 : 1;
    } else {
        // Closing/removing a support never restarts the trajectory or invents
        // an upward hop. Gravity acts on the current body and velocity.
        g.physics.RemovePlatform();
        g.perch.hwnd = nullptr;
        g.perch.rect = {};
    }
    if (g.brain) g.brain->Interrupt(ticks);
    g.perch.active = false;
    g.perch.transitioning = true;
    g.perch.landingOnWindow = landingOnWindow;
    g.perch.preparing = landingOnWindow;
    g.facing = g.direction;
    SetMotion(Motion::Jump, 6000);
    g.physics.SetTargetVelocity(0);
    g.jumpPending = g.jumpLaunched = false;
    g.airborneMotion = Motion::Jump;
}

void FinishPerchTransition(ULONGLONG ticks) {
    const auto physical = g.physics.CurrentPose();
    const bool landedOnWindow = physical.onPlatform && g.perch.landingOnWindow;
    g.perch.transitioning = g.perch.preparing = false;
    g.perch.active = landedOnWindow;
    if (landedOnWindow) {
        g.perch.leaveAfter = ticks + static_cast<ULONGLONG>(Random(22000, 52000));
        g.perch.nextTry = ticks + static_cast<ULONGLONG>(Random(150000, 300000));
    } else {
        g.physics.RemovePlatform();
        g.perch.hwnd = nullptr; g.perch.rect = {};
        g.perch.nextTry = ticks + static_cast<ULONGLONG>(Random(150000, 360000));
    }
    if (physical.landed) {
        g.landedAt = ticks;
        g.landingStrength = std::clamp(physical.landingSpeed / 230.0, .35, 1.0);
    }
    g.physics.SetTargetVelocity(0);
    SetMotion(Motion::Watch, static_cast<ULONGLONG>(Random(1700, 3000)));
    if (landedOnWindow && Random(0, 1) < .38) PlayCatSound(cat::audio::Cue::Greeting, true);
}

void UpdatePerchTransition(ULONGLONG ticks, double elapsed) {
    if (!g.perch.transitioning) return;
    if (g.perch.preparing && (g.pet.roamingPaused || g.motion != Motion::Jump)) {
        // Rest/manual commands can cancel a planned jump before its impulse.
        // Airborne transfers keep their real trajectory until a contact.
        g.physics.RemovePlatform();
        g.perch.landingOnWindow = g.perch.preparing = false;
        g.perch.hwnd = nullptr; g.perch.rect = {};
    }
    if (g.perch.landingOnWindow) {
        RECT current{};
        if (!QueryPerchRect(g.perch.hwnd, current)) {
            g.physics.RemovePlatform();
            g.perch.landingOnWindow = g.perch.preparing = false;
            g.perch.hwnd = nullptr; g.perch.rect = {};
        } else {
            // Before takeoff a moved target can change the planned impulse.
            // Once airborne only the ledge moves; the cat keeps its momentum.
            if (g.perch.preparing) {
                g.perch.targetX = std::clamp(g.perch.targetX + (current.left - g.perch.rect.left) / g.dpiScale,
                    (std::max(current.left, g.work.left) - g.work.left) / g.dpiScale + 78,
                    (std::min(current.right, g.work.right) - g.work.left) / g.dpiScale - 78);
            }
            SetPerchCollider(current);
            g.perch.rect = current;
        }
    }
    if (g.perch.preparing) {
        g.physics.SetTargetVelocity(0);
        const auto physical = g.physics.CurrentPose();
        g.waitingForStop = std::abs(WalkingVelocity(physical)) > 2;
        if (g.waitingForStop) g.motionStart = ticks;
        else if (ticks >= g.motionStart + 220) {
            if (g.physics.LaunchTo(g.perch.targetX, (g.work.bottom - g.perch.rect.top) / g.dpiScale)) {
                g.jumpLaunched = true;
                PlayCatSound(cat::audio::Cue::Jump, true);
            } else {
                g.physics.RemovePlatform();
                g.perch.landingOnWindow = false;
            }
            g.perch.preparing = false;
        }
    }
    const double previousX = g.physics.CurrentPose().x;
    g.physics.Update(elapsed);
    const auto physical = g.physics.CurrentPose();
    g.centerX = g.work.left + g.physics.RenderPose().x * g.dpiScale;
    if (std::abs(WalkingVelocity(physical)) > 2) g.facing = WalkingVelocity(physical) < 0 ? -1 : 1;
    if (g.waitingForStop)
        g.gait.Advance(g.walkingMotion, (physical.x - previousX - physical.supportTravelX) / cat::GrowthScale(g.pet.bornUtc, UtcNow()), elapsed);
    if (!g.perch.preparing && physical.grounded) FinishPerchTransition(ticks);
}

void MaintainPerch(ULONGLONG ticks) {
    if (!g.perch.active || g.perch.transitioning) return;
    RECT current{};
    if (!QueryPerchRect(g.perch.hwnd, current)) {
        BeginPerchTransition(nullptr, {}, false);
        return;
    }
    SetPerchCollider(current);
    g.perch.rect = current;
    if (!g.pet.roamingPaused && !g.waitingForStop && ticks >= g.perch.leaveAfter && g.physics.CurrentPose().grounded &&
        (g.motion == Motion::Idle || g.motion == Motion::Walk || g.motion == Motion::Watch || g.motion == Motion::Groom))
        BeginPerchTransition(nullptr, {}, false);
}

void MaybeVisitWindow(ULONGLONG ticks) {
    if (g.perch.active || g.perch.transitioning || g.approachingToy || g.approachingUser || g.memorySequenceCount || g.pet.roamingPaused || ticks < g.perch.nextTry ||
        ticks < g.bubbleUntil || g.menuOpen || g.dragging || !g.physics.CurrentPose().grounded) return;
    if (g.motion != Motion::Idle && g.motion != Motion::Walk && g.motion != Motion::Watch && g.motion != Motion::Groom) return;
    PerchCandidate candidate;
    if (!FindPerch(candidate)) {
        g.perch.nextTry = ticks + static_cast<ULONGLONG>(Random(90000, 180000));
        return;
    }
    // It should feel like a rare bit of cat logic, not a constant window tour.
    if (Random(0, 1) > .58) {
        g.perch.nextTry = ticks + static_cast<ULONGLONG>(Random(80000, 180000));
        return;
    }
    BeginPerchTransition(candidate.hwnd, candidate.rect, true);
}

void RoundedRect(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL radius) {
    const REAL d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90); path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90); path.AddArc(x, y + h - d, d, d, 90, 90); path.CloseFigure();
}
void DrawOutfit(Graphics& graphics, int frame, REAL width, REAL height, bool centerPivot,
                const cat::deform::Joints& joints = {}, cat::deform::Shape shape = cat::deform::Shape::Sitting) {
    if (g.preferences.outfit == cat::Outfit::None) return;
    const auto anchors = cat::render::AnchorsForFrame(frame);
    const auto warpedNeck = cat::deform::DeformPoint({anchors.neck.x, anchors.neck.y}, joints, shape);
    const auto warpedCrown = cat::deform::DeformPoint({anchors.crown.x, anchors.crown.y}, joints, shape);
    const auto neckPoint = cat::render::LocalPoint({warpedNeck.x, warpedNeck.y}, width, height, centerPivot);
    const auto crownPoint = cat::render::LocalPoint({warpedCrown.x, warpedCrown.y}, width, height, centerPivot);
    const REAL x = static_cast<REAL>(neckPoint.x), neck = static_cast<REAL>(neckPoint.y);
    const REAL unit = static_cast<REAL>(anchors.headWidth * width / 80.0);
    if (g.preferences.outfit == cat::Outfit::BowTie) {
        SolidBrush ribbon(Color(235, 218, 70, 66));
        SolidBrush knot(Color(245, 245, 184, 68));
        graphics.FillEllipse(&ribbon, x - 18*unit, neck - 7*unit, 17*unit, 14*unit);
        graphics.FillEllipse(&ribbon, x + 1*unit, neck - 7*unit, 17*unit, 14*unit);
        graphics.FillEllipse(&knot, x - 5*unit, neck - 5*unit, 10*unit, 10*unit);
    } else if (g.preferences.outfit == cat::Outfit::Scarf) {
        Pen scarf(Color(235, 213, 92, 79), 8*unit);
        scarf.SetStartCap(LineCapRound); scarf.SetEndCap(LineCapRound);
        graphics.DrawArc(&scarf, x - 25*unit, neck - 12*unit, 50*unit, 25*unit, 12, 156);
        graphics.DrawLine(&scarf, x + 12*unit, neck + 4*unit, x + 22*unit, neck + 28*unit);
        Pen trim(Color(210, 245, 210, 170), 1.3f*unit);
        graphics.DrawLine(&trim, x + 17*unit, neck + 19*unit, x + 25*unit, neck + 20*unit);
    } else if (g.preferences.outfit == cat::Outfit::Crown) {
        const REAL x = static_cast<REAL>(crownPoint.x);
        const REAL y = static_cast<REAL>(crownPoint.y - 14 * unit);
        PointF points[7] = {{x-18*unit,y+14*unit},{x-15*unit,y},{x-5*unit,y+9*unit},{x,y-3*unit},
                            {x+6*unit,y+9*unit},{x+16*unit,y},{x+18*unit,y+14*unit}};
        SolidBrush gold(Color(240, 242, 190, 58)); Pen border(Color(235, 151, 103, 28), 1.2f*unit);
        graphics.FillPolygon(&gold, points, 7); graphics.DrawPolygon(&border, points, 7);
        SolidBrush jewel(Color(230, 218, 64, 80)); graphics.FillEllipse(&jewel, x-2.5f*unit, y+7*unit, 5*unit, 5*unit);
    }
}

bool PrepareSkin(SpriteAtlas::Skin& skin, int frame, cat::deform::Joints& joints, cat::deform::Shape shape, bool parts = false) {
    std::array<double*, 8> values{{&joints.leftEar, &joints.rightEar, &joints.headNod,
        &joints.frontLeft, &joints.frontRight, &joints.tailRaise, &joints.breath, &joints.crouch}};
    std::array<int, 8> controls{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        controls[i] = static_cast<int>(std::lround(*values[i] * 32));
        *values[i] = controls[i] / 32.0;
    }
    if (!skin.valid || skin.controls != controls) {
        skin.valid = cat::deform::Warp(skin.source.data(), skin.width, skin.height, skin.width * 4,
            skin.deformed.data(), skin.width * 4, joints, shape);
        skin.controls = controls;
        skin.partsValid = false;
    }
    if (skin.valid && parts && !skin.partsValid) {
        for (int y = 0; y < skin.height; ++y) for (int x = 0; x < skin.width; ++x) {
            const auto i = static_cast<std::size_t>(y) * skin.width + x;
            const double coverage = cat::belly::HeadCoverage(frame,
                {(x + .5) / skin.width, (y + .5) / skin.height});
            skin.body[i] = cat::belly::MaskPixel(skin.deformed[i], coverage, false);
            skin.head[i] = cat::belly::MaskPixel(skin.deformed[i], coverage, true);
        }
        skin.partsValid = true;
    }
    return skin.valid;
}

void DrawPet(Graphics& graphics, int frame, REAL center, REAL bottom, REAL scale, bool flip,
             REAL scaleX = 1, REAL scaleY = 1, REAL angle = 0, bool centerPivot = false,
             cat::deform::Joints joints = {}, bool groundContact = true, bool bodyOnly = false) {
    if (frame < 0 || frame >= cat::animation::kSpriteCount) return;
    auto& art = CurrentArt();
    SpriteAtlas* atlas = &art.base;
    int cell = frame;
    if (frame >= 24) {
        atlas = &g.recline;
        cell = static_cast<int>(CharacterIndex()) * 2 + frame - 24;
        // A common head-size reference for all four reclining poses. Scaling
        // each pose to standing height would make the lying cat much larger.
        scale *= static_cast<REAL>(art.base.standingHeight) / 440.0f;
    } else if (frame >= 20) {
        atlas = &g.gestures;
        cell = static_cast<int>(CharacterIndex()) * 4 + frame - 20;
        scale *= static_cast<REAL>(art.base.standingHeight) / g.gestureStandingHeight[CharacterIndex()];
    } else if (frame >= 16) {
        atlas = &g.jumpTreat;
        cell = static_cast<int>(CharacterIndex()) * 4 + frame - 16;
        scale *= static_cast<REAL>(art.base.standingHeight) / g.extraStandingHeight[CharacterIndex()];
    } else if (frame >= 8) {
        atlas = &art.actions; cell = frame - 8;
        scale *= static_cast<REAL>(art.base.standingHeight) / art.actions.standingHeight;
    }
    const Rect& source = atlas->frames[cell];
    const REAL width = source.Width * scale * scaleX, height = source.Height * scale * scaleY;
    const GraphicsState saved = graphics.Save();
    const REAL clearance = groundContact ? static_cast<REAL>(cat::render::GroundClearance(width, height, angle, centerPivot)) : 0;
    graphics.TranslateTransform(center, bottom - clearance - (centerPivot ? height / 2 : 0));
    graphics.RotateTransform(angle);
    graphics.ScaleTransform(flip ? -1.0f : 1.0f, 1.0f);
    const RectF destination = centerPivot ? RectF(-width / 2, -height / 2, width, height)
                                         : RectF(-width / 2, -height, width, height);
    ImageAttributes attrs; attrs.SetWrapMode(WrapModeTileFlipXY);
    const auto shape = frame == 16 ? cat::deform::Shape::Airborne :
        frame == 25 ? cat::deform::Shape::ReclineBack :
        frame == 24 ? cat::deform::Shape::ReclineSide : cat::deform::Shape::Sitting;
    const bool articulated = frame == 4 || frame == 9 || frame == 10 || frame == 16 || frame >= 24;
    if (articulated) {
        auto& skin = atlas->skins[cell];
        if (PrepareSkin(skin, frame, joints, shape, bodyOnly)) graphics.DrawImage(bodyOnly ? skin.bodyImage.get() : skin.image.get(), destination, 0, 0,
            static_cast<REAL>(skin.width), static_cast<REAL>(skin.height), UnitPixel, &attrs);
    } else {
        graphics.DrawImage(atlas->image.get(), destination, static_cast<REAL>(source.X), static_cast<REAL>(source.Y), static_cast<REAL>(source.Width), static_cast<REAL>(source.Height), UnitPixel, &attrs);
        joints = {};
    }
    if (!bodyOnly) DrawOutfit(graphics, frame, width, height, centerPivot, joints, shape);
    graphics.Restore(saved);
}

void DrawBellyHead(Graphics& graphics, const cat::animation::Pose& pose, REAL center, REAL bottom, REAL scale) {
    auto& art = CurrentArt();
    const auto body = cat::nutrition::VisualModifiers(g.nutrition);
    auto dimensions = [&](int frame) {
        const auto& source = frame == 4 ? art.base.frames[4] :
            g.recline.frames[static_cast<int>(CharacterIndex()) * 2 + frame - 24];
        const REAL factor = frame == 4 ? scale : scale * art.base.standingHeight / 440.0f;
        return std::pair<double, double>{source.Width * factor * body.bodyWidth,
            source.Height * factor * body.bodyHeight};
    };
    const int second = pose.blendFrame < 0 ? pose.frame : pose.blendFrame;
    const auto a = dimensions(pose.frame), b = dimensions(second), headSize = dimensions(4);
    const auto bone = cat::belly::BlendBone(pose.frame, a.first, a.second,
        second, b.first, b.second, pose.blendFrame < 0 ? 0 : pose.blend);
    auto joints = pose.joints;
    joints.frontLeft = joints.frontRight = joints.tailRaise = joints.breath = joints.crouch = 0;
    auto& skin = art.base.skins[4];
    if (!PrepareSkin(skin, 4, joints, cat::deform::Shape::Sitting, true)) return;
    const auto saved = graphics.Save();
    graphics.TranslateTransform(center, bottom);
    graphics.ScaleTransform(pose.flip ? -1.0f : 1.0f, 1.0f);
    graphics.TranslateTransform(static_cast<REAL>(bone.x), static_cast<REAL>(bone.y));
    graphics.RotateTransform(static_cast<REAL>(bone.angle));
    const REAL width = static_cast<REAL>(headSize.first), height = static_cast<REAL>(headSize.second);
    ImageAttributes attrs; attrs.SetWrapMode(WrapModeTileFlipXY);
    graphics.DrawImage(skin.headImage.get(), RectF(-.57f * width, -.40f * height, width, height),
        0, 0, static_cast<REAL>(skin.width), static_cast<REAL>(skin.height), UnitPixel, &attrs);
    graphics.TranslateTransform(-.07f * width, .60f * height);
    DrawOutfit(graphics, 4, width, height, false, joints);
    graphics.Restore(saved);
}
void DrawAnimatedPet(Graphics& graphics, const cat::animation::Pose& pose,
                     REAL center, REAL bottom, REAL scale) {
    const auto body = cat::nutrition::VisualModifiers(g.nutrition);
    auto draw = [&](Graphics& target, int frame) {
        DrawPet(target, frame, center, bottom, scale, pose.flip,
                static_cast<REAL>(pose.scaleX * body.bodyWidth),
                static_cast<REAL>(pose.scaleY * body.bodyHeight), static_cast<REAL>(pose.angle), pose.centerPivot,
                pose.joints, pose.groundContact, pose.bellyRig);
    };
    if (pose.blendFrame < 0 || pose.blend <= 0 || !g.blendFrom.image || !g.blendTo.image) {
        draw(graphics, pose.frame);
        if (pose.bellyRig) DrawBellyHead(graphics, pose, center, bottom, scale);
        return;
    }
    if (pose.blend >= 1) {
        draw(graphics, pose.blendFrame);
        if (pose.bellyRig) DrawBellyHead(graphics, pose, center, bottom, scale);
        return;
    }
    auto layer = [&](Surface& buffer, int frame) {
        Graphics target(buffer.image.get());
        target.SetCompositingMode(CompositingModeSourceCopy); target.Clear(Color(0, 0, 0, 0));
        target.SetCompositingMode(CompositingModeSourceOver);
        target.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        target.SetSmoothingMode(SmoothingModeAntiAlias);
        target.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        draw(target, frame); target.Flush(FlushIntentionSync);
    };
    layer(g.blendFrom, pose.frame); layer(g.blendTo, pose.blendFrame);
    const unsigned weight = static_cast<unsigned>(std::lround(std::clamp(pose.blend, 0.0, 1.0) * 255));
    const std::size_t bytes = static_cast<std::size_t>(g.surface.width) * g.surface.height * 4;
    // Interpolate premultiplied BGRA itself. Drawing two translucent images over
    // one another would make the cat's opaque body transparent midway through.
    for (std::size_t i = 0; i < bytes; ++i)
        g.blendFrom.pixels[i] = static_cast<BYTE>((g.blendFrom.pixels[i] * (255 - weight) +
                                                 g.blendTo.pixels[i] * weight + 127) / 255);
    graphics.DrawImage(g.blendFrom.image.get(), Rect(0, 0, g.surface.width, g.surface.height),
                       0, 0, g.surface.width, g.surface.height, UnitPixel);
    if (pose.bellyRig) DrawBellyHead(graphics, pose, center, bottom, scale);
}

void DrawToyBackground(Graphics& graphics, REAL localX, REAL ground, ULONGLONG ticks) {
    if (g.toy == ToyKind::None) return;
    const REAL u = static_cast<REAL>(g.dpiScale);
    (void)ticks;
    if (g.toy == ToyKind::Ball) {
        const REAL bounce = 0;
        SolidBrush shadow(Color(55, 50, 45, 40)); graphics.FillEllipse(&shadow, localX-13*u, ground-5*u, 26*u, 7*u);
        SolidBrush ball(Color(245, 236, 111, 76)); Pen seam(Color(180, 159, 68, 46), 1.2f*u);
        graphics.FillEllipse(&ball, localX-12*u, ground-25*u-bounce, 24*u, 24*u);
        graphics.DrawArc(&seam, localX-9*u, ground-22*u-bounce, 18*u, 18*u, 210, 145);
        SolidBrush shine(Color(170,255,255,255)); graphics.FillEllipse(&shine,localX-6*u,ground-20*u-bounce,5*u,5*u);
    } else if (g.toy == ToyKind::Box) {
        const REAL u = static_cast<REAL>(g.dpiScale * std::max(1.35, 1.55 * cat::GrowthScale(g.pet.bornUtc, UtcNow()) * cat::nutrition::VisualModifiers(g.nutrition).bodyWidth));
        SolidBrush back(Color(235, 188, 137, 82)); Pen edge(Color(220, 126, 82, 48), 1.3f*u);
        RectF box(localX-39*u, ground-48*u, 78*u, 48*u);
        graphics.FillRectangle(&back, box); graphics.DrawRectangle(&edge, box);
        PointF flapL[3]={{localX-39*u,ground-48*u},{localX-15*u,ground-62*u},{localX,ground-48*u}};
        PointF flapR[3]={{localX,ground-48*u},{localX+17*u,ground-62*u},{localX+39*u,ground-48*u}};
        SolidBrush flap(Color(230, 208, 157, 96)); graphics.FillPolygon(&flap,flapL,3); graphics.FillPolygon(&flap,flapR,3);
    }
}

void DrawToyForeground(Graphics& graphics, REAL localX, REAL ground) {
    if (g.toy != ToyKind::Box || g.perch.active || g.perch.transitioning || g.motion != Motion::BoxSit) return;
    const REAL u = static_cast<REAL>(g.dpiScale * std::max(1.35, 1.55 * cat::GrowthScale(g.pet.bornUtc, UtcNow()) * cat::nutrition::VisualModifiers(g.nutrition).bodyWidth));
    SolidBrush front(Color(248, 201, 146, 87)); Pen edge(Color(230, 126, 82, 48), 1.3f*u);
    RectF panel(localX-40*u, ground-29*u, 80*u, 30*u);
    graphics.FillRectangle(&front,panel); graphics.DrawRectangle(&edge,panel);
    Pen tape(Color(180, 230, 203, 145), 4*u); graphics.DrawLine(&tape,localX,ground-28*u,localX,ground-1*u);
}

// Toys have their own click-through desktop layer, so walking away cannot
// clip them to the pet's 320-pixel drawing surface. They remain on the floor.
void RenderToy() {
    if (g.hidden || g.toy == ToyKind::None) {
        if (g.toyWindow) ShowWindow(g.toyWindow, SW_HIDE);
        return;
    }
    if (!g.toyWindow) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = g.instance; wc.lpszClassName = L"YeopjariCat.Toy.v1";
        RegisterClassExW(&wc);
        g.toyWindow = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
            WS_EX_TOPMOST | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, g.instance, nullptr);
    }
    if (!g.toyWindow) return;
    const int width = static_cast<int>(std::ceil(240 * g.dpiScale));
    const int height = static_cast<int>(std::ceil(180 * g.dpiScale));
    if ((g.toySurface.width != width || g.toySurface.height != height) && !g.toySurface.Create(width, height)) return;
    Graphics graphics(g.toySurface.image.get());
    graphics.SetCompositingMode(CompositingModeSourceCopy); graphics.Clear(Color(0,0,0,0));
    graphics.SetCompositingMode(CompositingModeSourceOver);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    DrawToyBackground(graphics, width / 2.0f, static_cast<REAL>(height - 1), GetTickCount64());
    graphics.Flush(FlushIntentionSync);
    POINT position{static_cast<LONG>(std::lround(g.toyX - width / 2.0)), g.work.bottom - height};
    SIZE size{width, height}; POINT origin{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(g.toyWindow, screen, &position, &size, g.toySurface.dc, &origin, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    SetWindowPos(g.toyWindow, g.window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void Render() {
    if (!g.window || !g.surface.image || g.hidden) return;
    const ULONGLONG ticks = GetTickCount64();
    const auto now = UtcNow();
    const REAL scale = static_cast<REAL>(110.0 * g.dpiScale * cat::GrowthScale(g.pet.bornUtc, now) / CurrentArt().base.standingHeight);
    const auto physical = g.physics.RenderPose();
    const auto actionElapsed = ticks >= g.motionStart ? ticks - g.motionStart : 0;
    Motion displayed = g.motion;
    // Keep the last walking gait through the short braking distance.
    if (g.waitingForStop || ((displayed == Motion::Idle || displayed == Motion::Watch) && physical.grounded && std::abs(WalkingVelocity(physical)) > 2))
        displayed = g.walkingMotion;
    auto pose = cat::animation::Evaluate(displayed, actionElapsed, ticks, g.facing, g.gait);
    if (displayed == Motion::Jump || displayed == Motion::Pounce) pose.lift = 0;
    if (!physical.grounded) {
        // Box2D owns height and landing time. Art follows the actual vertical
        // velocity, so changing weight or interrupting a jump cannot desync it.
        pose = {};
        pose.groundContact = false;
        pose.flip = g.facing < 0;
        if (g.airborneMotion == Motion::Pounce) {
            pose.frame = 1;
            pose.scaleX = 1.04;
            pose.angle = -g.facing * std::clamp(physical.velocityY / 22.0, -6.0, 6.0);
        } else {
            pose = cat::animation::AirbornePose(physical.velocityY, g.facing);
        }
    } else if (!g.perch.preparing && !g.waitingForStop && (g.jumpLaunched || !g.jumpPending) &&
            (g.motion == Motion::Jump || g.motion == Motion::Pounce)) {
        pose = {}; pose.frame = 14; pose.flip = g.facing < 0;
    }
    if (!pose.bellyRig && g.landedAt && ticks >= g.landedAt && ticks - g.landedAt < 280) {
        const double settle = std::sin((ticks - g.landedAt) / 280.0 * cat::animation::kPi) * g.landingStrength;
        pose.scaleX *= 1 + .035 * settle;
        pose.scaleY *= 1 - .085 * settle;
    }
    Graphics graphics(g.surface.image.get());
    graphics.SetCompositingMode(CompositingModeSourceCopy); graphics.Clear(Color(0, 0, 0, 0));
    graphics.SetCompositingMode(CompositingModeSourceOver); graphics.SetCompositingQuality(CompositingQualityHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic); graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality); graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    const LONG windowLeft = static_cast<LONG>(std::round(g.centerX - g.surface.width / 2.0));
    const REAL center = static_cast<REAL>(g.centerX - windowLeft);
    const double bodyScale = g.dpiScale * cat::GrowthScale(g.pet.bornUtc, now);
    const auto placement = cat::render::BuildPlacement(g.work.bottom, g.surface.height,
        g.dpiScale, physical.lift, pose.lift, bodyScale);
    const REAL bottom = static_cast<REAL>(placement.localBottom);
    if (g.motion == Motion::Zoomies) {
        Pen trail(Color(100, 204, 171, 137), static_cast<REAL>(1.3 * g.dpiScale));
        for (int i = 0; i < 3; ++i) {
            const REAL x = static_cast<REAL>(center - g.direction * (48 + i * 7) * bodyScale);
            const REAL y = static_cast<REAL>(bottom - (18 + i * 7) * bodyScale);
            graphics.DrawLine(&trail, x, y, static_cast<REAL>(x - g.direction * 12 * bodyScale), y);
        }
    }
    const REAL toyLocalX = static_cast<REAL>(g.toyX - windowLeft);
    RenderToy();
    DrawAnimatedPet(graphics, pose, static_cast<REAL>(center + pose.x * bodyScale), bottom, scale);
    DrawToyForeground(graphics, toyLocalX, static_cast<REAL>(g.surface.height - 1));
    if (g.motion == Motion::Sleep) {
        Font font(L"Segoe UI", static_cast<REAL>(13 * g.dpiScale), FontStyleRegular, UnitPixel);
        SolidBrush text(Color(165, 140, 136, 150));
        const REAL rise = static_cast<REAL>((ticks % 2600) / 2600.0 * 13 * g.dpiScale);
        graphics.DrawString(L"z", -1, &font, PointF(center + 40 * g.dpiScale, bottom - 62 * g.dpiScale - rise), &text);
    }
    if (ticks < g.bubbleUntil && !g.bubble.empty()) {
        const bool twoLines = g.bubble.find(L'\n') != std::wstring::npos;
        const REAL width = static_cast<REAL>(132 * g.dpiScale), height = static_cast<REAL>((twoLines ? 44 : 28) * g.dpiScale);
        const REAL x = center - width / 2, y = static_cast<REAL>(g.surface.height - (twoLines ? 172 : 156) * g.dpiScale);
        GraphicsPath bubble; RoundedRect(bubble, x, y, width, height, static_cast<REAL>(11 * g.dpiScale));
        SolidBrush fill(Color(239, 255, 250, 240)); Pen border(Color(210, 204, 181, 162), static_cast<REAL>(0.8 * g.dpiScale));
        graphics.FillPath(&fill, &bubble); graphics.DrawPath(&border, &bubble);
        Font font(L"Malgun Gothic", static_cast<REAL>(11 * g.dpiScale), FontStyleRegular, UnitPixel);
        SolidBrush text(Color(255, 110, 85, 68)); StringFormat format;
        format.SetAlignment(StringAlignmentCenter); format.SetLineAlignment(StringAlignmentCenter);
        graphics.DrawString(g.bubble.c_str(), -1, &font, RectF(x, y, width, height), &format, &text);
    }
    graphics.Flush(FlushIntentionSync);
    POINT destination{windowLeft, placement.windowTop};
    SIZE size{g.surface.width, g.surface.height}; POINT origin{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(g.window, screen, &destination, &size, g.surface.dc, &origin, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}
HICON CreateCatIcon() {
    Surface icon; if (!icon.Create(32, 32)) return CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    {
        Graphics graphics(icon.image.get()); graphics.Clear(Color(0, 0, 0, 0));
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic); graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        const auto& frame = CurrentArt().base.frames[4];
        const REAL scale = 31.0f / std::max(frame.Width, frame.Height);
        DrawPet(graphics, 4, 16, 32, scale, false); graphics.Flush(FlushIntentionSync);
    }
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, nullptr);
    ICONINFO info{}; info.fIcon = TRUE; info.hbmColor = icon.bitmap; info.hbmMask = mask;
    HICON result = CreateIconIndirect(&info); DeleteObject(mask); return result ? result : CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
}
void UpdateTrayTip() {
    const auto age = cat::Age(g.pet.bornUtc, UtcNow());
    std::wstring tip = std::wstring(cat::CharacterName(g.preferences.character)) + L" · 함께 " + std::to_wstring(age.days + 1) + L"일\n밥 Ctrl+Alt+F · 츄르 Ctrl+Alt+T";
    wcsncpy(g.tray.szTip, tip.c_str(), ARRAYSIZE(g.tray.szTip) - 1);
    g.tray.uFlags = NIF_TIP; Shell_NotifyIconW(NIM_MODIFY, &g.tray);
}
void AddTray() {
    g.tray = {}; g.tray.cbSize = sizeof(g.tray); g.tray.hWnd = g.window; g.tray.uID = 1;
    g.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; g.tray.uCallbackMessage = kTrayMessage; g.tray.hIcon = g.trayIcon;
    wcsncpy(g.tray.szTip, kTitle, ARRAYSIZE(g.tray.szTip) - 1);
    Shell_NotifyIconW(NIM_ADD, &g.tray); g.tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &g.tray); UpdateTrayTip();
}
void Speak(const std::wstring& message, ULONGLONG duration = 2600) { g.bubble = message; g.bubbleUntil = GetTickCount64() + duration; }
void MaybeAnnounceUpdate(ULONGLONG ticks) {
    cat::update::NoticeContext context;
    context.now = ticks; context.notBefore = g.ambientSoundAfter;
    context.enabled = updates.automatic && updates.hasUpdate;
    context.visible = !g.hidden;
    context.calm = g.motion == Motion::Idle || g.motion == Motion::Watch || g.motion == Motion::Walk;
    context.busy = g.closing || g.menuOpen || g.dragging || updates.applying ||
        updates.operation != UpdateState::Operation::None || g.prank.Active() ||
        !g.physics.CurrentPose().grounded || g.audio.IsPlaying() ||
        (g.soundSettings.enabled && g.soundSettings.volume > 0 &&
         (ticks < g.nextSoundAfter || ticks < g.nextAutomaticSoundAfter));
    context.bubbleVisible = ticks < g.bubbleUntil;
    if (!updates.announcement.Ready(context)) return;
    Speak(cat::update::kCatUpdateMessage, 6500);
    PlayCatSound(cat::audio::Cue::Greeting, true);
    updates.announcement.MarkAnnounced();
    // Optional notice history is independent of birthday, nutrition and sound
    // preferences. An unwritable history must not interrupt the desktop pet.
    const std::wstring path = g.directory + L"\\update-notice.ini", temporary = path + L".tmp";
    if (!WriteBytes(temporary, updates.announcement.Serialize()) ||
        !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        DeleteFileW(temporary.c_str());
}
void Care(bool snack) {
    if (!g.initialized || g.closing || updates.applying || g.menuOpen || g.dragging) return;
    const auto ticks = GetTickCount64();
    if (g.perch.transitioning || !g.physics.CurrentPose().grounded) {
        g.pendingCare = snack ? 1 : 0;
        return;
    }
    g.pendingCare = -1;
    SyncNutrition(UtcNow());
    const auto result = cat::nutrition::TryFeed(g.nutrition, g.nutrition.updatedUtc, snack, Random(0, 1));
    g.pet.hunger = g.nutrition.fullness;
    if (result != cat::nutrition::FeedResult::Accepted) {
        // Repeated hotkeys neither add food/care counts nor restart the reply.
        if (ticks < g.nextCareReply) return;
        g.nextCareReply = ticks + 12000;
        if (result == cat::nutrition::FeedResult::Full) {
            static constexpr const wchar_t* replies[]{L"배불러…\n조금 이따 먹을래", L"지금은 배가 빵빵해 ♡", L"조금만 쉬었다 먹을래"};
            Speak(replies[std::uniform_int_distribution<int>(0, 2)(g.random)], 3400);
        } else Speak(L"아직 소화 중이야 ♡", 2600);
        PlayCatSound(cat::audio::Cue::Refuse);
        Render();
        return;
    }
    g.prank.Clear();
    if (g.brain) g.brain->Fed(ticks, snack);
    // Keep the established lifetime counts/affection, with nutrition as the
    // sole owner of fullness and elapsed-time body changes.
    cat::Feed(g.pet, g.nutrition.updatedUtc, snack);
    Remember(snack ? cat::memory::Event::Treat : cat::memory::Event::Meal);
    g.pet.hunger = g.nutrition.fullness;
    g.nextCareReply = ticks + 6500;
    SetMotion(snack ? Motion::Treat : Motion::Eat, snack ? cat::animation::kTreatDurationMs : 3300);
    Speak(snack ? L"츄르다! 할짝할짝 ♡" : L"냠냠, 잘 먹을게 ♡", 3000);
    static constexpr cat::audio::Cue mealCues[]{cat::audio::Cue::Meal, cat::audio::Cue::Meal,
        cat::audio::Cue::Meal, cat::audio::Cue::MealFish, cat::audio::Cue::MealNogari};
    PlayCatSound(snack ? cat::audio::Cue::Treat : mealCues[std::uniform_int_distribution<int>(0, 4)(g.random)]);
    SavePet(); Render();
}
void MaybeAskForFood(ULONGLONG ticks) {
    if (g.hidden || g.menuOpen || g.dragging || g.closing || updates.applying || g.prank.Active() ||
        ticks < g.foodRequestAfter || ticks < g.bubbleUntil) return;
    // Quiet requests wait for a calm, awake moment; never interrupt sleeping,
    // eating, a jump, or the screen-punch effect, and never activate the window.
    if (g.motion != Motion::Idle && g.motion != Motion::Walk && g.motion != Motion::Watch) return;
    const bool request = cat::nutrition::NeedsFoodRequest(g.nutrition, std::max(UtcNow(), g.nutrition.updatedUtc));
    g.pet.hunger = g.nutrition.fullness;
    if (!request) return;
    static constexpr const wchar_t* requests[]{L"냥… 밥 조금 줄래?", L"배가 꼬르륵… 밥 줄래?", L"밥 먹고 같이 쉬자 ♡"};
    Speak(requests[std::uniform_int_distribution<int>(0, 2)(g.random)], 4200);
    PlayCatSound(g.nutrition.fullness <= 25 ? cat::audio::Cue::FoodRequest : cat::audio::Cue::Hungry, true);
    SavePet(false); // Persist the two-hour request cooldown across restarts.
}
void ToggleHidden() {
    g.hidden = !g.hidden;
    g.prank.Clear();
    g.audio.Stop();
    if (g.hidden) { ShowWindow(g.window, SW_HIDE); if (g.toyWindow) ShowWindow(g.toyWindow, SW_HIDE); }
    else { g.foodRequestAfter = std::max(g.foodRequestAfter, GetTickCount64() + 30000); g.ambientSoundAfter = std::max(g.ambientSoundAfter, GetTickCount64() + 30000); KeepOnWorkArea(); Render(); ShowWindow(g.window, SW_SHOWNOACTIVATE); SetWindowPos(g.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
    RefreshAnimationTimer();
}
const wchar_t* PersonalityLabel() {
    return g.brain ? cat::behavior::ArchetypeName(g.brain->GetArchetype()) : L"시크냥";
}
void StartPunch(bool forced) {
    if (g.hidden) ToggleHidden();
    // A paw contact effect requires the cat to be standing at its drawn feet.
    if (g.perch.transitioning || !g.physics.CurrentPose().grounded) return;
    g.prank.Clear();
    if (forced && g.brain) g.brain->MarkPunch(GetTickCount64());
    g.direction = g.centerX < (g.work.left + g.work.right) / 2.0 ? 1 : -1;
    g.bubbleUntil = 0;
    SetMotion(Motion::Punch, cat::animation::kPunchDurationMs);
    g.punchSoundAutomatic = !forced;
    g.punchCue = Random(0, 1) < .5 ? cat::audio::Cue::Punch : cat::audio::Cue::PunchSecond;
}
void StartJump() {
    if (g.hidden) ToggleHidden();
    if (g.perch.transitioning || !g.physics.CurrentPose().grounded) return;
    if (g.brain) g.brain->Interrupt(GetTickCount64());
    g.prank.Clear(); g.bubbleUntil = 0; g.speed = 0;
    SetMotion(Motion::Jump, cat::animation::kJumpDurationMs);
    g.motionSoundPending = true; g.motionSoundAutomatic = false;
}
void StartGesture(Motion motion) {
    if (g.hidden) ToggleHidden();
    if (g.perch.transitioning || !g.physics.CurrentPose().grounded) return;
    if (g.brain) g.brain->Interrupt(GetTickCount64());
    g.prank.Clear(); g.bubbleUntil = 0; g.speed = 0;
    SetMotion(motion, motion == Motion::EarFlick ? cat::animation::kEarFlickDurationMs : cat::animation::kTailRaiseDurationMs);
    g.gestureSoundPending = true; g.gestureSoundAutomatic = false;
}
void SelectCharacter(cat::Character character) {
    if (!cat::IsValid(character)) return;
    if (character == g.preferences.character && !g.preferencesInvalid && !g.preferencesRecovered) return;
    const cat::Preferences selected{character, g.preferences.outfit};
    if (!SavePreferences(selected)) return;
    g.preferences = selected;
    g.preferencesRecovered = g.preferencesInvalid = false;
    if (g.brain) g.brain->Interrupt(GetTickCount64());
    g.prank.Clear(); SetMotion(Motion::Idle, 3500);
    const HICON previous = g.trayIcon;
    g.trayIcon = CreateCatIcon();
    g.tray.hIcon = g.trayIcon; g.tray.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &g.tray);
    if (previous) DestroyIcon(previous);
    UpdateTrayTip();
    Speak(cat::CharacterName(character), 2200);
    PlayCatSound(cat::audio::Cue::Greeting);
    Render();
}

void SelectOutfit(cat::Outfit outfit) {
    if (!cat::IsValid(outfit)) return;
    if (outfit == g.preferences.outfit && !g.preferencesInvalid && !g.preferencesRecovered) return;
    const cat::Preferences selected{g.preferences.character, outfit};
    if (!SavePreferences(selected)) return;
    g.preferences = selected;
    g.preferencesRecovered = g.preferencesInvalid = false;
    Speak(cat::OutfitName(outfit), 2200);
    PlayCatSound(cat::audio::Cue::Greeting);
    Render();
}

void PlaceToy(ToyKind toy) {
    if (g.hidden) ToggleHidden();
    if (g.brain) g.brain->Interrupt(GetTickCount64());
    g.toy = toy;
    if (toy != ToyKind::None) Remember(cat::memory::Event::Play);
    if (toy == ToyKind::None) {
        if (!g.perch.transitioning) SetMotion(Motion::Idle, 2600);
        Speak(L"장난감은 치워둘게 냥", 2200);
        Render();
        return;
    }
    const double offset = (toy == ToyKind::Ball ? 105.0 : 88.0) * g.dpiScale;
    const double margin = std::min(96.0 * g.dpiScale, (g.work.right - g.work.left) / 2.0);
    const double low = g.work.left + margin, high = g.work.right - margin;
    g.toyX = cat::toy::ClampCenter(g.centerX + g.direction * offset, low, high);
    g.toyBall = {g.toyX / g.dpiScale, 0};
    g.direction = g.toyX < g.centerX ? -1 : 1;
    g.facing = g.direction;
    g.nextToyAction = GetTickCount64() + 900;
    if (!g.perch.transitioning) SetMotion(Motion::Watch, 1800);
    Speak(toy == ToyKind::Ball ? L"공이다!" : L"박스다!", 1800);
    PlayCatSound(cat::audio::Cue::Greeting);
    Render();
}
bool CursorNear(POINT* position = nullptr) {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return false;
    if (position) *position = cursor;
    return MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL) == g.monitor &&
        std::abs(cursor.x - g.centerX) < 235 * g.dpiScale &&
        cursor.y >= CurrentGroundY(GetTickCount64()) - 230 * g.dpiScale &&
        cursor.y <= CurrentGroundY(GetTickCount64()) + 55 * g.dpiScale;
}
void PetCat() {
    g.prank.Clear();
    const auto ticks = GetTickCount64();
    if (g.perch.transitioning || !g.physics.CurrentPose().grounded || g.motion == Motion::Eat || g.motion == Motion::Treat) return;
    // A sleeping cat occasionally chooses its nap; interaction need not always be obedience.
    if (g.motion == Motion::Sleep && Random(0, 1) < .55) { Speak(L"…쿨쿨", 1600); return; }
    Remember(cat::memory::Event::Pet);
    if (g.brain) g.brain->Pet(ticks);
    const double sociability = g.brain ? g.brain->GetPersonality().sociability : .5;
    const double familiarity = g.memory.enabled() ? .3 + .7 * cat::memory::Summarize(g.memory.state).bond / 100.0 : 1.0;
    if (Random(0, 1) < (.15 + sociability * .6) * familiarity) {
        Speak(L"골골… ♡", 1800); SetMotion(Motion::Idle, 1800);
        PlayCatSound(cat::audio::Cue::Pet);
    } else {
        g.bubbleUntil = 0; SetMotion(Motion::Watch, 2200);
    }
}
void ShowPetStatus() {
    if (g.menuOpen || g.closing || updates.applying) return;
    SyncNutrition(UtcNow()); const auto age = cat::Age(g.pet.bornUtc, UtcNow());
    const wchar_t* stage = age.years >= 7 ? L"느긋한 어른 고양이" : age.years >= 1 ? L"어른 고양이" : L"아기 고양이";
    const wchar_t* activity = g.nutrition.bodyCondition < 35 ? L"기운이 적어 천천히 걷고 낮게 뛰어요"
        : g.nutrition.bodyCondition >= 70 ? L"몸이 무거워 천천히 걷고 낮게 뛰어요" : L"가볍게 걷고 잘 뛰어요";
    std::wostringstream text;
    text << L"함께한 지 " << age.days + 1 << L"일 · " << stage << L"\n실제 나이: " << age.years << L"년 " << age.months << L"개월\n"
         << L"처음 만난 날: " << DateText(g.pet.bornUtc) << L"\n캐릭터: " << cat::CharacterName(g.preferences.character)
         << L"\n성격: " << PersonalityLabel() << L"\n의상: " << cat::OutfitName(g.preferences.outfit) << L"\n\n"
         << L"배부름: " << static_cast<int>(std::round(g.pet.hunger)) << L" / 100\n"
         << L"몸 상태: " << cat::nutrition::BodyDescription(g.nutrition) << L"\n움직임: " << activity << L"\n"
         << L"함께 먹은 밥: " << g.pet.feedCount << L"번\n함께 먹은 간식·츄르: " << g.pet.snackCount << L"번\n\n";
    if (g.memory.enabled()) {
        SyncExperience();
        const auto memory = cat::memory::Summarize(g.memory.state);
        const wchar_t* bondName = memory.bond >= 75 ? L"오랜 단짝" : memory.bond >= 50 ? L"마음을 연 사이" :
            memory.bond >= 25 ? L"익숙한 친구" : L"천천히 알아가는 중";
        text << L"친밀도: " << bondName << L" (" << static_cast<int>(std::lround(memory.bond)) << L" / 100)\n"
             << L"기억하는 취향: " << (memory.petPreference >= .3 ? L"쓰다듬는 손이 좋아요 · " : L"")
             << (memory.treatPreference >= .3 ? L"츄르 시간이 기다려져요" : L"함께한 시간을 쌓고 있어요") << L"\n"
             << L"친밀도는 실제 돌봄으로 천천히 쌓이고, 바빠도 잊지 않아요.\n\n";
    } else text << L"친밀도·기억: 기존 파일 확인이 필요해 학습을 쉬고 있어요.\n\n";
    if (age.reachedTenYears) text << L"열 번째 생일도 함께 보냈어요. 앞으로도 계속 같이 살아요.\n";
    else text << L"열 번째 생일: " << DateText(age.tenthAnniversary) << L"\n";
    text << L"\n시간과 체형은 실제 날짜에 따라 천천히 변해요.\n알맞게 먹으면 몸 상태도 서서히 돌아와요.\n캐릭터를 바꿔도 생일과 기록은 이어지고, 바빠도 사라지지 않아요.\n\n밥: Ctrl+Alt+F    츄르: Ctrl+Alt+T\n숨기기/보이기: Ctrl+Alt+H\n고양이를 끌면 다른 모니터로 옮길 수 있어요.\n\n우클릭 메뉴에서 캐릭터·의상·장난감을 고르고 귀여운 행동을 볼 수 있어요.\n냥냥펀치의 화면 금은 잠깐 보였다 사라지는 장난이에요.";
    UpdateMessage(text.str().c_str(), kTitle, MB_OK | MB_ICONINFORMATION);
}
void ExportBackup() {
    if (g.menuOpen || g.closing || updates.applying) return;
    SavePet();
    wchar_t file[32768] = L"YeopjariCat-backup.ini";
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = g.window;
    dialog.lpstrFilter = L"고양이 성장 기록 (*.ini)\0*.ini\0모든 파일\0*.*\0"; dialog.lpstrFile = file; dialog.nMaxFile = ARRAYSIZE(file);
    dialog.lpstrDefExt = L"ini"; dialog.lpstrTitle = L"고양이의 생일과 기록을 안전한 곳에 보관하기";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    g.menuOpen = true; g.prank.Clear(); g.audio.Stop();
    const BOOL selected = GetSaveFileNameW(&dialog);
    g.menuOpen = false;
    if (selected) {
        if (_wcsicmp(file, g.savePath.c_str()) == 0 || _wcsicmp(file, g.backupPath.c_str()) == 0 ||
            _wcsicmp(file, g.nutritionPath.c_str()) == 0 || _wcsicmp(file, g.nutritionBackupPath.c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\memory.ini").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\memory.bak").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\preferences.ini").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\preferences.bak").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\sound-settings.ini").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\sound-settings.bak").c_str()) == 0) {
            UpdateMessage(L"지금 사용 중인 기록 파일 대신 다른 폴더나 파일 이름을 선택해 주세요.", kTitle, MB_OK | MB_ICONINFORMATION); return;
        }
        const std::wstring temporary = std::wstring(file) + L".tmp";
        if (WriteBytes(temporary, cat::Serialize(g.pet)) && MoveFileExW(temporary.c_str(), file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) Balloon(L"기록을 보관했어요", L"이 파일은 생일·성장 기록이에요. 친밀도·의상·소리까지 옮기려면 기록 폴더 전체를 함께 보관해 주세요.");
        else UpdateMessage(L"선택한 위치에 백업하지 못했어요. 다른 폴더를 선택해 주세요.", kTitle, MB_OK | MB_ICONERROR);
    }
}
void RunCommand(UINT command) {
    switch (command) {
    case FeedCommand: Care(false); break;
    case SnackCommand: Care(true); break;
    case StatusCommand: ShowPetStatus(); break;
    case PauseCommand: if (g.brain) g.brain->Interrupt(GetTickCount64()); g.pet.roamingPaused = !g.pet.roamingPaused; g.prank.Clear(); SetMotion(Motion::Idle, 5000); SavePet(); break;
    case HideCommand: ToggleHidden(); break;
    case BackupCommand: ExportBackup(); break;
    case FolderCommand: ShellExecuteW(g.window, L"open", g.directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case QuitCommand: PostMessageW(g.window, WM_CLOSE, 0, 0); break;
    case PunchCommand: StartPunch(true); Render(); break;
    case JumpCommand: StartJump(); Render(); break;
    case CreamCommand: SelectCharacter(cat::Character::Cream); break;
    case SilverCommand: SelectCharacter(cat::Character::SilverTabby); break;
    case OutfitNoneCommand: SelectOutfit(cat::Outfit::None); break;
    case OutfitBowCommand: SelectOutfit(cat::Outfit::BowTie); break;
    case OutfitScarfCommand: SelectOutfit(cat::Outfit::Scarf); break;
    case OutfitCrownCommand: SelectOutfit(cat::Outfit::Crown); break;
    case ToyBallCommand: PlaceToy(ToyKind::Ball); break;
    case ToyBoxCommand: PlaceToy(ToyKind::Box); break;
    case ToyClearCommand: PlaceToy(ToyKind::None); break;
    case LoafCommand: if (g.perch.transitioning || !g.physics.CurrentPose().grounded) break; if (g.brain) g.brain->Interrupt(GetTickCount64()); SetMotion(Motion::Loaf, 9000); Render(); break;
    case KneadCommand: if (g.perch.transitioning || !g.physics.CurrentPose().grounded) break; if (g.brain) g.brain->Interrupt(GetTickCount64()); SetMotion(Motion::Knead, 5200); PlayCatSound(cat::audio::Cue::Pet); Render(); break;
    case BellyCommand: if (g.perch.transitioning || !g.physics.CurrentPose().grounded) break; if (g.brain) g.brain->Interrupt(GetTickCount64()); SetMotion(Motion::BellyRoll, 4400); PlayCatSound(cat::audio::Cue::Pet); Render(); break;
    case EarCommand: StartGesture(Motion::EarFlick); Render(); break;
    case TailCommand: StartGesture(Motion::TailRaise); Render(); break;
    case UpdateCheckCommand: StartUpdateCheck(true); break;
    case UpdateViewCommand: ShowUpdateDetails(); break;
    case UpdateAutomaticCommand: ToggleAutomaticUpdates(); break;
    case SoundToggleCommand: ChangeSoundSettings(true); break;
    case SoundQuietCommand: ChangeSoundSettings(false, 25); break;
    case SoundNormalCommand: ChangeSoundSettings(false, 45); break;
    case SoundLouderCommand: ChangeSoundSettings(false, 70); break;
    case SoundPreviewCommand: PlayCatSound(cat::audio::Cue::Greeting); break;
    }
}
void ShowMenu(POINT point) {
    if (g.menuOpen || g.closing || updates.applying) return;
    HMENU menu = CreatePopupMenu();
    const auto age = cat::Age(g.pet.bornUtc, UtcNow());
    const std::wstring title = L"옆자리 고양이 · 함께 " + std::to_wstring(age.days + 1) + L"일";
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, title.c_str()); AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, FeedCommand, L"밥 주기\tCtrl+Alt+F");
    AppendMenuW(menu, MF_STRING, SnackCommand, L"츄르 주기\tCtrl+Alt+T");
    HMENU characters = CreatePopupMenu();
    AppendMenuW(characters, MF_STRING, CreamCommand, cat::CharacterName(cat::Character::Cream));
    AppendMenuW(characters, MF_STRING, SilverCommand, cat::CharacterName(cat::Character::SilverTabby));
    CheckMenuRadioItem(characters, CreamCommand, SilverCommand,
                      g.preferences.character == cat::Character::SilverTabby ? SilverCommand : CreamCommand, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(characters), L"캐릭터 선택");
    HMENU wardrobe = CreatePopupMenu();
    AppendMenuW(wardrobe, MF_STRING, OutfitNoneCommand, cat::OutfitName(cat::Outfit::None));
    AppendMenuW(wardrobe, MF_STRING, OutfitBowCommand, cat::OutfitName(cat::Outfit::BowTie));
    AppendMenuW(wardrobe, MF_STRING, OutfitScarfCommand, cat::OutfitName(cat::Outfit::Scarf));
    AppendMenuW(wardrobe, MF_STRING, OutfitCrownCommand, cat::OutfitName(cat::Outfit::Crown));
    const UINT outfitCommand = g.preferences.outfit == cat::Outfit::BowTie ? OutfitBowCommand :
        g.preferences.outfit == cat::Outfit::Scarf ? OutfitScarfCommand :
        g.preferences.outfit == cat::Outfit::Crown ? OutfitCrownCommand : OutfitNoneCommand;
    CheckMenuRadioItem(wardrobe, OutfitNoneCommand, OutfitCrownCommand, outfitCommand, MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(wardrobe), L"옷장");
    HMENU toys = CreatePopupMenu();
    AppendMenuW(toys, MF_STRING, ToyBallCommand, L"공 꺼내기");
    AppendMenuW(toys, MF_STRING, ToyBoxCommand, L"박스 꺼내기");
    AppendMenuW(toys, MF_STRING | (g.toy == ToyKind::None ? MF_GRAYED : MF_ENABLED), ToyClearCommand, L"장난감 치우기");
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(toys), L"장난감");
    HMENU actions = CreatePopupMenu();
    AppendMenuW(actions, MF_STRING, BellyCommand, L"배 까고 뒹굴기");
    AppendMenuW(actions, MF_STRING, LoafCommand, L"식빵 자세");
    AppendMenuW(actions, MF_STRING, KneadCommand, L"꾹꾹이");
    AppendMenuW(actions, MF_STRING, JumpCommand, L"점프");
    AppendMenuW(actions, MF_STRING, EarCommand, L"귀 움직이기");
    AppendMenuW(actions, MF_STRING, TailCommand, L"꼬리 세우기");
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(actions), L"귀여운 행동 보기");
    AppendMenuW(menu, MF_STRING, JumpCommand, L"점프 한 번 보기");
    AppendMenuW(menu, MF_STRING, PunchCommand, L"냥냥펀치 한 번 보기");
    HMENU soundMenu = CreatePopupMenu();
    AppendMenuW(soundMenu, MF_STRING | (g.soundSettings.enabled ? MF_CHECKED : MF_UNCHECKED), SoundToggleCommand, L"고양이 소리 켜기");
    AppendMenuW(soundMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(soundMenu, MF_STRING, SoundQuietCommand, L"작게 · 25% (기본)");
    AppendMenuW(soundMenu, MF_STRING, SoundNormalCommand, L"보통 · 45%");
    AppendMenuW(soundMenu, MF_STRING, SoundLouderCommand, L"크게 · 70%");
    const UINT volumeCommand = g.soundSettings.volume == 25 ? SoundQuietCommand
        : g.soundSettings.volume == 45 ? SoundNormalCommand
        : g.soundSettings.volume == 70 ? SoundLouderCommand : 0;
    if (volumeCommand) CheckMenuRadioItem(soundMenu, SoundQuietCommand, SoundLouderCommand, volumeCommand, MF_BYCOMMAND);
    AppendMenuW(soundMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(soundMenu, MF_STRING | ((!g.soundSettings.enabled || g.hidden) ? MF_GRAYED : MF_ENABLED), SoundPreviewCommand, L"소리 미리 듣기");
    if (g.soundSettingsRecovered) AppendMenuW(soundMenu, MF_STRING | MF_GRAYED, 0, L"이전 소리 설정에서 복구했어요");
    else if (g.soundSettingsInvalid) AppendMenuW(soundMenu, MF_STRING | MF_GRAYED, 0, L"설정을 읽지 못해 소리를 껐어요");
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(soundMenu), L"고양이 소리");
    AppendMenuW(menu, MF_STRING, StatusCommand, L"생일과 성장 기록"); AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g.pet.roamingPaused ? MF_CHECKED : MF_UNCHECKED), PauseCommand, L"이 자리에서 쉬기");
    AppendMenuW(menu, MF_STRING, HideCommand, g.hidden ? L"고양이 보이기\tCtrl+Alt+H" : L"잠깐 숨기기\tCtrl+Alt+H");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, BackupCommand, L"성장 기록 따로 백업하기…");
    AppendMenuW(menu, MF_STRING, FolderCommand, L"기록 폴더 열기");
    HMENU updateMenu = CreatePopupMenu();
    AppendMenuW(updateMenu, MF_STRING | MF_GRAYED, 0, L"현재 버전 " YEOPJARI_VERSION_WSTRING);
    const bool updateBusy = updates.operation != UpdateState::Operation::None;
    const wchar_t* checkLabel = updates.operation == UpdateState::Operation::Downloading ? L"업데이트 다운로드 중…"
        : updates.operation == UpdateState::Operation::Checking ? L"새 버전 확인 중…" : L"업데이트 확인…";
    AppendMenuW(updateMenu, MF_STRING | (updateBusy ? MF_GRAYED : MF_ENABLED), UpdateCheckCommand, checkLabel);
    if (updates.hasUpdate) {
        const auto label = L"v" + UpdateText(updates.available.versionText) + L" 변경 내용과 업데이트…";
        AppendMenuW(updateMenu, MF_STRING | (updateBusy ? MF_GRAYED : MF_ENABLED), UpdateViewCommand, label.c_str());
    }
    AppendMenuW(updateMenu, MF_STRING | (updates.automatic ? MF_CHECKED : MF_UNCHECKED) | (updates.feed.empty() ? MF_GRAYED : MF_ENABLED),
        UpdateAutomaticCommand, L"새 버전 자동 알림");
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(updateMenu), L"프로그램 업데이트");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, QuitCommand, L"오늘은 여기까지 · 종료");
    if (point.x == -1 && point.y == -1) GetCursorPos(&point);
    g.menuOpen = true; g.prank.Clear(); g.audio.Stop();
    SetForegroundWindow(g.window); // Only after an explicit context-menu request, never from a timer or care hotkey.
    UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, g.window, nullptr);
    PostMessageW(g.window, WM_NULL, 0, 0); DestroyMenu(menu); g.menuOpen = false;
    if (choice) RunCommand(choice);
}
bool MaybePlayWithToy(ULONGLONG ticks) {
    if (g.toy == ToyKind::None || g.perch.active || g.perch.transitioning || g.pet.roamingPaused ||
        g.hidden || g.menuOpen || g.dragging || ticks < g.nextToyAction || !g.physics.CurrentPose().grounded)
        return false;
    if (g.motion != Motion::Idle && g.motion != Motion::Walk && g.motion != Motion::Watch &&
        g.motion != Motion::Loaf && g.motion != Motion::Groom) return false;
    const auto [low, high] = MovementBoundsLogical();
    const double growth = cat::GrowthScale(g.pet.bornUtc, UtcNow());
    const double target = cat::toy::ApproachTarget(g.toy, g.toyX / g.dpiScale,
        g.centerX / g.dpiScale, 36 * growth, g.work.left / g.dpiScale + low, g.work.left / g.dpiScale + high);
    const double distance = target - g.centerX / g.dpiScale;
    if (g.brain) g.brain->Interrupt(ticks);
    if (std::abs(distance) > 2 || std::abs(WalkingVelocity(g.physics.CurrentPose())) > 3) {
        g.direction = distance < 0 ? -1 : 1;
        g.speed = 38 * g.dpiScale;
        SetMotion(Motion::Walk, 1000);
        g.approachingToy = true;
        return true;
    }
    if (g.toy == ToyKind::Ball) {
        g.direction = g.toyX < g.centerX ? -1 : 1; g.facing = g.direction;
        SetMotion(Motion::ToyPaw, 1050);
        g.nextToyAction = ticks + static_cast<ULONGLONG>(Random(45000, 95000));
    } else {
        // Center only after the physical approach has settled within two pixels.
        g.centerX = g.toyX; ResetMovement();
        SetMotion(Motion::BoxSit, static_cast<ULONGLONG>(Random(9000, 22000)));
        if (Random(0,1) < .42) { Speak(L"박스는 못 참지 ♡", 2600); PlayCatSound(cat::audio::Cue::Pet, true); }
        g.nextToyAction = ticks + static_cast<ULONGLONG>(Random(65000, 130000));
    }
    return true;
}

void DecideMotion() {
    if (!g.brain) { SetMotion(Motion::Idle, 10000); return; }
    const auto ticks = GetTickCount64();
    const bool wasSleeping = g.motion == Motion::Sleep;
    SYSTEMTIME local{}; GetLocalTime(&local);
    POINT cursor{}; const bool nearby = CursorNear(&cursor);
    const auto age = cat::Age(g.pet.bornUtc, UtcNow());
    SyncExperience();
    const auto plan = g.brain->Next({ticks, age.years, g.pet.hunger, g.pet.affection,
                                    static_cast<int>(local.wHour), nearby, g.pet.roamingPaused});
    Motion selected = Motion::Idle;
    using Action = cat::behavior::Action;
    switch (plan.action) {
    case Action::Idle: selected = Motion::Idle; break;
    case Action::Walk: selected = Motion::Walk; break;
    case Action::Sleep: selected = Motion::Sleep; break;
    case Action::Groom: selected = Motion::Groom; break;
    case Action::Stretch: selected = Motion::Stretch; break;
    case Action::Watch: selected = Motion::Watch; break;
    case Action::Stalk: selected = Motion::Stalk; break;
    case Action::Pounce: selected = Motion::Pounce; break;
    case Action::Zoomies: selected = Motion::Zoomies; break;
    case Action::TailChase: selected = Motion::TailChase; break;
    case Action::Punch: StartPunch(false); return;
    case Action::Jump: selected = Motion::Jump; break;
    case Action::EarFlick: selected = Motion::EarFlick; break;
    case Action::TailRaise: selected = Motion::TailRaise; break;
    case Action::BellyRoll: selected = Motion::BellyRoll; break;
    case Action::Loaf: selected = Motion::Loaf; break;
    case Action::Knead: selected = Motion::Knead; break;
    case Action::Peek: selected = Motion::Peek; break;
    case Action::Scratch: selected = Motion::Scratch; break;
    case Action::Sneeze: selected = Motion::Sneeze; break;
    }
    if ((selected == Motion::Watch || selected == Motion::Stalk || selected == Motion::Pounce) && nearby)
        g.direction = cursor.x < g.centerX ? -1 : 1;
    else if (selected == Motion::Walk || selected == Motion::Zoomies) g.direction = Random(0, 1) < .5 ? -1 : 1;
    g.speed = plan.speed * g.dpiScale;
    SetMotion(selected, plan.durationMs);
    if ((selected == Motion::Jump || selected == Motion::Pounce) && Random(0, 1) < .45) {
        g.motionSoundPending = true; g.motionSoundAutomatic = true;
    } else if (wasSleeping && selected == Motion::Stretch && Random(0, 1) < .35)
        PlayCatSound(cat::audio::Cue::Wake, true);
    if ((selected == Motion::EarFlick || selected == Motion::TailRaise) && Random(0, 1) < .34) {
        g.gestureSoundPending = true; g.gestureSoundAutomatic = true;
    }
    if ((selected == Motion::BellyRoll || selected == Motion::Knead) && Random(0, 1) < .42)
        PlayCatSound(cat::audio::Cue::Pet, true);
    if ((selected == Motion::Peek || selected == Motion::Scratch) && Random(0, 1) < .32)
        PlayCatSound(cat::audio::Cue::Gesture, true);
    if (selected == Motion::Sneeze) {
        Speak(L"에취!", 1200);
        if (Random(0, 1) < .55) PlayCatSound(cat::audio::Cue::Gesture, true);
    }
}
bool ContinueMemorySequence() {
    if (!g.memorySequenceCount) return false;
    const Motion next = g.memorySequence[0];
    auto remainder = g.memorySequence;
    const auto count = g.memorySequenceCount - 1;
    for (unsigned i = 0; i < count; ++i) remainder[i] = remainder[i + 1];
    const ULONGLONG duration = next == Motion::Sleep ? 60000 : next == Motion::Loaf ? 12000 :
        next == Motion::Knead ? 5200 : next == Motion::BellyRoll ? cat::animation::kBellyRollDurationMs : 5000;
    SetMotion(next, duration);
    g.memorySequence = remainder; g.memorySequenceCount = count;
    return true;
}
void MaybeMemoryRoutine(ULONGLONG ticks) {
    if (!g.memory.enabled() || ticks < g.nextMemoryRoutineAttempt) return;
    const auto now = UtcNow();
    cat::routine::Context context;
    context.nowUtc = now;
    SYSTEMTIME local{}; GetLocalTime(&local);
    context.localDay = cat::DaysFromCivil(local.wYear, local.wMonth, local.wDay);
    context.localHour = local.wHour;
    context.roamingPaused = g.pet.roamingPaused;
    context.busy = !g.initialized || g.hidden || g.menuOpen || g.dragging || g.closing || updates.applying ||
        g.perch.transitioning || g.approachingToy || g.approachingUser || g.memorySequenceCount ||
        g.pendingCare >= 0 || ticks < g.bubbleUntil || g.prank.Active() || !g.physics.CurrentPose().grounded ||
        (g.motion != Motion::Idle && g.motion != Motion::Watch && g.motion != Motion::Walk);
    if (context.busy) return;
    auto nutrition = g.nutrition;
    cat::nutrition::Advance(nutrition, now);
    context.canRequestFood = ticks >= g.foodRequestAfter && cat::routine::CanRequestFood(g.nutrition, now);
    context.hunger = nutrition.fullness;
    context.canRequestTreat = ticks >= g.foodRequestAfter && cat::routine::CanRequestTreat(g.nutrition, now);
    POINT cursor{};
    const bool nearby = CursorNear(&cursor);
    const auto [low, high] = MovementBoundsLogical();
    const double cursorX = (cursor.x - g.work.left) / g.dpiScale;
    const auto target = cat::memory::ApproachTarget((g.centerX - g.work.left) / g.dpiScale, cursorX, low, high);
    context.cursorReachable = nearby && target.has_value();
    const auto decision = cat::routine::Peek(g.memory.state, context, Random(0, 1));
    using Decision = cat::routine::Decision;
    if (decision == Decision::None) return;
    // Persist the accepted schedule before showing it: restart cannot replay a
    // rare event. Failure leaves both the opportunity and UI state unchanged.
    const auto prior = g.memory.state;
    if (!cat::routine::Commit(g.memory.state, context, decision)) return;
    if (!SaveMemory()) { g.memory.state = prior; g.nextMemoryRoutineAttempt = ticks + 30000; return; }
    if (decision == Decision::LunchRequest || decision == Decision::TreatRequest) {
        if (!cat::routine::AcceptRequest(g.nutrition, decision, now)) return;
        if (decision == Decision::LunchRequest) {
            Speak(L"점심시간이네…\n나도 밥 먹을래", 4200);
        } else {
            Speak(L"그때 그 츄르…\n조금만 줄래?", 4200);
        }
        SavePet(false);
        return;
    }
    if (g.brain) g.brain->Interrupt(ticks);
    switch (decision) {
    case Decision::MorningStretch: SetMotion(Motion::Stretch, 3200); break;
    case Decision::DawnZoomies:
        g.speed = 120 * g.dpiScale * std::max(.45, 1.0 - std::max(0, cat::Age(g.pet.bornUtc, now).years - 6) * .065);
        g.direction = Random(0, 1) < .5 ? -1 : 1;
        SetMotion(Motion::Zoomies, 3200);
        break;
    case Decision::Approach:
        SetMotion(Motion::Walk, 30000);
        g.userTargetX = *target; g.approachingUser = true;
        break;
    case Decision::RareKnead:
        SetMotion(Motion::Knead, 5200);
        g.memorySequence[0] = Motion::Loaf; g.memorySequenceCount = 1;
        Speak(L"네 옆이 편해졌어 ♡", 3200);
        break;
    case Decision::RareGreeting:
        SetMotion(Motion::TailRaise, cat::animation::kTailRaiseDurationMs);
        g.memorySequence[0] = Motion::BellyRoll; g.memorySequence[1] = Motion::Watch; g.memorySequenceCount = 2;
        Speak(L"오래 함께한 너니까 ♡", 3200);
        break;
    case Decision::RareBond:
        SetMotion(Motion::Stretch, 3200);
        g.memorySequence = {Motion::Knead, Motion::Loaf, Motion::Sleep}; g.memorySequenceCount = 3;
        Speak(L"앞으로도 쭉…\n네 옆에 있을게", 4200);
        break;
    default: break;
    }
}

void Tick() {
    g.audio.Poll();
    PollUpdates();
    if (g.closing) return;
    const auto ticks = GetTickCount64();
    const auto clockDelta = ticks >= g.lastTick ? ticks - g.lastTick : 0;
    g.lastTick = ticks;
    const double motionNow = MotionClock();
    double elapsed = std::max(0.0, motionNow - g.lastPhysicsTime);
    g.lastPhysicsTime = motionNow;
    const bool suspended = g.hidden || g.dragging || g.menuOpen;
    if (suspended || elapsed > .25) {
        // Freeze action time as well as the body through menus, sleep/resume,
        // and long scheduler stalls. No late takeoff or catch-up teleport.
        const auto frozen = std::min(clockDelta, ticks >= g.motionStart ? ticks - g.motionStart : 0);
        g.motionStart += frozen; g.nextDecision += frozen;
        if (g.perch.active) g.perch.leaveAfter += frozen;
        if (g.landedAt) g.landedAt += frozen;
        g.physics.Update(0, true);
        elapsed = 0;
        // A paused body resumes the same stride. Actual drag/teleport paths
        // already reset gait through ResetMovement().

    }
    g.prank.Tick(ticks);
    if (ticks >= g.nextMaintenance) {
        KeepOnWorkArea(); SyncNutrition(UtcNow()); UpdateTrayTip(); g.nextMaintenance = ticks + 1500;
        if (!g.hidden) SetWindowPos(g.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (ticks >= g.nextSave) { SavePet(); g.nextSave = ticks + 60000; }
    RefreshAnimationTimer();
    if (!suspended) {
        MaintainPerch(ticks);
        if (g.perch.transitioning) {
            UpdatePerchTransition(ticks, elapsed);
            Render();
            return;
        }
        if (!g.waitingForStop) MaybeVisitWindow(ticks);
        if (g.perch.transitioning) { UpdatePerchTransition(ticks, elapsed); Render(); return; }
        if (!g.waitingForStop && !g.approachingToy && !g.approachingUser && ticks >= g.nextDecision && g.physics.CurrentPose().grounded) {
            if (!ContinueMemorySequence() && !MaybePlayWithToy(ticks)) DecideMotion();
        }
        if (cat::motion::AdvanceStopGate(g.waitingForStop, g.physics.CurrentPose().grounded,
                WalkingVelocity(g.physics.CurrentPose()), ticks, g.motionStart, g.nextDecision) && g.stopSoundPending) {
            const auto cue = g.stopSoundCue;
            const bool automatic = g.stopSoundAutomatic;
            g.stopSoundPending = false;
            PlayCatSound(cue, automatic);
        }
        const auto actionElapsed = ticks >= g.motionStart ? ticks - g.motionStart : 0;
        const auto body = cat::nutrition::VisualModifiers(g.nutrition);
        const double growth = cat::GrowthScale(g.pet.bornUtc, UtcNow());
        const bool pounceLanded = g.motion == Motion::Pounce && g.jumpLaunched && g.physics.CurrentPose().grounded;
        double targetSpeed = cat::animation::Travels(g.motion) && !g.pet.roamingPaused && !pounceLanded ?
            g.direction * g.speed / g.dpiScale * body.speed * cat::animation::TravelFactor(g.motion, actionElapsed) : 0;
        if (g.approachingUser) {
            POINT cursor{};
            const auto [low, high] = MovementBoundsLogical();
            g.userTargetX = std::clamp(g.userTargetX, low, high);
            const double distance = g.userTargetX - (g.centerX - g.work.left) / g.dpiScale;
            const bool gone = !CursorNear(&cursor) || std::abs((cursor.x - g.work.left) / g.dpiScale - g.userTargetX) > 150;
            if (g.pet.roamingPaused || gone || ticks >= g.nextDecision ||
                (std::abs(distance) <= 2 && std::abs(WalkingVelocity(g.physics.CurrentPose())) <= 3)) {
                SetMotion(Motion::Watch, 5000); targetSpeed = 0;
            } else {
                targetSpeed = cat::toy::ApproachVelocity(distance, 18 * body.speed);
                if (targetSpeed != 0) g.direction = targetSpeed < 0 ? -1 : 1;
            }
        }
        if (g.approachingToy) {
            const auto [low, high] = MovementBoundsLogical();
            const double target = cat::toy::ApproachTarget(g.toy, g.toyX / g.dpiScale,
                g.centerX / g.dpiScale, 36 * growth, g.work.left / g.dpiScale + low, g.work.left / g.dpiScale + high);
            const double distance = target - g.centerX / g.dpiScale;
            targetSpeed = g.pet.roamingPaused ? 0 : cat::toy::ApproachVelocity(distance, 38 * body.speed);
            if (std::abs(distance) <= 2 && std::abs(WalkingVelocity(g.physics.CurrentPose())) <= 3) {
                g.approachingToy = false;
                g.nextToyAction = ticks;
                MaybePlayWithToy(ticks);
                targetSpeed = 0;
            } else if (targetSpeed != 0) g.direction = targetSpeed < 0 ? -1 : 1;
        }
        if (g.toy == ToyKind::Ball) {
            if (!g.waitingForStop && g.motion == Motion::ToyPaw && cat::toy::PawContact(ticks >= g.motionStart ? ticks - g.motionStart : 0, g.toyContact)) {
                if (std::abs(g.toyX - g.centerX) <= 55 * g.dpiScale * growth) {
                    g.toyBall.Kick(g.direction, 90);
                    if (Random(0,1) < .32) PlayCatSound(cat::audio::Cue::Gesture, true);
                }
            }
            const auto [low, high] = MovementBoundsLogical();
            // Keep the ball reachable even for a young cat at a screen edge.
            const double reach = 36 * growth;
            const auto [ballLow, ballHigh] = cat::toy::RollingBounds(
                (g.work.right - g.work.left) / g.dpiScale, low, high, reach);
            g.toyBall.Advance(elapsed, g.work.left / g.dpiScale + ballLow,
                g.work.left / g.dpiScale + ballHigh);
            g.toyX = g.toyBall.x * g.dpiScale;
        }
        if (g.perch.active && g.physics.OnPlatform() && targetSpeed != 0) {
            const auto support = g.physics.CurrentPose();
            const auto [low, high] = MovementBoundsLogical();
            if (cat::motion::BrakeBeforeEdge(support.x, WalkingVelocity(support), targetSpeed, low, high)) {
                g.direction = targetSpeed < 0 ? 1 : -1;
                targetSpeed = 0;
                if (g.motion != Motion::Pounce) SetMotion(Motion::Watch, 2400);
            }
        }
        if (g.waitingForStop) targetSpeed = 0;
        g.physics.SetTargetVelocity(targetSpeed);
        if (!g.waitingForStop && g.jumpPending) {
            const ULONGLONG takeoff = g.motion == Motion::Jump ? 220 : 240;
            if (actionElapsed >= takeoff) {
                g.jumpPending = false;
                const bool launched = g.motion == Motion::Jump ? g.physics.Jump(55 * growth * body.jump) :
                    g.physics.Pounce(g.direction, 30 * growth * body.jump, g.pet.roamingPaused ? 0 : std::abs(targetSpeed));
                if (launched) {
                    g.jumpLaunched = true; g.airborneMotion = g.motion;
                }
                if (launched && g.motionSoundPending)
                    PlayCatSound(cat::audio::Cue::Jump, g.motionSoundAutomatic);
                g.motionSoundPending = false;
            }
        }
        if (!g.waitingForStop && g.gestureSoundPending) {
            const ULONGLONG starts = g.motion == Motion::EarFlick ? 160 : 120;
            if (actionElapsed >= starts) {
                g.gestureSoundPending = false;
                if (actionElapsed < starts + 160 && g.physics.CurrentPose().grounded)
                    PlayCatSound(cat::audio::Cue::Gesture, g.gestureSoundAutomatic);
            }
        }
        const double previousX = g.physics.CurrentPose().x;
        g.physics.Update(elapsed);
        const auto physical = g.physics.CurrentPose();
        g.centerX = g.work.left + g.physics.RenderPose().x * g.dpiScale;
        if (g.perch.active && physical.grounded && !physical.onPlatform) {
            // A moved ledge can be missed during an ordinary jump. A floor
            // contact must not leave the cat constrained to that old window.
            g.perch.landingOnWindow = false;
            FinishPerchTransition(ticks);
        }
        if (std::abs(WalkingVelocity(physical)) > 2) g.facing = WalkingVelocity(physical) < 0 ? -1 : 1;
        else g.facing = g.direction;
        Motion gaitMotion = g.motion;
        if (g.motion == Motion::Walk || g.motion == Motion::Stalk || g.motion == Motion::Zoomies) g.walkingMotion = g.motion;
        else if (g.waitingForStop || ((g.motion == Motion::Idle || g.motion == Motion::Watch) && physical.grounded && std::abs(WalkingVelocity(physical)) > 2))
            gaitMotion = g.walkingMotion;
        g.gait.Advance(gaitMotion, (physical.x - previousX - physical.supportTravelX) / growth, elapsed);
        if (physical.landed) {
            g.landedAt = ticks;
            g.landingStrength = std::clamp(physical.landingSpeed / 230.0, .35, 1.0);
        }
        if (cat::animation::Travels(g.motion) && targetSpeed != 0) {
            const auto [low, high] = MovementBoundsLogical();
            const bool atLeft = targetSpeed < 0 && physical.x <= low + .3;
            const bool atRight = targetSpeed > 0 && physical.x >= high - .3;
            if (atLeft || atRight) {
                g.direction = atLeft ? 1 : -1;
                if (g.motion == Motion::Walk || g.motion == Motion::Stalk) SetMotion(Motion::Watch, 2400);
            }
        }
        if (!g.waitingForStop && g.motion == Motion::Punch) {
            const int hits = cat::animation::PunchHits(actionElapsed);
            if (hits > g.punchHits) {
                // Consume skipped contacts after a menu or scheduler stall; don't crack
                // the screen later while the cat is already sitting innocently again.
                g.punchHits = hits;
                if (cat::animation::PunchContact(actionElapsed)) {
                    const double bodyScale = g.dpiScale * cat::GrowthScale(g.pet.bornUtc, UtcNow());
                    const auto body = cat::nutrition::VisualModifiers(g.nutrition);
                    POINT paw{static_cast<LONG>(std::round(g.centerX + g.direction * 38 * bodyScale * body.bodyWidth)),
                              static_cast<LONG>(std::round(CurrentGroundY(ticks) - 74 * bodyScale * body.bodyHeight))};
                    g.prank.Trigger(paw, g.work, g.dpiScale);
                    if (hits == 1) PlayCatSound(g.punchCue, g.punchSoundAutomatic);
                }
            }
            if (actionElapsed >= 1000 && g.bubbleUntil < g.motionStart) Speak(L"…나 아니야", 1300);
        }
        // Brief, occasional curiosity. The cursor is only observed, never moved or captured.
        if (!g.waitingForStop && (g.motion == Motion::Idle || g.motion == Motion::Walk) && ticks >= g.nextCuriosity && !g.pet.roamingPaused && !g.approachingToy && !g.approachingUser && !g.memorySequenceCount) {
            POINT cursor{};
            if (CursorNear(&cursor)) {
                g.nextCuriosity = ticks + static_cast<ULONGLONG>(Random(45000, 100000));
                if (g.brain && Random(0, 1) < g.brain->GetPersonality().curiosity) {
                    g.direction = cursor.x < g.centerX ? -1 : 1;
                    SetMotion(Motion::Watch, 2600);
                }
            }
        }
        if (g.pendingCare >= 0 && physical.grounded) Care(g.pendingCare != 0);
        MaybeAnnounceUpdate(ticks);
        if (!g.waitingForStop) {
            MaybeMemoryRoutine(ticks);
            MaybeAskForFood(ticks);
        }
        Render();
    }
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g.taskbarCreated && g.initialized) { AddTray(); return 0; }
    switch (message) {
    case WM_TIMER: if (wParam == kTickTimer) Tick(); return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT paint{}; BeginPaint(window, &paint); EndPaint(window, &paint); return 0; }
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        // The visible body above the ledge remains clickable (including the
        // update menu). Only the supporting app's own rectangle passes through.
        if (g.perch.active && PtInRect(&g.perch.rect, point)) return HTTRANSPARENT;
        ScreenToClient(window, &point);
        if (g.surface.pixels && point.x >= 0 && point.y >= 0 && point.x < g.surface.width && point.y < g.surface.height) {
            const BYTE alpha = g.surface.pixels[(static_cast<size_t>(point.y) * g.surface.width + point.x) * 4 + 3];
            if (alpha > 22) return HTCLIENT;
        }
        return HTTRANSPARENT;
    }
    case WM_LBUTTONDOWN: {
        g.prank.Clear();
        g.audio.Stop();
        GetCursorPos(&g.dragStart); g.dragOffset = g.centerX - g.dragStart.x;
        if (g.brain) g.brain->Interrupt(GetTickCount64());
        g.approachingToy = g.approachingUser = false; g.memorySequenceCount = 0;
        g.dragging = true; g.moved = false; SetCapture(window); return 0;
    }
    case WM_MOUSEMOVE: if (g.dragging) {
        POINT cursor{}; GetCursorPos(&cursor);
        if (!g.moved && (std::abs(cursor.x - g.dragStart.x) > 4 || std::abs(cursor.y - g.dragStart.y) > 4)) {
            if (g.perch.active || g.perch.transitioning) CancelPerch();
            g.moved = true;
        }
        if (g.moved) {
            HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
            if (monitor != g.monitor) { g.monitor = monitor; KeepOnWorkArea(true); }
            g.centerX = cursor.x + g.dragOffset; KeepOnWorkArea(); ResetMovement(); SetMotion(Motion::Idle, 3000); Render();
        }
        return 0;
    } break;
    case WM_LBUTTONUP: if (g.dragging) {
        ReleaseCapture(); g.dragging = false;
        if (!g.moved) PetCat();
        SavePet(); Render(); return 0;
    } break;
    case WM_CAPTURECHANGED: g.dragging = false; break;
    case WM_LBUTTONDBLCLK: PetCat(); Render(); return 0;
    case WM_CONTEXTMENU: ShowMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}); return 0;
    case WM_HOTKEY:
        if (wParam == kFeedHotkey) Care(false);
        else if (wParam == kSnackHotkey) Care(true);
        else if (wParam == kHideHotkey) ToggleHidden();
        return 0;
    case kShowExisting:
        if (g.hidden) ToggleHidden();
        Speak(L"나 여기 있어 ♡", 3000); PlayCatSound(cat::audio::Cue::Greeting); Render(); return 0;
    case kTrayMessage: {
        if (g.menuOpen || g.closing || updates.applying) return 0;
        const UINT event = LOWORD(lParam);
        if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
            POINT cursor{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)};
            if (event == WM_RBUTTONUP) GetCursorPos(&cursor); ShowMenu(cursor);
        } else if (event == WM_LBUTTONDBLCLK) { if (g.hidden) ToggleHidden(); else ShowPetStatus(); }
        return 0;
    }
    case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: if (g.initialized) { g.prank.Clear(); KeepOnWorkArea(); Render(); } return 0;
    case WM_DPICHANGED: if (g.initialized) { g.prank.Clear(); KeepOnWorkArea(); Render(); } return 0;
    case WM_QUERYENDSESSION: g.audio.Stop(); SavePet(false); return TRUE;
    case WM_ENDSESSION: if (wParam) { g.audio.Shutdown(); SavePet(false); } return 0;
    case WM_CLOSE: if (!g.closing) { g.closing = true; g.audio.Shutdown(); updates.cancel.store(true); SavePet(false); DestroyWindow(window); } return 0;
    case WM_DESTROY:
        g.audio.Shutdown();
        g.prank.Shutdown();
        if (g.preciseTimer) { timeEndPeriod(1); g.preciseTimer = false; }
        KillTimer(window, kTickTimer); UnregisterHotKey(window, kFeedHotkey); UnregisterHotKey(window, kSnackHotkey); UnregisterHotKey(window, kHideHotkey);
        if (g.tray.hWnd) Shell_NotifyIconW(NIM_DELETE, &g.tray);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int NativePerchHitTest() {
    // Exercise the production Win32 procedure on a hidden popup. No message
    // loop, visible window, mouse-up/save path or application data is involved.
    if (!g.surface.pixels || g.surface.width != 420 || g.surface.height != 320) return 50;
    constexpr wchar_t testClass[] = L"YeopjariCat.Native.PerchHitSelfTest";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = WindowProc;
    wc.hInstance = g.instance; wc.lpszClassName = testClass;
    if (!RegisterClassExW(&wc)) return 51;
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        testClass, L"", WS_POPUP, 100, 100, 420, 320,
        nullptr, nullptr, g.instance, nullptr);
    if (!window) { UnregisterClassW(testClass, g.instance); return 52; }

    const int result = [&]() -> int {
        POINT origin{};
        if (!ClientToScreen(window, &origin)) return 53;
        POINT opaque{-1, -1}, transparent{-1, -1};
        for (int y = 0; y < 280; ++y) for (int x = 0; x < 420; ++x) {
            const BYTE alpha = g.surface.pixels[(static_cast<std::size_t>(y) * 420 + x) * 4 + 3];
            if (alpha >= 245 && opaque.x < 0) opaque = {x, y};
            if (alpha == 0 && transparent.x < 0) transparent = {x, y};
        }
        if (opaque.x < 0) return 54;
        if (transparent.x < 0) return 55;
        const auto hit = [&](POINT client) {
            return SendMessageW(window, WM_NCHITTEST, 0,
                MAKELPARAM(origin.x + client.x, origin.y + client.y));
        };
        g.perch.active = true; g.perch.transitioning = false;
        g.perch.hwnd = window;
        g.perch.rect = {origin.x, origin.y + 280, origin.x + 420, origin.y + 600};
        if (hit(opaque) != HTCLIENT) return 56;
        if (hit(transparent) != HTTRANSPARENT) return 57;
        // An opaque shadow/accessory over the supporting app still passes
        // through to its title bar. Restore the actual rendered pixel afterward.
        const std::size_t offset = static_cast<std::size_t>(290 * 420 + 210) * 4;
        const std::array<BYTE, 4> saved{{g.surface.pixels[offset], g.surface.pixels[offset + 1],
            g.surface.pixels[offset + 2], g.surface.pixels[offset + 3]}};
        std::fill_n(g.surface.pixels + offset, 4, static_cast<BYTE>(255));
        const auto supportHit = hit({210, 290});
        std::copy(saved.begin(), saved.end(), g.surface.pixels + offset);
        if (supportHit != HTTRANSPARENT) return 58;
        g.perch.active = false; g.perch.transitioning = true;
        if (hit(opaque) != HTCLIENT) return 59;
        g.perch.active = true; g.perch.transitioning = false;

        g.physics.Reset(500, 40, 960); g.physicsReady = true;
        g.physics.SetPlatform(250, 900, 180);
        if (!g.physics.LaunchTo(500, 180)) return 60;
        for (int i = 0; i < 180; ++i) g.physics.Update(cat::physics::kFixedStepSeconds);
        if (!g.physics.OnPlatform() || !g.physics.CurrentPose().grounded) return 61;
        const auto before = g.physics.CurrentPose();
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(opaque.x, opaque.y));
        if (!g.perch.active || !g.physics.OnPlatform() || !g.physics.CurrentPose().grounded ||
                g.physics.CurrentPose().lift != before.lift || !g.dragging || g.moved) return 62;
        return 0;
    }();
    if (GetCapture() == window) ReleaseCapture();
    g.dragging = g.moved = false;
    g.perch = {};
    g.physics.RemovePlatform(); g.physicsReady = false;
    DestroyWindow(window); // WM_DESTROY does not enter the save/close path.
    UnregisterClassW(testClass, g.instance);
    return result;
}

int RenderSelfTest() {
    // Native GDI+ regression path: resources and offscreen DIBs only. This
    // deliberately runs before LoadPet, instance windows, audio or updates.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 30;
    GdiplusStartupInput startup;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &startup, nullptr) != Ok) {
        CoUninitialize();
        return 31;
    }
    int result = 0;
    std::string context = "initialization";
    try {
        result = [&]() -> int {
            if (!LoadAtlas()) return 32;
            if (!g.surface.Create(420, 320) || !g.blendFrom.Create(420, 320) ||
                    !g.blendTo.Create(420, 320)) return 33;
            const auto birthday = cat::ToUnix({2025, 1, 1});
            g.pet = cat::NewPet(birthday);
            g.nutrition = cat::nutrition::NewState(birthday, birthday);
            g.dpiScale = 1;
            constexpr REAL center = 210, floor = 280;
            constexpr std::uint64_t fixedTicks = 500000;
            const auto draw = [&](const cat::animation::Pose& pose, REAL scale) {
                Graphics graphics(g.surface.image.get());
                graphics.SetCompositingMode(CompositingModeSourceCopy);
                graphics.Clear(Color(0, 0, 0, 0));
                graphics.SetCompositingMode(CompositingModeSourceOver);
                graphics.SetCompositingQuality(CompositingQualityHighQuality);
                graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                graphics.SetSmoothingMode(SmoothingModeAntiAlias);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                DrawAnimatedPet(graphics, pose, center + static_cast<REAL>(pose.x),
                    floor - static_cast<REAL>(pose.lift), scale);
                graphics.Flush(FlushIntentionSync);
                return graphics.GetLastStatus() == Ok;
            };
            const auto snapshot = [&] {
                return std::vector<BYTE>(g.surface.pixels,
                    g.surface.pixels + static_cast<std::size_t>(420 * 320 * 4));
            };
            constexpr std::array<cat::Outfit, 4> outfits{{cat::Outfit::None,
                cat::Outfit::BowTie, cat::Outfit::Scarf, cat::Outfit::Crown}};
            constexpr std::array<std::uint64_t, 4> samples{{0, 360, 1500, 3500}};
            for (int character = 0; character < 2; ++character) {
                g.preferences.character = character ? cat::Character::SilverTabby : cat::Character::Cream;
                const REAL scale = 110.0f / CurrentArt().base.standingHeight;
                for (int motion = 0; motion <= static_cast<int>(Motion::BoxSit); ++motion) {
                    for (const auto elapsed : samples) {
                        const auto pose = cat::animation::Evaluate(static_cast<Motion>(motion),
                            elapsed, fixedTicks, character ? -1 : 1);
                        std::vector<BYTE> body;
                        std::size_t bodyOpaque = 0;
                        for (const auto outfit : outfits) {
                            g.preferences.outfit = outfit;
                            context = "character=" + std::to_string(character) + " motion=" + std::to_string(motion) +
                                " elapsed=" + std::to_string(elapsed) + " outfit=" + std::to_string(static_cast<int>(outfit));
                            if (!draw(pose, scale)) return 34;
                            std::size_t opaque = 0, visible = 0, coveredBody = 0;
                            for (std::size_t i = 0; i < 420 * 320 * 4; i += 4) {
                                const auto* pixel = g.surface.pixels + i;
                                const unsigned alpha = pixel[3];
                                if (pixel[0] > alpha + 1 || pixel[1] > alpha + 1 || pixel[2] > alpha + 1)
                                    return 35;
                                if (alpha >= 245) ++opaque;
                                if (alpha >= 32) ++visible;
                                if (!body.empty() && body[i + 3] >= 245 && alpha >= 245) ++coveredBody;
                            }
                            // A rendered accessory alone cannot satisfy the body
                            // coverage check. None is always the first outfit.
                            if (opaque < 500 || visible < 2000) return 36;
                            if (outfit == cat::Outfit::None) {
                                body = snapshot(); bodyOpaque = opaque;
                            } else if (coveredBody * 100 < bodyOpaque * 95) return 37;
                        }
                    }
                }

                // Real flight uses the velocity-driven joint pose, not the
                // elapsed-time demonstration sequence above. Exercise its
                // exact GDI+ skin path across ascent, apex and descent.
                for (const double velocity : {720.0, 120.0, 1.0, 0.0, -1.0, -120.0, -720.0}) {
                    for (const int facing : {-1, 1}) {
                        context = "airborne character=" + std::to_string(character) + " vy=" + std::to_string(velocity) + " facing=" + std::to_string(facing);
                        const auto flight = cat::animation::AirbornePose(velocity, facing);
                        if (!draw(flight, scale)) return 45;
                        std::size_t visible = 0;
                        for (std::size_t offset = 0; offset < 420 * 320 * 4; offset += 4) {
                            const auto* pixel = g.surface.pixels + offset;
                            if (pixel[0] > pixel[3] + 1 || pixel[1] > pixel[3] + 1 || pixel[2] > pixel[3] + 1)
                                return 46;
                            if (pixel[3] >= 32) ++visible;
                        }
                        if (visible < 2000) return 47;
                    }
                }

                // Ear motion must leave the planted lower body bit-identical,
                // even when the cached native Bitmap is reused and rewarped.
                g.preferences.outfit = cat::Outfit::None;
                const auto neutral = cat::animation::Evaluate(Motion::EarFlick, 0, fixedTicks, 1);
                if (!draw(neutral, scale)) return 38;
                const auto baseline = snapshot();
                const double height = CurrentArt().base.frames[4].Height * scale;
                const int firstRow = std::clamp(static_cast<int>(std::ceil(floor - .55 * height)), 0, 319);
                for (const auto elapsed : {360ULL, 1110ULL, 0ULL}) {
                    context = "ear character=" + std::to_string(character) + " elapsed=" + std::to_string(elapsed);
                    const auto pose = cat::animation::Evaluate(Motion::EarFlick, elapsed, fixedTicks, 1);
                    if (!draw(pose, scale)) return 38;
                    std::size_t compared = 0, changed = 0;
                    for (std::size_t offset = 0; offset < baseline.size(); offset += 4)
                        if ((baseline[offset + 3] >= 32 || g.surface.pixels[offset + 3] >= 32) &&
                                std::memcmp(baseline.data() + offset, g.surface.pixels + offset, 4) != 0)
                            ++changed;
                    for (int y = firstRow; y < 320; ++y) for (int x = 0; x < 420; ++x) {
                        const auto offset = static_cast<std::size_t>(y * 420 + x) * 4;
                        // The original published PNGs have interior alpha
                        // mostly 252/253, with fewer than 30 alpha255 pixels
                        // across an entire atlas. Compare their opaque-looking
                        // body pixels exactly; do not require impossible alpha255.
                        if (baseline[offset + 3] < 245) continue;
                        ++compared;
                        if (std::memcmp(baseline.data() + offset, g.surface.pixels + offset, 4) != 0)
                            return 39;
                    }
                    if (compared < 500) { context += " compared=" + std::to_string(compared); return 40; }
                    if (elapsed && changed < 5) return 42;
                }

                // At the start of a belly roll the separately rendered head
                // and body still occupy the canonical sitting pose. Their
                // source-over composition must not cut a transparent neck.
                context = "belly composition character=" + std::to_string(character);
                cat::animation::Pose canonical;
                canonical.frame = 4; canonical.joints = {};
                if (!draw(canonical, scale)) return 38;
                const auto wholeCat = snapshot();
                const auto belly = cat::animation::Evaluate(Motion::BellyRoll, 0, fixedTicks, 1);
                if (!draw(belly, scale)) return 38;
                std::size_t visibleCompared = 0, visibleLoss = 0;
                for (std::size_t offset = 0; offset < wholeCat.size(); offset += 4) {
                    if (wholeCat[offset + 3] < 32) continue;
                    ++visibleCompared;
                    const int loss = static_cast<int>(wholeCat[offset + 3]) - g.surface.pixels[offset + 3];
                    if (loss > 12) {
                        context += " loss=" + std::to_string(loss) + " x=" + std::to_string(offset / 4 % 420) +
                            " y=" + std::to_string(offset / 4 / 420);
                        return 43;
                    }
                    if (loss > 8) ++visibleLoss;
                }
                if (visibleCompared < 2000 || visibleLoss > 12) {
                    context += " compared=" + std::to_string(visibleCompared) + " lossCount=" + std::to_string(visibleLoss);
                    return 44;
                }
            }
            context = "native perch hit testing and mouse-down support";
            return NativePerchHitTest();
        }();
    } catch (...) {
        result = 41;
    }
    WriteBytes(L"render-self-test.txt", "exit=" + std::to_string(result) + " " + context + "\n");
    ReleaseAtlases();
    g.surface.Reset(); g.blendFrom.Reset(); g.blendTo.Reset();
    GdiplusShutdown(token);
    CoUninitialize();
    const std::wstring diagnostic = L"YeopjariCat --render-self-test exit=" + std::to_wstring(result) + L"\n";
    OutputDebugStringW(diagnostic.c_str());
    return result;
}

int SelfTest() {
    const auto start = cat::ToUnix({2026, 10, 31, 0, 0, 0});
    cat::PetState original = cat::NewPet(start), parsed; std::string error;
    if (!cat::Parse(cat::Serialize(original), parsed, error) || parsed.bornUtc != start) return 10;
    cat::Feed(parsed, start + 600, false); cat::Feed(parsed, start + 601, true);
    if (parsed.feedCount != 1 || parsed.snackCount != 1 || parsed.bornUtc != start) return 11;
    const auto ninth = cat::Anniversary(start, 9);
    cat::Advance(parsed, ninth);
    if (parsed.bornUtc != start || cat::Age(start, ninth).years != 9 || cat::Age(start, ninth).reachedTenYears) return 12;
    if (!cat::Age(start, cat::Anniversary(start, 10)).reachedTenYears || parsed.hunger < 20) return 13;
    const auto leap = cat::ToUnix({2024, 2, 29});
    const auto anniversary = cat::FromUnix(cat::Anniversary(leap, 10));
    if (anniversary.year != 2034 || anniversary.month != 2 || anniversary.day != 28) return 14;
    const auto before = parsed.bornUtc;
    if (cat::Parse("broken", parsed, error) || parsed.bornUtc != before) return 15;
    return 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    if (commandLine && wcsstr(commandLine, L"--render-self-test")) {
        g.instance = instance;
        return RenderSelfTest();
    }
    if (const auto helperResult = cat::update::HandleUpdateHelperCommandLine()) return *helperResult;
    if (commandLine && wcsstr(commandLine, L"--self-test")) return SelfTest();
    g.instance = instance;
    g.instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\YeopjariCat.OneInstance.v1");
    if (!g.instanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g.instanceMutex) CloseHandle(g.instanceMutex);
        if (HWND existing = FindWindowW(kClass, nullptr)) { PostMessageW(existing, kShowExisting, 0, 0); return 0; }
        MessageBoxW(nullptr, L"고양이는 이미 곁에 있어요.\n\nCtrl+Alt+H로 보이게 하거나, 작업표시줄 오른쪽 숨겨진 아이콘에서 고양이를 찾아 주세요.", kTitle, MB_OK | MB_ICONINFORMATION); return 0;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!LoadPet()) { ReleaseMutex(g.instanceMutex); CloseHandle(g.instanceMutex); if (SUCCEEDED(com)) CoUninitialize(); return 1; }
    LoadNutrition();
    LoadMemory();
    LoadPreferences();
    LoadSoundSettings();
    InitializeUpdates();
    g.brain = std::make_unique<cat::behavior::Brain>(g.pet.bornUtc,
        static_cast<std::uint64_t>(g.random()) << 32 | g.random());
    SyncExperience();
    GdiplusStartupInput startup;
    if (GdiplusStartup(&g.gdiplusToken, &startup, nullptr) != Ok || !LoadAtlas()) {
        MessageBoxW(nullptr, L"고양이 그림을 불러오지 못했어요. 실행 파일을 다시 받아 주세요. 성장 기록은 그대로 남아 있어요.", kTitle, MB_ICONERROR);
        ReleaseAtlases();
        if (g.gdiplusToken) GdiplusShutdown(g.gdiplusToken);
        ReleaseMutex(g.instanceMutex); CloseHandle(g.instanceMutex); if (SUCCEEDED(com)) CoUninitialize(); return 2;
    }
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.style = CS_DBLCLKS; wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g.window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClass, kTitle, WS_POPUP, 0, 0, 260, 210, nullptr, nullptr, instance, nullptr);
    if (!g.window) {
        ReleaseAtlases(); GdiplusShutdown(g.gdiplusToken);
        ReleaseMutex(g.instanceMutex); CloseHandle(g.instanceMutex); if (SUCCEEDED(com)) CoUninitialize(); return 3;
    }
    g.prank.Initialize(instance);
    POINT cursor{}; GetCursorPos(&cursor); g.monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY); KeepOnWorkArea(true);
    g.trayIcon = CreateCatIcon(); AddTray();
    const UINT modifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
    if (!RegisterHotKey(g.window, kFeedHotkey, modifiers, 'F')) g.hotkeyFailures.push_back(L"Ctrl+Alt+F (밥)");
    if (!RegisterHotKey(g.window, kSnackHotkey, modifiers, 'T')) g.hotkeyFailures.push_back(L"Ctrl+Alt+T (츄르)");
    if (!RegisterHotKey(g.window, kHideHotkey, modifiers, 'H')) g.hotkeyFailures.push_back(L"Ctrl+Alt+H (숨기기)");
    g.initialized = true; g.lastTick = GetTickCount64(); g.nextMaintenance = g.lastTick + 1500; g.nextSave = g.lastTick + 60000;
    g.foodRequestAfter = g.lastTick + 180000;
    g.ambientSoundAfter = g.lastTick + 60000;
    g.nextCuriosity = g.lastTick + 20000;
    g.perch.nextTry = g.lastTick + static_cast<ULONGLONG>(Random(90000, 180000));
    SetMotion(Motion::Idle, 5000); Speak(L"안녕. 오늘도 네 옆에 ♡", 4500);
    Render(); ShowWindow(g.window, SW_SHOWNOACTIVATE); RefreshAnimationTimer();
    if (!Exists(g.directory + L"\\sound-settings.ini") && !Exists(g.directory + L"\\sound-settings.bak"))
        SaveSoundSettings(g.soundSettings, false);
    PlayCatSound(cat::audio::Cue::Greeting);
    if (!SavePet()) UpdateMessage(L"고양이 기록을 저장할 수 없어요. 기록 폴더의 권한과 디스크 공간을 확인해 주세요. 지금은 정상 저장을 보장할 수 없어요.", kTitle, MB_OK | MB_ICONWARNING);
    if (!g.nutritionNotice.empty()) {
        if (g.restoredBackup) g.nutritionNotice += L" 성장 기록도 백업에서 복구했어요.";
        if (!g.hotkeyFailures.empty()) g.nutritionNotice += L" 겹친 단축키는 우클릭 메뉴로 대신할 수 있어요.";
        Balloon(L"몸 상태 기록 안내", g.nutritionNotice, true);
    } else if (!g.hotkeyFailures.empty()) {
        std::wstring failures = L"다른 프로그램과 겹쳐 사용할 수 없는 단축키:\n";
        for (const auto& shortcut : g.hotkeyFailures) failures += shortcut + L"\n";
        failures += L"고양이나 트레이 아이콘을 우클릭하면 같은 동작을 할 수 있어요.";
        Balloon(L"단축키가 겹쳤어요", failures, true);
    } else if (g.restoredBackup) Balloon(L"이전 기록에서 안전하게 돌아왔어요", L"기본 기록을 읽지 못해 백업으로 복구했어요. 생일은 바꾸지 않았어요. 손상된 파일도 기록 폴더에 보관했어요.");
    else if (g.preferencesRecovered) Balloon(L"캐릭터 선택을 복구했어요", L"이전 선택에서 고양이를 불러왔어요. 생일과 성장 기록은 그대로예요.");
    else if (g.preferencesInvalid) Balloon(L"캐릭터를 다시 골라 주세요", L"캐릭터 선택 파일을 읽지 못해 기본 고양이로 표시했어요. 성장 기록은 그대로예요. 우클릭 메뉴에서 다시 선택할 수 있어요.", true);
    if (!g.memoryNotice.empty()) Balloon(L"친밀도 기록 안내", g.memoryNotice, !g.memory.enabled());
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    updates.Stop();
    g.audio.Shutdown();
    if (g.toyWindow) DestroyWindow(g.toyWindow);
    g.toySurface.Reset();
    g.prank.Shutdown(); g.surface.Reset(); g.blendFrom.Reset(); g.blendTo.Reset(); ReleaseAtlases();
    if (g.trayIcon) DestroyIcon(g.trayIcon); GdiplusShutdown(g.gdiplusToken);
    ReleaseMutex(g.instanceMutex); CloseHandle(g.instanceMutex); if (SUCCEEDED(com)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
