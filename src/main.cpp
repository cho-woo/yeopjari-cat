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
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <utility>
#include "app_version.h"
#include "pet_state.hpp"
#include "cat_nutrition.hpp"
#include "cat_fish.hpp"
#include "cat_behavior.hpp"
#include "cat_memory.hpp"
#include "cat_memory_motion.hpp"
#include "cat_memory_storage.hpp"
#include "cat_routine.hpp"
#include "cat_animation.hpp"
#include "cat_pose_transition.hpp"
#include "cat_care_presentation.hpp"
#include "cat_locomotion_rig.hpp"
#include "cat_articulated_walk.hpp"
#include "cat_motion_gate.hpp"
#include "cat_physics.hpp"
#include "cat_environment.hpp"
#include "cat_cursor_attack.hpp"
#include "cat_cursor_animation.hpp"
#include "cat_toy.hpp"
#include "cat_ball_physics.hpp"
#include "cat_render_geometry.hpp"
#include "cat_render_placement.hpp"
#include "cat_belly_rig.hpp"
#include "cat_species_belly.hpp"
#include "cat_recline_transition.hpp"
#include "cat_hanging_rig.hpp"
#include "cat_preferences.hpp"
#include "cat_species.hpp"
#include <mmsystem.h> // Millisecond animation timing only; no audio playback.
#include "screen_prank.hpp"
#include "update_client.hpp"
#include "update_swap.hpp"
#include "cat_update_notice.hpp"
#include "cat_interaction_settings.hpp"
#include "cat_life_storage.hpp"
#include "cat_species.hpp"
#include "cat_companion.hpp"
#include "care_sprites.generated.hpp"

using namespace Gdiplus;
void PausePrimaryCarePresentation(std::uint64_t milliseconds);
void ResetPrimaryCarePresentation();
void ReleaseCarePresentation();
constexpr wchar_t kClass[] = L"YeopjariCat.Native.v1";
constexpr wchar_t kTitle[] = L"옆자리 고양이";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowExisting = WM_APP + 2;
constexpr UINT_PTR kTickTimer = 1;
constexpr int kFeedHotkey = 1, kSnackHotkey = 2, kHideHotkey = 3;
constexpr UINT kCharacterCommandBase = 2500, kOutfitCommandBase = 2520;
constexpr UINT kWorkInterferenceCommand = 2300, kSeasonalEventsCommand = 2301;
constexpr UINT kFishCommand = 2302;
// Keep the original IDs: retired action, toy and sound IDs are never reused.
enum Command {
    FeedCommand = 1010, SnackCommand = 1011, StatusCommand = 1012,
    PauseCommand = 1013, HideCommand = 1014, BackupCommand = 1015,
    FolderCommand = 1016, QuitCommand = 1017,
    CreamCommand = 1020, SilverCommand = 1021,
    UpdateCheckCommand = 1024, UpdateViewCommand = 1025, UpdateAutomaticCommand = 1026,
    OutfitNoneCommand = 1032, OutfitBowCommand = 1033,
    OutfitScarfCommand = 1034, OutfitCrownCommand = 1035
};
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
    std::array<Rect, 32> frames{};
    int standingHeight = 1;
    struct Skin {
        int width = 0, height = 0, groundInset = 0;
        std::vector<std::uint32_t> source, deformed, body, head, expression;
        std::unique_ptr<Bitmap> sourceImage, image, bodyImage, headImage, expressionImage;
        std::array<int, 11> controls{};
        bool valid = false, partsValid = false;
    };
    std::array<Skin, 32> skins;
    void Reset() {
        for (auto& skin : skins) {
            skin.sourceImage.reset(); skin.image.reset(); skin.bodyImage.reset(); skin.headImage.reset(); skin.expressionImage.reset();
            skin.source.clear(); skin.deformed.clear(); skin.body.clear(); skin.head.clear(); skin.expression.clear(); skin.valid = skin.partsValid = false;
        }
        image.reset();
        if (stream) { stream->Release(); stream = nullptr; }
        standingHeight = 1;
    }
};
struct CareSpriteAtlas {
    const cat::sprite_assets::Descriptor* layout=nullptr;
    std::array<std::vector<std::uint32_t>,cat::sprite_assets::kMaxFrames> framePixels;
    std::array<std::unique_ptr<Bitmap>,cat::sprite_assets::kMaxFrames> frames;
    IStream* stream=nullptr;
    std::unique_ptr<Bitmap> image;
    ~CareSpriteAtlas() { image.reset();if(stream)stream->Release(); }
};
struct WalkingSkin {
    cat::walk::Rig rig;
    std::unique_ptr<Bitmap> image;
    double phase = -1, stride = 0, width = 0, height = 0, duty = 0, lift = 0;
    cat::gait::Feet feet{};
    bool feetValid=false;
};
struct JointWalkingSkin {
    cat::articulated::Rig rig;
    std::unique_ptr<Bitmap> image;
    cat::gait::Feet feet{};
    double width=0,height=0;
    bool valid=false;
    cat::walk_dynamics::Frame dynamics{};
};
struct CharacterArt {
    SpriteAtlas base, actions;
    std::unique_ptr<WalkingSkin> walking;
    std::unique_ptr<JointWalkingSkin> jointWalking;
};

struct WindowPerch {
    HWND hwnd = nullptr;
    RECT rect{};
    HWND sourceHwnd = nullptr;
    RECT sourceRect{};
    bool active = false;
    bool transitioning = false;
    bool landingOnWindow = false;
    bool preparing = false;
    bool startled = false;
    bool sawUnreachableLedge = false;
    bool walkAfterLanding = false;
    ULONGLONG nextTry = 0, leaveAfter = 0, nextLookDown = 0, nextBalance = 0, observedAt = 0;
    double targetX = 0.0;
};

using ToyKind = cat::toy::Kind;

struct App {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HANDLE instanceMutex = nullptr;
    ULONG_PTR gdiplusToken = 0;
    std::array<CharacterArt, cat::kCharacterCount> characters;
    Surface styleBlendFrom, styleBlendTo;
    std::vector<std::unique_ptr<CareSpriteAtlas>> careSprites;
    SpriteAtlas authoredBoxProp;
    SpriteAtlas jumpTreat;
    SpriteAtlas gestures;
    SpriteAtlas recline;
    SpriteAtlas hanging, companionBack, sleepStyles;
    std::array<int, 2> extraStandingHeight{1, 1};
    std::array<int, 2> gestureStandingHeight{1, 1};
    cat::Preferences preferences;
    cat::interaction::Settings interactionSettings;
    bool workInterference = false;
    bool newlyCreatedPet = false;
    bool preferencesRecovered = false, preferencesInvalid = false;
    ToyKind toy = ToyKind::None;
    double toyX = 0.0;
    bool approachingToy = false, toyContact = false;
    int pendingCare = -1;
    bool fishMeal = false; // Transient presentation; fish remains a meal in every saved record.
    cat::sprite_sequence::ProgramCueCursor punchCues;
    bool wakingFromSleep = false;
    unsigned sleepVariant = 0, groomVariant = 0;
    bool authoredBoxVisible = false;
    cat::toy::PhysicsBall toyBall;
    cat::toy::Lure toyLure;
    cat::toy::Session toySession;
    cat::toy::Automatic automaticToy;
    bool toyEngaged = false, toyDispatching = false, toyRewardPending = false;
    Motion toyMotion = Motion::Watch;
    unsigned toyAppliedRevision = 0;
    HWND toyWindow = nullptr;
    Surface toySurface;
    ULONGLONG nextToyAction = 0;
    ULONGLONG updateAnnounceAfter = 0;
    HICON trayIcon = nullptr;
    NOTIFYICONDATAW tray{};
    UINT taskbarCreated = 0;
    Surface surface;
    Surface blendFrom, blendTo;
    cat::ScreenPrank prank;
    WindowPerch perch;
    cat::PetState pet;
    cat::memory::storage::Store memory;
    cat::life::storage::Store life;
    bool lifeSaveErrorShown = false;
    std::int64_t lifeInteractionUntil = 0;
    ULONGLONG nextLifeEventAttempt = 0;
    std::array<Motion, 3> lifeSequence{};
    unsigned lifeSequenceCount = 0;
    bool smallBoxEvent = false, lifeDispatching = false;
    cat::life::ScreenCrossing lifeCrossing;
    ULONGLONG lifeHangUntil = 0;
    cat::companion::Store companionStore;
    cat::companion::Model companionModel;
    cat::companion::Window companionWindow;
    bool companionSaveErrorShown = false;
    RECT companionWork{};
    double companionDpi = 0;
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
    cat::animation::PoseTransition poseTransition;
    bool physicsReady = false, jumpPending = false, jumpLaunched = false;
    bool waitingForStop = false;
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
    std::uint64_t motionSerial = 0; // Transient ownership; clocks can be rebased during pauses.
    bool autonomousResting = false, autonomousDispatching = false, companionReaction = false;
    ULONGLONG autonomousTick = 0, autonomousClaimedAt = std::numeric_limits<ULONGLONG>::max();
    int direction = 1, facing = 1;
    double speed = 24.0;
    int punchHits = 0;
    ULONGLONG lastTick = 0, nextDecision = 0, nextMaintenance = 0, nextSave = 0, motionStart = 0, bubbleUntil = 0, nextCuriosity = 0;
    ULONGLONG foodRequestAfter = 0, nextCareReply = 0;
    std::wstring bubble;
    std::vector<std::wstring> hotkeyFailures;
    std::mt19937 random{std::random_device{}()};
} g;

bool WorkInterferenceEnabled() { return g.workInterference; }

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
bool WorkInterferenceEnabled();
bool EnvironmentBusy();
bool ApplyCursorAttackPose(cat::animation::Pose& pose, ULONGLONG ticks, const cat::physics::Pose& physical);
void ObserveLife(ULONGLONG ticks);
bool MaybeLifeEvent(ULONGLONG ticks, bool decisionBoundary = false);
bool ContinueLifeSequence();
void UpdateLifeEvent(ULONGLONG ticks, double elapsed);

std::size_t CharacterIndex(cat::Character character = g.preferences.character) {
    return cat::IsValid(character) ? static_cast<std::size_t>(character) : 0U;
}
CharacterArt& CurrentArt(cat::Character character = g.preferences.character) { return g.characters[CharacterIndex(character)]; }

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
    double bond = 0;
    if (!g.memory.enabled()) g.brain->SetExperience({});
    else {
        cat::memory::Advance(g.memory.state, UtcNow());
        const auto p = cat::memory::Summarize(g.memory.state); bond = p.bond;
        g.brain->SetExperience({p.bond, p.petPreference, p.treatPreference,
            p.sociabilityShift, p.energyShift, p.curiosityShift, p.mischiefShift});
    }
    const auto p = g.life.enabled() ? cat::life::Summarize(g.life.state, bond) : cat::life::Profile{};
    const auto species = cat::species::ProfileFor(g.preferences.character);
    g.brain->SetDisposition(p.activityScale * species.movementSpeedMultiplier, p.restScale,
        p.socialScale, p.curiosityScale * species.cursorInterest, p.foodScale * species.foodInterest);
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
void LoadLife() {
    MemoryIO io; MemoryTransaction transaction;
    if (transaction.held) g.life.Load(io, g.pet, UtcNow(), g.newlyCreatedPet,
        (static_cast<std::uint64_t>(g.random()) << 32) | g.random());
    if(g.life.outcome==cat::life::storage::Outcome::Migrated && g.memory.enabled() &&
            g.memory.state.bornUtc==g.life.state.bornUtc && g.memory.state.lastRareUtc)
        cat::life::InheritRareCooldown(g.life.state,g.memory.state.lastRareUtc);
    if (!g.life.enabled()) g.memoryNotice += L" 기분·계절 기록을 읽지 못해 원본을 보존하고 해당 학습을 쉬어요.";
}
bool SaveLife(bool reportError = true) {
    if (!g.life.enabled()) return true;
    MemoryIO io; MemoryTransaction transaction;
    if (transaction.held && g.life.Save(io)) { g.lifeSaveErrorShown = false; return true; }
    if (reportError && !g.lifeSaveErrorShown)
        Balloon(L"생활 기록을 저장하지 못했어요", L"이전 기분·기념일 기록을 보존했어요. 기록 폴더의 권한과 공간을 확인해 주세요.", true);
    g.lifeSaveErrorShown = true; return false;
}
void LoadCompanion() {
    MemoryIO io; MemoryTransaction transaction;
    if (transaction.held) g.companionStore.Load(io, UtcNow());
    if (g.companionStore.available()) g.companionModel.Bind(g.companionStore.state);
    else if (g.companionStore.outcome == cat::companion::Outcome::Blocked)
        g.memoryNotice += L" 두 번째 동물의 기록을 읽지 못해 원본을 보존했어요.";
}
bool SaveCompanion(bool reportError = true) {
    if (!g.companionStore.available()) return true;
    MemoryIO io; MemoryTransaction transaction;
    if (transaction.held && g.companionStore.Save(io)) { g.companionSaveErrorShown = false; return true; }
    if (reportError && !g.companionSaveErrorShown)
        Balloon(L"두 번째 동물의 기록을 저장하지 못했어요", L"기존 기록을 보존했어요. 기록 폴더의 권한과 공간을 확인해 주세요.", true);
    g.companionSaveErrorShown = true; return false;
}
void Remember(cat::memory::Event event) {
    g.lifeInteractionUntil = UtcNow() + 30;
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
    const bool lifeSaved = SaveLife(reportError);
    const bool companionSaved = SaveCompanion(reportError);
    return nutritionSaved && memorySaved && lifeSaved && companionSaved;
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
    const int choice = MessageBoxW(g.window, text, title, flags & ~MB_ICONMASK);
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
    g.menuOpen = true; g.prank.Clear();
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
        MessageBoxW(nullptr, L"윈도우 개인 저장 폴더를 찾을 수 없어요. 기록을 잃지 않도록 실행을 멈춥니다.", kTitle, 0); return false;
    }
    g.directory = std::wstring(local) + L"\\YeopjariCat";
    if (!CreateDirectoryW(g.directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"고양이 기록 폴더를 만들 수 없어요. 폴더 접근 권한과 디스크 공간을 확인해 주세요.", kTitle, 0); return false;
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
        MessageBoxW(nullptr, message.c_str(), kTitle, MB_OK | 0); return false;
    }
    const auto now = UtcNow();
    if (now < 946684800LL || now > 32503680000LL) {
        MessageBoxW(nullptr, L"컴퓨터의 날짜를 먼저 확인해 주세요. 정확한 생일을 기록한 다음 함께 살기 시작할게요.", kTitle, 0); return false;
    }
    g.pet = cat::NewPet(now); g.newlyCreatedPet = true; return true;
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

void LoadInteractionSettings() {
    std::string bytes;
    cat::interaction::Settings saved;
    const auto path = g.directory + L"\\interaction-settings.ini";
    if (ReadBytes(path, bytes) && cat::interaction::Parse(bytes, saved)) g.interactionSettings = saved;
    else if (ReadBytes(path + L".bak", bytes) && cat::interaction::Parse(bytes, saved)) g.interactionSettings = saved;
    g.workInterference = g.interactionSettings.workInterference;
}
void ChangeInteractionSettings(bool changeInterference) {
    const auto path = g.directory + L"\\interaction-settings.ini";
    const auto temp = path + L".tmp", backup = path + L".bak", backupTemp = backup + L".tmp";
    bool ownsTransaction=false;
    auto fail = [&]() {
        if (ownsTransaction) { DeleteFileW(temp.c_str()); DeleteFileW(backupTemp.c_str()); }
        UpdateMessage(L"놀이 설정을 저장하지 못했어요. 기존 설정을 유지할게요.", kTitle, MB_OK | MB_ICONWARNING);
    };
    // The same AppData folder can be open in another Windows session. Merge
    // this menu's explicit change into its latest verified settings under the
    // cross-session lock, preserving the other option changed elsewhere.
    MemoryTransaction transaction;
    if (!transaction.held) { fail(); return; }
    ownsTransaction=true;
    MemoryIO io;
    const bool hadMain=io.Exists("interaction-settings.ini");
    bool hadBackup=io.Exists("interaction-settings.ini.bak");
    std::string previous, previousBackup;
    if ((hadMain && !ReadBytes(path,previous)) || (hadBackup && !ReadBytes(backup,previousBackup))) { fail(); return; }
    auto unchanged=[&]() {
        std::string current;
        return io.Exists("interaction-settings.ini")==hadMain &&
            (!hadMain || (ReadBytes(path,current) && current==previous)) &&
            io.Exists("interaction-settings.ini.bak")==hadBackup &&
            (!hadBackup || (ReadBytes(backup,current) && current==previousBackup));
    };
    auto candidate=g.interactionSettings;
    cat::interaction::Settings parsed;
    const bool mainValid=hadMain && cat::interaction::Parse(previous,parsed);
    if (mainValid) candidate=parsed;
    else if (hadBackup && cat::interaction::Parse(previousBackup,parsed)) candidate=parsed;
    if (changeInterference) candidate.workInterference=!g.interactionSettings.workInterference;
    else candidate.seasonalEvents=!g.interactionSettings.seasonalEvents;
    const auto verifiedWrite = [&](const std::wstring& destination, const std::string& bytes) {
        std::string readback; cat::interaction::Settings checked;
        return WriteBytes(destination,bytes) && ReadBytes(destination,readback) &&
            readback==bytes && cat::interaction::Parse(readback,checked);
    };
    if (hadMain) {
        if (mainValid) {
            if (hadBackup && !cat::interaction::Parse(previousBackup,parsed) &&
                !io.Preserve("interaction-settings.ini.bak")) { fail(); return; }
            if (!verifiedWrite(backupTemp, previous) || !unchanged() ||
                !MoveFileExW(backupTemp.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { fail(); return; }
            hadBackup=true; previousBackup=previous;
        } else if (!io.Preserve("interaction-settings.ini")) { fail(); return; }
    }
    if (!verifiedWrite(temp, cat::interaction::Serialize(candidate)) || !unchanged() ||
        !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { fail(); return; }
    g.interactionSettings = candidate;
    g.workInterference = candidate.workInterference;
}

bool LoadAtlasResource(int resourceId, SpriteAtlas& sheet, int columns = 4, int frameCount = 8, int sheetRow = -1) {
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
    for (int row = 0; row <= rows; ++row) rowCuts[row] = sheetRow < 0 ? row * height / rows : (sheetRow + row) * height / 8;
    if (sheetRow >= 0) {
        // Generated contact sheets have uneven transparent row gutters. Find
        // the nearest empty scanline so no paws/ears leak into another pet.
        for (int edge=0; edge<=1; ++edge) {
            const int split=sheetRow+edge;
            if(split==0){rowCuts[edge]=0;continue;}
            if(split==8){rowCuts[edge]=height;continue;}
            const int target=split*height/8,radius=height/32;
            int best=target,fewest=width+1;
            for(int y=std::max(0,target-radius);y<std::min(height,target+radius+1);++y){
                const auto* row=static_cast<const BYTE*>(data.Scan0)+static_cast<ptrdiff_t>(y)*data.Stride;
                int occupied=0;for(int x=0;x<width;++x)if(row[x*4+3]>16)++occupied;
                if(occupied<fewest||(occupied==fewest&&std::abs(y-target)<std::abs(best-target))){best=y;fewest=occupied;}
            }
            rowCuts[edge]=best;
        }
    }
    std::vector<int> columnCuts(columns+1);
    for(int col=0;col<=columns;++col)columnCuts[col]=col*width/columns;
    if(sheetRow>=0) for(int split=1;split<columns;++split){
        const int target=split*width/columns,radius=width/columns/5;
        int best=target,fewest=height+1;
        for(int x=std::max(columnCuts[split-1]+1,target-radius);x<std::min(width,target+radius+1);++x){
            int occupied=0;for(int y=rowCuts[0];y<rowCuts[1];++y){const auto* row=static_cast<const BYTE*>(data.Scan0)+static_cast<ptrdiff_t>(y)*data.Stride;if(row[x*4+3]>16)++occupied;}
            if(occupied<fewest||(occupied==fewest&&std::abs(x-target)<std::abs(best-target))){best=x;fewest=occupied;}
        }
        columnCuts[split]=best;
    }
    // Portrait sheets have deliberately generous, sometimes uneven gutters.
    // Locate the nearest transparent scanline so a tall ear cannot leak into
    // the sprite above it. Original artwork bytes remain unchanged.
    if (columns == 2 || resourceId == 112) for (int split = 1; split < rows; ++split) {
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
        const int x0 = columnCuts[col], x1 = columnCuts[col+1];
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
        const bool needsSkin = resourceId == 112 || resourceId == 111 || resourceId == 110 || resourceId == 109 || resourceId == 107 ||
            (resourceId == 105 && (index == 0 || index == 2 || index == 4 || index == 6)) ||
            ((resourceId == 101 || resourceId == 103) && (index == 1 || index >= 4)) ||
            ((resourceId == 102 || resourceId == 104) && (index == 0 || index == 1 || index == 2 || index == 7));
        const bool originalWalk = (resourceId == 101 || resourceId == 103) && index < 4;
        if (!needsSkin && !originalWalk) continue;
        const auto& source = frames[index];
        auto& skin = sheet.skins[index];
        skin.width = source.Width; skin.height = source.Height;
        skin.source.resize(static_cast<std::size_t>(skin.width) * skin.height);
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
        if (originalWalk) {
            // The two-pixel crop padding and faint fur fringe are not feet.
            // Register the last solid sole row, without rescaling each pose.
            int sole = skin.height - 1;
            for (; sole > 0; --sole) {
                const auto begin = skin.source.begin() + static_cast<std::size_t>(sole) * skin.width;
                if (std::any_of(begin, begin + skin.width, [](std::uint32_t pixel) {
                    return (pixel >> 24) > 153;
                })) break;
            }
            skin.groundInset = skin.height - 1 - sole;
            skin.sourceImage = std::make_unique<Bitmap>(skin.width, skin.height, skin.width * 4,
                PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(skin.source.data()));
            if (skin.sourceImage->GetLastStatus() != Ok) { atlas->UnlockBits(&data); return false; }
        }
        if (!needsSkin) continue;
        skin.deformed.resize(skin.source.size());
        skin.body.resize(skin.source.size()); skin.head.resize(skin.source.size()); skin.expression.resize(skin.source.size());
        skin.expressionImage = std::make_unique<Bitmap>(skin.width, skin.height, skin.width * 4,
            PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(skin.expression.data()));
        skin.image = std::make_unique<Bitmap>(skin.width, skin.height, skin.width * 4,
            PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(skin.deformed.data()));
        skin.bodyImage = std::make_unique<Bitmap>(skin.width, skin.height, skin.width * 4,
            PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(skin.body.data()));
        skin.headImage = std::make_unique<Bitmap>(skin.width, skin.height, skin.width * 4,
            PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(skin.head.data()));
    }
    atlas->UnlockBits(&data); return true;
}
struct CareSpriteMemoryStats {
    std::size_t decodedAtlases=0,residentPixels=0,residentFrames=0;
};
CareSpriteMemoryStats careSpriteMemory;

bool PrepareCareSpriteFrames(CareSpriteAtlas& sprite,Bitmap& atlas,std::size_t& residentPixels) {
    if(!sprite.layout || !cat::sprite_assets::Valid(*sprite.layout))return false;
    const auto& layout=*sprite.layout;
    if(atlas.GetWidth()!=layout.imageWidth || atlas.GetHeight()!=layout.imageHeight)return false;
    constexpr std::size_t budget=cat::sprite_assets::kMaxResidentPixels;
    std::size_t needed=0;
    for(unsigned index=0;index<layout.frameCount;++index) {
        const auto& r=layout.frames[index].rect;
        const auto pixels=static_cast<std::size_t>(r.width)*r.height;
        if(pixels>budget-needed)return false;
        needed+=pixels;
    }
    if(residentPixels>budget || needed>budget-residentPixels)return false;
    Rect whole(0,0,layout.imageWidth,layout.imageHeight);BitmapData data{};
    if(atlas.LockBits(&whole,ImageLockModeRead,PixelFormat32bppPARGB,&data)!=Ok)return false;
    struct Unlock {Bitmap& image;BitmapData& data;~Unlock(){image.UnlockBits(&data);}} unlock{atlas,data};
    CareSpriteAtlas prepared;prepared.layout=&layout;
    try {
        for(unsigned frame=0;frame<layout.frameCount;++frame) {
            const auto& r=layout.frames[frame].rect;
            auto& pixels=prepared.framePixels[frame];pixels.resize(static_cast<std::size_t>(r.width)*r.height);
            for(int y=0;y<r.height;++y) {
                const auto* row=static_cast<const BYTE*>(data.Scan0)+static_cast<ptrdiff_t>(r.y+y)*data.Stride+r.x*4;
                auto* target=reinterpret_cast<BYTE*>(pixels.data()+static_cast<std::size_t>(y)*r.width);
                std::memcpy(target,row,r.width*4);
                // Ignore only transparent background noise, never guess a color key.
                for(int x=0;x<r.width;++x)if(target[x*4+3]<=16)std::memset(target+x*4,0,4);
            }
            prepared.frames[frame]=std::make_unique<Bitmap>(r.width,r.height,r.width*4,
                PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(pixels.data()));
            if(prepared.frames[frame]->GetLastStatus()!=Ok)return false;
        }
    } catch(const std::bad_alloc&) {return false;}
    // Commit only complete frames. Each bitmap owns no pixels; vectors outlive it.
    sprite.frames=std::move(prepared.frames);
    sprite.framePixels=std::move(prepared.framePixels);
    residentPixels+=needed;
    return true;
}

bool LoadCareSprites() {
    g.careSprites.clear();careSpriteMemory={};
    std::vector<int> decoded;
    for(const auto& layout:cat::sprite_assets::kCareSprites) {
        if(!cat::sprite_assets::Valid(layout))return false;
        if(std::find(decoded.begin(),decoded.end(),layout.resourceId)!=decoded.end())continue;
        const auto resource=FindResourceW(g.instance,MAKEINTRESOURCEW(layout.resourceId),RT_RCDATA);
        if(!resource)return false;
        const auto loaded=LoadResource(g.instance,resource);
        const auto count=SizeofResource(g.instance,resource);
        const auto* bytes=static_cast<const BYTE*>(LockResource(loaded));
        if(!bytes || count<33 || count>cat::sprite_assets::kMaxImageBytes ||
                std::memcmp(bytes,"\x89PNG\r\n\x1a\n",8)!=0 || std::memcmp(bytes+12,"IHDR",4)!=0)return false;
        const auto big=[](const BYTE* p){return static_cast<std::uint32_t>(p[0])<<24 |
            static_cast<std::uint32_t>(p[1])<<16 | static_cast<std::uint32_t>(p[2])<<8 | p[3];};
        const auto width=big(bytes+16),height=big(bytes+20);
        if(width!=layout.imageWidth || height!=layout.imageHeight || width>cat::sprite_assets::kMaxSide || height>cat::sprite_assets::kMaxSide ||
                static_cast<std::uint64_t>(width)*height>cat::sprite_assets::kMaxImagePixels || bytes[24]!=8 || bytes[25]!=6)return false;
        const auto memory=GlobalAlloc(GMEM_MOVEABLE,count);
        if(!memory)return false;
        const auto target=GlobalLock(memory);
        if(!target){GlobalFree(memory);return false;}
        std::memcpy(target,bytes,count);GlobalUnlock(memory);
        CareSpriteAtlas decodedAtlas;
        if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&decodedAtlas.stream))){GlobalFree(memory);return false;}
        decodedAtlas.image.reset(Bitmap::FromStream(decodedAtlas.stream,FALSE));
        if(!decodedAtlas.image || decodedAtlas.image->GetLastStatus()!=Ok)return false;
        for(const auto& member:cat::sprite_assets::kCareSprites)if(member.resourceId==layout.resourceId) {
            auto sprite=std::make_unique<CareSpriteAtlas>();sprite->layout=&member;
            if(!PrepareCareSpriteFrames(*sprite,*decodedAtlas.image,careSpriteMemory.residentPixels))return false;
            careSpriteMemory.residentFrames+=member.frameCount;
            g.careSprites.push_back(std::move(sprite));
        }
        decoded.push_back(layout.resourceId);++careSpriteMemory.decodedAtlases;
        // Decoded PNG and stream die here. Only isolated, prewarmed frames remain.
    }
    return true;
}

bool LoadAtlas() {
    if(!LoadCareSprites())return false;
    // Prepare the empty prop once from the supplied box drawing. Its outline
    // overlaps the seated cat, so a rectangular crop alone would retain fur.
    // Source PNG bytes remain unchanged; this is a cached rendering layer.
    bool finalBox=false;
    for(const auto& sprite:g.careSprites)if(sprite->layout->clip==cat::sprite_sequence::Clip::Box &&
            sprite->layout->character==static_cast<unsigned>(cat::Character::Cream) && sprite->frames[0]) {
        auto image=std::make_unique<Bitmap>(228,136,PixelFormat32bppPARGB);
        if(image->GetLastStatus()!=Ok)return false;
        {
            Graphics graphics(image.get());graphics.Clear(Color(0,0,0,0));
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            const PointF outline[]{{25,19},{97,0},{197,8},{226,34},{201,46},{201,103},
                {131,135},{25,118},{23,77},{0,87},{20,50},{21,29}};
            GraphicsPath mask;mask.AddPolygon(outline,static_cast<int>(std::size(outline)));
            graphics.SetClip(&mask);
            graphics.DrawImage(sprite->frames[0].get(),Rect(0,0,228,136),204,268,228,136,UnitPixel);
            graphics.Flush(FlushIntentionSync);
        }
        g.authoredBoxProp.Reset();g.authoredBoxProp.image=std::move(image);
        g.authoredBoxProp.frames[0]=Rect(0,0,228,136);finalBox=true;break;
    }
    if(!finalBox && !LoadAtlasResource(193,g.authoredBoxProp,1,1))return false;
    for (int i = 0; i < 2; ++i) {
        auto& art = g.characters[i];
        if (!LoadAtlasResource(101 + i * 2, art.base) || !LoadAtlasResource(102 + i * 2, art.actions)) return false;
        // The relaxed sitting pose is the size reference across a character's sheets.
        art.actions.standingHeight = std::max(1, art.actions.frames[6].Height);
    }
    for (int i = 2; i < cat::kCharacterCount; ++i) {
        if (!LoadAtlasResource(109, g.characters[i].base, 8, 8, i-2)) return false;
        g.characters[i].base.standingHeight = g.characters[i].base.frames[0].Height;
    }
    if (!LoadAtlasResource(105, g.jumpTreat, 2) || !LoadAtlasResource(106, g.gestures, 2) ||
        !LoadAtlasResource(107, g.recline, 2, 4) ||
        !LoadAtlasResource(110, g.hanging, 5, 10) || !LoadAtlasResource(111, g.companionBack, 4, 8) ||
        !LoadAtlasResource(112, g.sleepStyles, 4, 20)) return false;
    for (int i = 0; i < 2; ++i) {
        g.extraStandingHeight[i] = std::max(g.jumpTreat.frames[i * 4 + 2].Height, g.jumpTreat.frames[i * 4 + 3].Height);
        g.gestureStandingHeight[i] = std::max(g.gestures.frames[i * 4].Height, g.gestures.frames[i * 4 + 1].Height);
    }
    return true;
}
void ReleaseAtlases() {
    ReleaseCarePresentation();
    g.careSprites.clear();careSpriteMemory={};g.authoredBoxProp.Reset();
    g.styleBlendFrom.Reset(); g.styleBlendTo.Reset();
    for (auto& art : g.characters) { art.jointWalking.reset(); art.walking.reset(); art.base.Reset(); art.actions.Reset(); }
    g.jumpTreat.Reset(); g.gestures.Reset(); g.recline.Reset(); g.hanging.Reset(); g.companionBack.Reset(); g.sleepStyles.Reset();
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
    g.poseTransition.Reset();
    ResetPrimaryCarePresentation();
    g.jumpPending = g.jumpLaunched = false;
    g.waitingForStop = false;
    g.landedAt = 0;
    g.facing = g.direction;
    g.lastPhysicsTime = MotionClock();
}
double WalkingVelocity(const cat::physics::Pose& pose) {
    return pose.velocityX - pose.supportVelocityX;
}

const CareSpriteAtlas* LoadedCareSprite(cat::sprite_sequence::Clip clip,cat::Character character) {
    for(const auto& sprite:g.careSprites)
        if(sprite->layout->clip==clip && sprite->layout->character==static_cast<unsigned>(character))return sprite.get();
    return nullptr;
}
void ConfigurePrimaryAuthoredStride() {
    using Clip=cat::sprite_sequence::Clip;
    const auto* walk=LoadedCareSprite(Clip::Walk,g.preferences.character);
    const auto* run=LoadedCareSprite(Clip::Run,g.preferences.character);
    if(walk && run) {
        const auto body=cat::nutrition::VisualModifiers(g.nutrition);
        g.gait.ConfigureAuthoredStrides(walk->layout->stridePixels*body.bodyWidth,
            run->layout->stridePixels*body.bodyWidth);
    }
}
ULONGLONG PrimaryTakeoffMs(bool pounce=false) {
    if(const auto* jump=LoadedCareSprite(cat::sprite_sequence::Clip::Jump,g.preferences.character);
            jump && jump->layout->program.stepCount)
        return cat::sprite_sequence::Duration(jump->layout->program,jump->layout->program.intro);
    return pounce?240:220;
}

void RefreshAnimationTimer() {
    if (!g.window) return;
    bool companionResting = true;
    if (g.companionStore.enabled() && g.companionModel.Bound()) {
        const auto second = g.companionModel.View(GetTickCount64());
        companionResting = (second.motion == Motion::Sleep || second.motion == Motion::Idle || second.motion == Motion::Watch) && second.physical.grounded &&
            std::abs(second.physical.velocityX) < .5;
    }
    const bool toysResting = (g.toy != ToyKind::Ball ||
        (std::abs(g.toyBall.velocity) < .5 && std::abs(g.toyBall.verticalVelocity) < .5 && g.toyBall.lift < .2)) &&
        !(g.toy == ToyKind::FishingRod && g.toyEngaged);
    const bool resting = (g.motion == Motion::Sleep || g.motion == Motion::Loaf || g.motion == Motion::BoxSit ||
        g.motion == Motion::Idle || g.motion == Motion::Watch) &&
        companionResting && toysResting &&
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
    ++g.motionSerial;
    using Clip=cat::sprite_sequence::Clip;
    const auto clip=motion==Motion::Groom?Clip::Groom:motion==Motion::Eat?Clip::Eat:
        motion==Motion::Treat?Clip::Treat:motion==Motion::Punch?Clip::Punch:Clip::None;
    if(const auto* asset=LoadedCareSprite(clip,g.preferences.character);asset && asset->layout->program.stepCount)
        duration=std::max<ULONGLONG>(duration,cat::sprite_sequence::ProgramDuration(asset->layout->program));
    if(motion==Motion::Jump || motion==Motion::Pounce) {
        if(const auto* jump=LoadedCareSprite(Clip::Jump,g.preferences.character);jump && jump->layout->program.stepCount)
            duration=std::max<ULONGLONG>(duration,
                cat::sprite_sequence::Duration(jump->layout->program,jump->layout->program.intro)+
                cat::sprite_sequence::Duration(jump->layout->program,jump->layout->program.outro)+1500);
    }
    g.wakingFromSleep=g.motion==Motion::Sleep && motion==Motion::Stretch;
    if(g.wakingFromSleep) {
        if(const auto* sleep=LoadedCareSprite(Clip::Sleep,g.preferences.character)) {
            const auto wake=sleep->layout->program.stepCount?
                cat::sprite_sequence::Duration(sleep->layout->program,sleep->layout->program.outro):
                cat::sprite_sequence::kSleepWakeMs;
            duration=std::max<ULONGLONG>(duration,wake+500);
        }
    }
    g.autonomousResting=false;g.companionReaction=false;
    if(g.autonomousDispatching)g.autonomousClaimedAt=g.autonomousTick;
    if(motion==Motion::Sleep)g.sleepVariant=LoadedCareSprite(Clip::Sleep,g.preferences.character)?0:
        cat::sprite_sequence::SelectSleepVariant(Random(0,1),false);
    if(motion==Motion::Groom)g.groomVariant=static_cast<unsigned>(g.random()%3);
    g.fishMeal = false;
    if (!g.lifeDispatching) {
        g.lifeSequenceCount = 0; g.smallBoxEvent = false; g.lifeCrossing = {};
    }
    if (motion != Motion::Hang && g.physics.Hanging()) {
        g.physics.EndHang(false); g.lifeHangUntil = 0;
    }
    if (motion == Motion::Peek) duration = cat::animation::kPeekDurationMs;
    if (motion == Motion::BellyRoll) duration = cat::animation::kBellyRollDurationMs;
    if (g.motion == Motion::Walk || g.motion == Motion::Stalk || g.motion == Motion::Zoomies)
        g.walkingMotion = g.motion;
    g.waitingForStop = g.physicsReady && cat::motion::NeedsStop(motion,
        g.physics.CurrentPose().grounded, WalkingVelocity(g.physics.CurrentPose()), g.perch.transitioning);
    const auto ticks = GetTickCount64(); g.motion = motion; g.motionStart = ticks; g.nextDecision = ticks + duration;
    if (!g.toyDispatching && g.toyEngaged) {
        g.toyEngaged = false;
        g.toySession.Start(g.toy);
        // Resume discovering after the explicit action has had its full time.
        g.nextToyAction = ticks + duration + 1500;
    }
    g.punchHits = 0;
    g.punchCues.Reset();
    g.jumpPending = motion == Motion::Jump || motion == Motion::Pounce;
    g.jumpLaunched = false;
    g.approachingToy = false; g.toyContact = false;
    g.approachingUser = false; g.memorySequenceCount = 0;
    RefreshAnimationTimer();
}

// Only a completed quiet interval opens a new unrelated autonomous story.
// Explicit care, an existing story's next phase and physical safety use their
// own paths; no random owner may replace a half-played authored action.
bool PrimaryAutonomousBoundary(ULONGLONG ticks) {
    return g.autonomousResting && ticks>=g.nextDecision && g.autonomousClaimedAt!=ticks &&
        g.physicsReady && g.physics.CurrentPose().grounded && !g.waitingForStop &&
        !g.perch.transitioning && !g.jumpPending && !g.lifeSequenceCount && !g.memorySequenceCount &&
        !g.lifeCrossing.Active() && !g.physics.Hanging() && !EnvironmentBusy() &&
        !g.toyEngaged && !g.approachingToy && !g.approachingUser && g.pendingCare<0;
}
bool CanStartAutonomousStory(ULONGLONG ticks) {
    return PrimaryAutonomousBoundary(ticks) && !g.companionModel.Busy();
}
bool PrimarySocialBoundary(ULONGLONG ticks) {
    // Two independent random clocks need an overlap, not one matching frame.
    // A social proposal may use the latter part of an already quiet interval.
    return g.autonomousResting && ticks>=g.motionStart &&
        ticks-g.motionStart>=cat::behavior::Brain::NeutralRestMinMs && !g.waitingForStop &&
        !g.lifeSequenceCount && !g.memorySequenceCount && !g.toyEngaged &&
        !g.approachingToy && !g.approachingUser && g.pendingCare<0;
}
void BeginAutonomousRest() {
    SetMotion(Motion::Idle,cat::behavior::Brain::NeutralRestDuration(g.random()));
    g.autonomousResting=true;g.speed=0;
}
struct PerchCandidate { HWND hwnd = nullptr; RECT rect{}; double score = 0.0; };

double PerchJumpScale() {
    return cat::species::ProfileFor(g.preferences.character).jumpHeightMultiplier *
        cat::nutrition::VisualModifiers(g.nutrition).jump;
}
double PerchClearance() { return 45 * PerchJumpScale(); }
double PerchRiseLimit() {
    return cat::environment::WindowJumpRiseLimit((g.work.bottom - g.work.top) / g.dpiScale, PerchJumpScale());
}

bool PlanPerchLanding(const RECT& rect, double& target) {
    const double left = (std::max(rect.left, g.work.left) - g.work.left) / g.dpiScale;
    const double right = (std::min(rect.right, g.work.right) - g.work.left) / g.dpiScale;
    const double height = (g.work.bottom - rect.top) / g.dpiScale;
    if (height - g.physics.CurrentPose().lift > PerchRiseLimit())
        return false;
    return cat::environment::ChooseLanding(g.physics.CurrentPose().x, left, right,
        [&](double x) { return g.physics.CanLaunchToPlatform(left, right, height, x, PerchClearance()); }, target);
}

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
    if (hwnd == g.perch.hwnd) return TRUE;
    RECT rect{};
    if (!QueryPerchRect(hwnd, rect)) return TRUE;
    double targetX = 0;
    if (!PlanPerchLanding(rect, targetX)) {
        if ((g.work.bottom - rect.top) / g.dpiScale - g.physics.CurrentPose().lift > PerchRiseLimit())
            g.perch.sawUnreachableLedge = true;
        return TRUE;
    }
    const double horizontal = std::abs(targetX - g.physics.CurrentPose().x);
    const double vertical = std::abs((g.work.bottom - rect.top) / g.dpiScale - g.physics.CurrentPose().lift);
    // Prefer a nearby window, with a small bias toward lower/easier ledges.
    list->push_back({hwnd, rect, horizontal + vertical * .18});
    return TRUE;
}

bool FindPerch(PerchCandidate& choice) {
    std::vector<PerchCandidate> candidates;
    g.perch.sawUnreachableLedge = false;
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
    g.perch.sourceHwnd = nullptr; g.perch.sourceRect = {};
    g.perch.active = g.perch.transitioning = g.perch.preparing = false;
    g.perch.landingOnWindow = false;
    g.perch.startled = false;
    g.perch.walkAfterLanding = false;
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
        double target = 0;
        if (!PlanPerchLanding(rect, target)) {
            g.perch.nextTry = ticks + 60000;
            return;
        }
        // Retain the real source ledge while the cat brakes and crouches.
        // The destination collider is installed atomically at takeoff.
        g.perch.sourceHwnd = g.perch.active ? g.perch.hwnd : nullptr;
        g.perch.sourceRect = g.perch.active ? g.perch.rect : RECT{};
        g.perch.targetX = target;
        g.perch.hwnd = hwnd;
        g.perch.rect = rect;
        g.perch.startled = false;
        g.direction = target < g.physics.CurrentPose().x ? -1 : 1;
    } else {
        // Closing/removing a support never restarts the trajectory or invents
        // an upward hop. Gravity acts on the current body and velocity.
        g.physics.RemovePlatform();
        g.perch.hwnd = nullptr;
        g.perch.sourceHwnd = nullptr; g.perch.sourceRect = {};
        g.perch.rect = {};
    }
    if (g.brain) g.brain->Interrupt(ticks);
    g.perch.active = false;
    g.perch.walkAfterLanding = false;
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
    g.perch.sourceHwnd = nullptr; g.perch.sourceRect = {};
    g.perch.active = landedOnWindow;
    g.perch.landingOnWindow = landedOnWindow;
    g.perch.walkAfterLanding = landedOnWindow;
    if (landedOnWindow) {
        g.perch.leaveAfter = ticks + static_cast<ULONGLONG>(Random(22000, 52000));
        g.perch.nextTry = ticks + static_cast<ULONGLONG>(Random(18000, 35000));
        g.perch.nextLookDown = ticks + 7000; g.perch.observedAt = ticks;
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
    SetMotion(g.perch.startled ? Motion::Startled : Motion::Watch,
        static_cast<ULONGLONG>(landedOnWindow ? 1000 : Random(1700, 3000)));
    g.perch.startled = false;
}

void AbortPlannedPerchTransfer(ULONGLONG ticks) {
    RECT source{};
    const bool supported = g.perch.sourceHwnd && QueryPerchRect(g.perch.sourceHwnd, source);
    g.perch.hwnd = supported ? g.perch.sourceHwnd : nullptr;
    g.perch.rect = supported ? source : RECT{};
    g.perch.active = supported;
    g.perch.sourceHwnd = nullptr; g.perch.sourceRect = {};
    if (supported) SetPerchCollider(source);
    else g.physics.RemovePlatform();
    g.perch.landingOnWindow = supported;
    g.perch.preparing = false;
    g.perch.transitioning = !supported && !g.physics.CurrentPose().grounded;
    g.perch.nextTry = ticks + 30000;
    g.waitingForStop = g.jumpPending = g.jumpLaunched = false;
    g.physics.SetTargetVelocity(0);
    if (!g.perch.transitioning && g.motion == Motion::Jump) SetMotion(Motion::Watch, 1800);
}

void UpdatePerchTransition(ULONGLONG ticks, double elapsed) {
    if (!g.perch.transitioning) return;
    if (g.perch.preparing && (g.pet.roamingPaused || g.motion != Motion::Jump)) {
        // A canceled preparation leaves its original ledge intact. Only a
        // closed source removes support; an airborne transfer keeps momentum.
        AbortPlannedPerchTransfer(ticks);
        return;
    }
    if (g.perch.preparing && g.perch.sourceHwnd) {
        RECT source{};
        if (!QueryPerchRect(g.perch.sourceHwnd, source)) {
            g.perch.startled = true;
            AbortPlannedPerchTransfer(ticks);
            return;
        }
        SetPerchCollider(source);
        g.perch.sourceRect = source;
    }
    if (g.perch.landingOnWindow) {
        RECT current{};
        if (!QueryPerchRect(g.perch.hwnd, current)) {
            if (g.perch.preparing) { AbortPlannedPerchTransfer(ticks); return; }
            g.physics.RemovePlatform();
            g.perch.landingOnWindow = g.perch.preparing = false;
            g.perch.startled = true;
            g.perch.hwnd = nullptr; g.perch.rect = {};
        } else {
            // Before takeoff a moved target can change the planned impulse.
            // Once airborne only the ledge moves; the cat keeps its momentum.
            if (g.perch.preparing) {
                if (!PlanPerchLanding(current, g.perch.targetX)) { AbortPlannedPerchTransfer(ticks); return; }
            } else SetPerchCollider(current);
            g.perch.rect = current;
        }
    }
    if (g.perch.preparing) {
        g.physics.SetTargetVelocity(0);
        const auto physical = g.physics.CurrentPose();
        g.waitingForStop = !physical.grounded || std::abs(WalkingVelocity(physical)) > 2;
        if (g.waitingForStop) g.motionStart = ticks;
        else if (ticks >= g.motionStart + PrimaryTakeoffMs()) {
            if (g.physics.LaunchToPlatform(
                    (std::max(g.perch.rect.left, g.work.left) - g.work.left) / g.dpiScale,
                    (std::min(g.perch.rect.right, g.work.right) - g.work.left) / g.dpiScale,
                    (g.work.bottom - g.perch.rect.top) / g.dpiScale, g.perch.targetX, PerchClearance())) {
                g.jumpLaunched = true;
                g.perch.sourceHwnd = nullptr; g.perch.sourceRect = {};
            } else {
                AbortPlannedPerchTransfer(ticks);
                return;
            }
            g.perch.preparing = false;
        }
    }
    const auto previousRendered = g.physics.RenderPose();
    g.physics.Update(elapsed);
    const auto physical = g.physics.CurrentPose();
    const auto rendered = g.physics.RenderPose();
    g.centerX = g.work.left + rendered.x * g.dpiScale;
    if (std::abs(WalkingVelocity(physical)) > 2) g.facing = WalkingVelocity(physical) < 0 ? -1 : 1;
    if (g.waitingForStop) {
        ConfigurePrimaryAuthoredStride();
        g.gait.Advance(g.walkingMotion, (rendered.relativeTravelX-previousRendered.relativeTravelX) / cat::GrowthScale(g.pet.bornUtc, UtcNow()), elapsed, rendered.grounded);
    }
    if (!g.perch.preparing && physical.grounded) FinishPerchTransition(ticks);
}

void MaintainPerch(ULONGLONG ticks) {
    if (!g.perch.active || g.perch.transitioning) return;
    RECT current{};
    if (!QueryPerchRect(g.perch.hwnd, current)) {
        g.perch.startled = true;
        BeginPerchTransition(nullptr, {}, false);
        return;
    }
    const double seconds = g.perch.observedAt && ticks > g.perch.observedAt ? (ticks - g.perch.observedAt) / 1000.0 : 0;
    const double strength = seconds > 0 && seconds <= .25 ? cat::environment::BalanceStrength(
        (current.left - g.perch.rect.left) / g.dpiScale / seconds,
        (current.top - g.perch.rect.top) / g.dpiScale / seconds) : 0;
    SetPerchCollider(current);
    g.perch.rect = current;
    g.perch.observedAt = ticks;
    if (g.physics.Hanging()) return; // Keep its real support tracked; do not start another action.
    if (g.perch.walkAfterLanding && (g.motion != Motion::Watch || g.pet.roamingPaused))
        g.perch.walkAfterLanding = false;
    if (g.perch.walkAfterLanding && !g.waitingForStop && !EnvironmentBusy() &&
            !g.lifeSequenceCount && !g.lifeCrossing.Active() && !g.toyEngaged &&
            g.physics.OnPlatform() && ticks >= g.nextDecision) {
        g.perch.walkAfterLanding = false;
        const auto bounds = MovementBoundsLogical();
        g.direction = g.physics.CurrentPose().x < (bounds.first + bounds.second)*.5 ? 1 : -1;
        const bool run = bounds.second - bounds.first >= 250 && Random(0, 1) < .35;
        g.speed = (run ? 90 : 42) * g.dpiScale;
        SetMotion(run ? Motion::Zoomies : Motion::Walk, run ? 2100 : 4200);
    }
    const bool available = !EnvironmentBusy() && !g.lifeSequenceCount && !g.lifeCrossing.Active() &&
        !g.toyEngaged && !g.waitingForStop && g.physics.OnPlatform() &&
        (g.motion == Motion::Idle || g.motion == Motion::Watch || g.motion == Motion::Walk || g.motion == Motion::Balance);
    if (available && strength > .08 && ticks >= g.perch.nextBalance) {
        SetMotion(Motion::Balance, 1100);
        g.perch.nextBalance = ticks + 1500;
    } else if (available && CanStartAutonomousStory(ticks) && ticks >= g.perch.nextLookDown && strength < .02 &&
            std::abs(WalkingVelocity(g.physics.CurrentPose())) < 2) {
        SetMotion(Motion::LookDown, 1900);
        g.perch.nextLookDown = ticks + static_cast<ULONGLONG>(Random(18000, 35000));
    }
    if (CanStartAutonomousStory(ticks) && !g.pet.roamingPaused && !g.waitingForStop && ticks >= g.perch.leaveAfter && g.physics.CurrentPose().grounded &&
        !EnvironmentBusy() && !g.lifeSequenceCount && !g.lifeCrossing.Active() && !g.toyEngaged &&
        (g.motion == Motion::Idle || g.motion == Motion::Walk || g.motion == Motion::Watch || g.motion == Motion::Groom)) {
        PerchCandidate next;
        if (FindPerch(next)) BeginPerchTransition(next.hwnd, next.rect, true);
        else BeginPerchTransition(nullptr, {}, false);
    }
}

void MaybeVisitWindow(ULONGLONG ticks) {
    if(!CanStartAutonomousStory(ticks))return;
    if (EnvironmentBusy() || g.lifeSequenceCount || g.lifeCrossing.Active() || g.physics.Hanging() ||
        g.perch.transitioning || g.toyEngaged || g.approachingToy || g.approachingUser || g.memorySequenceCount || g.pet.roamingPaused || ticks < g.perch.nextTry ||
        ticks < g.bubbleUntil || g.menuOpen || g.dragging || !g.physics.CurrentPose().grounded) return;
    if (g.motion != Motion::Idle && g.motion != Motion::Walk && g.motion != Motion::Watch && g.motion != Motion::Groom) return;
    PerchCandidate candidate;
    if (!FindPerch(candidate)) {
        g.perch.nextTry = ticks + static_cast<ULONGLONG>(Random(30000, 70000));
        if (g.preferences.character == cat::Character::Munchkin && g.perch.sawUnreachableLedge && Random(0, 1) < .3)
            SetMotion(Motion::LookDown, 1700); // Assess an unreachable ledge without a fake hop.
        return;
    }
    // It should feel like a rare bit of cat logic, not a constant window tour.
    if (Random(0, 1) > .58) {
        g.perch.nextTry = ticks + static_cast<ULONGLONG>(Random(30000, 70000));
        return;
    }
    BeginPerchTransition(candidate.hwnd, candidate.rect, true);
}

void RoundedRect(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL radius) {
    const REAL d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90); path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90); path.AddArc(x, y + h - d, d, d, 90, 90); path.CloseFigure();
}
bool PrepareSkin(SpriteAtlas::Skin& skin, int frame, cat::deform::Joints& joints, cat::deform::Shape shape, bool parts = false,
                 cat::Character character = g.preferences.character) {
    std::array<double*, 11> values{{&joints.leftEar, &joints.rightEar, &joints.headNod,
        &joints.frontLeft, &joints.frontRight, &joints.tailRaise, &joints.breath, &joints.crouch,
        &joints.pawReachX, &joints.pawReachY, &joints.pawAnchorX}};
    std::array<int, 11> controls{};
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
            const double coverage = cat::belly::HeadCoverage(character,frame,
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
             cat::deform::Joints joints = {}, bool groundContact = true, bool bodyOnly = false,
             cat::Character character = g.preferences.character, cat::Outfit /*outfit*/ = g.preferences.outfit,
             cat::expression::Face face = {}) {
    if (frame < 0 || frame >= cat::animation::kSpriteCount) return;
    auto& art = CurrentArt(character);
    SpriteAtlas* atlas = &art.base;
    int cell = frame;
    const bool newArt = cat::species::UsesNewArt(character);
    if (frame >= 27) {
        atlas=&g.sleepStyles;cell=static_cast<int>(CharacterIndex(character))*2+frame-27;
        scale *= static_cast<REAL>(art.base.standingHeight)/300.0f;
    } else if (frame == 26) {
        atlas=&g.hanging; cell=static_cast<int>(CharacterIndex(character));
        scale *= static_cast<REAL>(art.base.standingHeight) / atlas->frames[cell].Height;
        scaleX *= static_cast<REAL>(cat::hanging::BodyWidthScale(character));
    } else if (newArt && frame == 25) {
        atlas=&g.companionBack; cell=cat::species::ArtCell(character);
        scale *= static_cast<REAL>(art.base.standingHeight) / 250.0f;
    } else if (newArt) {
        const int originalFrame = frame;
        cell = frame < 4 ? 1 + frame % 2 : frame == 6 ? 3 : frame == 7 || frame == 18 || frame == 19 ? 4 :
            frame == 9 || frame == 10 ? 5 : frame == 16 || frame == 17 ? 6 : frame >= 24 || frame == 8 ? 7 : 0;
        frame = cell == 0 ? 4 : cell == 3 ? 6 : cell == 4 ? 7 : cell == 5 ? 9 : cell == 6 ? 16 : cell == 7 ? 24 : 0;
        if (originalFrame == 13) joints.frontRight = 1;
    } else if (frame >= 24) {
        atlas = &g.recline;
        cell = static_cast<int>(CharacterIndex(character)) * 2 + frame - 24;
        // A common head-size reference for all four reclining poses. Scaling
        // each pose to standing height would make the lying cat much larger.
        scale *= static_cast<REAL>(art.base.standingHeight) / 440.0f;
    } else if (frame >= 20) {
        atlas = &g.gestures;
        cell = static_cast<int>(CharacterIndex(character)) * 4 + frame - 20;
        scale *= static_cast<REAL>(art.base.standingHeight) / g.gestureStandingHeight[CharacterIndex(character)];
    } else if (frame >= 16) {
        atlas = &g.jumpTreat;
        cell = static_cast<int>(CharacterIndex(character)) * 4 + frame - 16;
        scale *= static_cast<REAL>(art.base.standingHeight) / g.extraStandingHeight[CharacterIndex(character)];
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
        frame == 8 ? cat::deform::Shape::Stretch :
        frame == 15 ? cat::deform::Shape::TailChase :
        frame == 7 ? cat::deform::Shape::Eating :
        frame == 9 || frame == 10 ? cat::deform::Shape::Grooming :
        frame == 18 ? cat::deform::Shape::Treat :
        frame == 25 ? cat::deform::Shape::ReclineBack :
        frame == 24 || frame == 6 || frame >= 27 ? cat::deform::Shape::ReclineSide : cat::deform::Shape::Sitting;
    const bool articulated = newArt || frame == 4 || frame == 6 || frame == 7 || frame == 8 || frame == 9 || frame == 10 || frame == 15 || frame == 16 || frame == 18 || frame >= 24;
    if (articulated) {
        auto& skin = atlas->skins[cell];
        if (PrepareSkin(skin, frame, joints, shape, bodyOnly,character)) {
            Bitmap* image = bodyOnly ? skin.bodyImage.get() : skin.image.get();
            if (frame == 4 && !bodyOnly && !cat::expression::Zero(face)) {
                const auto landmarks=cat::species::FaceFor(character);
                const auto& closed=art.base.skins[5];
                if (cat::expression::Apply(skin.deformed.data(), skin.width, skin.height,
                        skin.expression.data(), face, landmarks, joints,{closed.source.data(),closed.width,closed.height,cat::species::ClosedFaceFor(character)})) image = skin.expressionImage.get();
            } else if ((frame==24 || frame==25) && !bodyOnly && face.blink>0) {
                const auto& closed=art.base.skins[5];
                if(cat::recline::CloseEyes(skin.deformed.data(),skin.width,skin.height,skin.expression.data(),
                    character,frame,face.blink,{closed.source.data(),closed.width,closed.height,cat::species::ClosedFaceFor(character)}))
                    image=skin.expressionImage.get();
            }
            graphics.DrawImage(image, destination, 0, 0, static_cast<REAL>(skin.width), static_cast<REAL>(skin.height), UnitPixel, &attrs);
        }
    } else {
        graphics.DrawImage(atlas->image.get(), destination, static_cast<REAL>(source.X), static_cast<REAL>(source.Y), static_cast<REAL>(source.Width), static_cast<REAL>(source.Height), UnitPixel, &attrs);
        joints = {};
    }
    graphics.Restore(saved);
}

void DrawBellyHead(Graphics& graphics, const cat::animation::Pose& pose, REAL center, REAL bottom, REAL scale,
                   cat::Character character, cat::Outfit /*outfit*/, const cat::nutrition::Visual& body) {
    const bool newArt=cat::species::UsesNewArt(character);
    auto& art = CurrentArt(character);
    auto dimensions = [&](int frame) {
        if(frame==26){const auto& source=g.hanging.frames[CharacterIndex(character)];const REAL factor=scale*art.base.standingHeight/source.Height;
            return std::pair<double,double>{source.Width*factor*body.bodyWidth*cat::hanging::BodyWidthScale(character),source.Height*factor*body.bodyHeight};}
        const auto& source = newArt ? (frame==4 ? art.base.frames[0] : frame==24 ? art.base.frames[7] : g.companionBack.frames[cat::species::ArtCell(character)]) :
            (frame == 4 ? art.base.frames[4] : g.recline.frames[static_cast<int>(CharacterIndex(character)) * 2 + frame - 24]);
        const REAL factor = newArt ? (frame==25 ? scale*art.base.standingHeight/250.0f : scale) : frame == 4 ? scale : scale * art.base.standingHeight / 440.0f;
        return std::pair<double, double>{source.Width * factor * body.bodyWidth,
            source.Height * factor * body.bodyHeight};
    };
    const int second = pose.blendFrame < 0 ? pose.frame : pose.blendFrame;
    const auto a = dimensions(pose.frame), b = dimensions(second), headSize = dimensions(4);
    auto bone = cat::belly::BlendBone(character,pose.frame, a.first, a.second,
        second, b.first, b.second, pose.blendFrame < 0 ? 0 : pose.blend);
    const auto headAnchor=cat::belly::HeadAnchor(character);
    const double headU=headAnchor.x,headV=headAnchor.y;
    if(pose.frame==26){
        // Hanging stretches the torso below the grip, never the face. The
        // head retains precisely the neutral pixel dimensions while its neck
        // meets the hanging body's unchanged paw/shoulder line.
        bone=cat::hanging::BoneFor(character,a.second,headSize.second);
    }
    auto joints = pose.joints;
    joints.frontLeft = joints.frontRight = joints.tailRaise = joints.breath = joints.crouch = 0;
    auto& skin = art.base.skins[newArt?0:4];
    if (!PrepareSkin(skin, 4, joints, cat::deform::Shape::Sitting, true,character)) return;
    Bitmap* headImage = skin.headImage.get();
    if (pose.frame==26 || !cat::expression::Zero(pose.face)) {
        const auto& closed=art.base.skins[5];
        if (cat::expression::Apply(skin.deformed.data(), skin.width, skin.height, skin.expression.data(), pose.face, cat::species::FaceFor(character), joints,{closed.source.data(),closed.width,closed.height,cat::species::ClosedFaceFor(character)})) {
            for (int y=0; y<skin.height; ++y) for (int x=0; x<skin.width; ++x) {
                const auto i=static_cast<std::size_t>(y)*skin.width+x;
                const cat::deform::Point uv{(x+.5)/skin.width,(y+.5)/skin.height};
                const double coverage=pose.frame==26?cat::hanging::HeadCoverage(character,uv):cat::belly::HeadCoverage(character,4,uv);
                skin.expression[i]=cat::belly::MaskPixel(skin.expression[i],coverage,true);
            }
            headImage=skin.expressionImage.get();
        }
    }
    const auto saved = graphics.Save();
    graphics.TranslateTransform(center, bottom);
    graphics.ScaleTransform(pose.flip ? -1.0f : 1.0f, 1.0f);
    graphics.TranslateTransform(static_cast<REAL>(bone.x), static_cast<REAL>(bone.y));
    graphics.RotateTransform(static_cast<REAL>(bone.angle));
    const REAL width = static_cast<REAL>(headSize.first), height = static_cast<REAL>(headSize.second);
    ImageAttributes attrs; attrs.SetWrapMode(WrapModeTileFlipXY);
    graphics.DrawImage(headImage, RectF(static_cast<REAL>(-headU*width),static_cast<REAL>(-headV*height),width,height),
        0, 0, static_cast<REAL>(skin.width), static_cast<REAL>(skin.height), UnitPixel, &attrs);
    graphics.TranslateTransform(static_cast<REAL>((.5-headU)*width),static_cast<REAL>((1-headV)*height));
    graphics.Restore(saved);
}
bool DrawOriginalWalkingPet(Graphics& graphics,const cat::animation::Pose& pose,
                           REAL center,REAL bottom,REAL scale,cat::Character character,
                           const cat::nutrition::Visual& body) {
    if (cat::species::UsesNewArt(character) || pose.frame < 0 || pose.frame >= 4) return false;
    const auto& source=CurrentArt(character).base.skins[pose.frame];
    if (!source.sourceImage) return false;
    // These are complete painted poses. Do not cut the shoulder into a second
    // layer or skin it with joints: the source has no hidden limb/torso artwork.
    // One common scale preserves the artist's proportions; the registered
    // solid sole is the support, with Box2D supplying all root translation.
    const REAL width=static_cast<REAL>(source.width*scale*body.bodyWidth);
    const REAL height=static_cast<REAL>(source.height*scale*body.bodyHeight);
    const auto saved=graphics.Save();
    graphics.TranslateTransform(center,bottom+source.groundInset*scale*static_cast<REAL>(body.bodyHeight));
    graphics.ScaleTransform(pose.flip?-1.0f:1.0f,1.0f);
    ImageAttributes attributes;attributes.SetWrapMode(WrapModeClamp,Color(0,0,0,0));
    const auto status=graphics.DrawImage(source.sourceImage.get(),RectF(-width/2,-height,width,height),
        0,0,static_cast<REAL>(source.width),static_cast<REAL>(source.height),UnitPixel,&attributes);
    graphics.Restore(saved);
    return status==Ok;
}

bool DrawJointWalkingPet(Graphics& graphics,const cat::animation::Pose& pose,
                         REAL center,REAL bottom,REAL scale,cat::Character character,
                         const cat::nutrition::Visual& body) {
    auto& art=CurrentArt(character);
    const auto& source=art.base.skins[1];
    if(source.source.empty())return false;
    if(!art.jointWalking) {
        auto skin=std::make_unique<JointWalkingSkin>();
        if(!skin->rig.Prepare(source.source.data(),source.width,source.height,source.width*4))return false;
        skin->image=std::make_unique<Bitmap>(skin->rig.Width(),skin->rig.Height(),skin->rig.Width()*4,
            PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(skin->rig.Pixels())));
        if(skin->image->GetLastStatus()!=Ok)return false;
        art.jointWalking=std::move(skin);
    }
    auto& skin=*art.jointWalking;
    const double reference=110.0/std::max(1,art.base.standingHeight);
    const double widthBody=source.width*reference*body.bodyWidth;
    const double heightBody=source.height*reference*body.bodyHeight;
    if(!skin.valid || skin.width!=widthBody || skin.height!=heightBody ||
            !cat::gait::SameFeet(skin.feet,pose.jointFeet) ||
            !cat::walk_dynamics::Same(skin.dynamics,pose.jointDynamics)) {
        if(!skin.rig.Raster(pose.jointFeet,widthBody,heightBody,pose.jointDynamics))return false;
        skin.feet=pose.jointFeet;skin.width=widthBody;skin.height=heightBody;skin.valid=true;
        skin.dynamics=pose.jointDynamics;
    }
    const REAL width=static_cast<REAL>(skin.rig.Width()*scale*body.bodyWidth);
    const REAL height=static_cast<REAL>(skin.rig.Height()*scale*body.bodyHeight);
    const auto saved=graphics.Save();
    graphics.TranslateTransform(center,bottom+skin.rig.GroundInsetPixels()*scale*static_cast<REAL>(body.bodyHeight));
    graphics.ScaleTransform(pose.flip?-1.0f:1.0f,1);
    ImageAttributes attrs;attrs.SetWrapMode(WrapModeTileFlipXY);
    const auto status=graphics.DrawImage(skin.image.get(),RectF(-width/2,-height,width,height),
        0,0,static_cast<REAL>(skin.rig.Width()),static_cast<REAL>(skin.rig.Height()),UnitPixel,&attrs);
    graphics.Restore(saved);
    return status==Ok;
}

bool DrawWalkingPet(Graphics& graphics, const cat::animation::Pose& pose,
                    REAL center, REAL bottom, REAL scale, cat::Character character,
                    cat::Outfit /*outfit*/, const cat::nutrition::Visual& body) {
    auto& art = CurrentArt(character);
    const auto& source = art.base.skins[cat::species::UsesNewArt(character) ? 2 : 1];
    if (source.source.empty() || source.width <= 0 || source.height <= 0) return false;
    if (!art.walking) {
        auto walking = std::make_unique<WalkingSkin>();
        if (!walking->rig.Prepare(source.source.data(),source.width,source.height,source.width*4,
                cat::walk::ProfileFor(character))) return false;
        walking->image = std::make_unique<Bitmap>(walking->rig.Width(),walking->rig.Height(),
            walking->rig.Width()*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(walking->rig.Pixels()));
        if (walking->image->GetLastStatus() != Ok) return false;
        art.walking = std::move(walking);
    }
    auto& walking = *art.walking;
    // The gait covers logical body pixels. Nutrition changes the picture's
    // width, but cannot lengthen its step or make planted paws drift.
    const double reference = 110.0 / std::max(1,art.base.standingHeight);
    const double widthBody = source.width*reference*body.bodyWidth;
    const double heightBody = source.height*reference*body.bodyHeight;
    if (walking.phase != pose.walkPhase || walking.stride != pose.walkStride ||
            walking.width != widthBody || walking.height != heightBody ||
            walking.duty != pose.walkDuty || walking.lift != pose.walkLift ||
            walking.feetValid != pose.walkFeetValid ||
            (pose.walkFeetValid && !cat::gait::SameFeet(walking.feet,pose.walkFeet))) {
        if (!walking.rig.Raster(pose.walkPhase,pose.walkStride,widthBody,heightBody,
                pose.walkDuty,pose.walkLift,pose.walkFeetValid?&pose.walkFeet:nullptr)) return false;
        walking.phase=pose.walkPhase; walking.stride=pose.walkStride;
        walking.width=widthBody; walking.height=heightBody;
        walking.duty=pose.walkDuty; walking.lift=pose.walkLift;
        walking.feet=pose.walkFeet;walking.feetValid=pose.walkFeetValid;
    }
    const REAL height = static_cast<REAL>(source.height*scale*body.bodyHeight);
    const REAL rasterWidth = static_cast<REAL>(walking.rig.Width()*scale*body.bodyWidth);
    const REAL inset = static_cast<REAL>(walking.rig.GroundInsetPixels()*height/source.height);
    const auto saved=graphics.Save();
    // Torso compression is a rigid translation, never a scale pulse. The
    // raster subtracts the same logical offset from every planted paw.
    const REAL compression=static_cast<REAL>(walking.rig.BodyOffset()*scale/reference);
    graphics.TranslateTransform(center,bottom+inset+compression);
    graphics.ScaleTransform(pose.flip ? -1.0f : 1.0f,1.0f);
    ImageAttributes attrs; attrs.SetWrapMode(WrapModeTileFlipXY);
    const auto status=graphics.DrawImage(walking.image.get(),RectF(-rasterWidth/2,-height,rasterWidth,height),
        0,0,static_cast<REAL>(walking.rig.Width()),static_cast<REAL>(walking.rig.Height()),UnitPixel,&attrs);
    graphics.Restore(saved);
    return status==Ok;
}

bool DrawRegisteredReclineLayers(const cat::animation::Pose& pose, REAL center, REAL bottom, REAL scale,
                                cat::Character character, const cat::nutrition::Visual& body,
                                Surface& from, Surface& to, int width, int height) {
    if(pose.bellyRig || pose.centerPivot || pose.angle!=0 || !pose.groundContact ||
       !cat::recline::Frame(pose.frame) || !cat::recline::Frame(pose.blendFrame) ||
       (pose.frame==4 && pose.blendFrame==4))return false;
    auto& art=CurrentArt(character);
    const bool added=cat::species::UsesNewArt(character);
    struct Layer { const std::uint32_t* pixels=nullptr;int width=0,height=0;cat::recline::Geometry size; };
    const double blink=std::max(pose.face.blink,cat::recline::TurnBlink(pose.frame,pose.blendFrame,pose.blend));
    const auto prepare=[&](int frame,Layer& layer){
        SpriteAtlas* atlas=&art.base;int cell=added?0:4;double factor=scale;
        if(frame>=24){
            if(added && frame==24)cell=7;
            else if(added){atlas=&g.companionBack;cell=cat::species::ArtCell(character);factor*=art.base.standingHeight/250.0;}
            else {atlas=&g.recline;cell=static_cast<int>(CharacterIndex(character))*2+frame-24;factor*=art.base.standingHeight/440.0;}
        }
        auto& skin=atlas->skins[cell];auto joints=pose.joints;
        const auto shape=frame==4?cat::deform::Shape::Sitting:frame==24?cat::deform::Shape::ReclineSide:cat::deform::Shape::ReclineBack;
        if(!PrepareSkin(skin,frame,joints,shape,false,character))return false;
        layer={skin.deformed.data(),skin.width,skin.height,
            {skin.width*factor*pose.scaleX*body.bodyWidth,skin.height*factor*pose.scaleY*body.bodyHeight}};
        if(frame>=24 && blink>0){
            const auto& closed=art.base.skins[5];
            if(!cat::recline::CloseEyes(skin.deformed.data(),skin.width,skin.height,skin.expression.data(),character,frame,blink,
                {closed.source.data(),closed.width,closed.height,cat::species::ClosedFaceFor(character)}))return false;
            layer.pixels=skin.expression.data();
        } else if(frame==4 && !cat::expression::Zero(pose.face)) {
            const auto& closed=art.base.skins[5];
            if(!cat::expression::Apply(skin.deformed.data(),skin.width,skin.height,skin.expression.data(),pose.face,
                cat::species::FaceFor(character),joints,{closed.source.data(),closed.width,closed.height,cat::species::ClosedFaceFor(character)}))return false;
            layer.pixels=skin.expression.data();
        }
        return true;
    };
    Layer first,second;cat::recline::Mesh mesh;
    if(!prepare(pose.frame,first)||!prepare(pose.blendFrame,second)||
       !cat::recline::Build(character,pose.frame,first.size,pose.blendFrame,second.size,pose.blend,mesh))return false;
    return cat::recline::Raster(first.pixels,first.width,first.height,reinterpret_cast<std::uint32_t*>(from.pixels),
               width,height,first.size,mesh,false,{center,bottom},pose.flip) &&
           cat::recline::Raster(second.pixels,second.width,second.height,reinterpret_cast<std::uint32_t*>(to.pixels),
               width,height,second.size,mesh,true,{center,bottom},pose.flip);
}

unsigned SpriteFrameFor(const cat::animation::Pose& pose,const cat::sprite_assets::Descriptor& layout) {
    if(!pose.spriteTimed)return pose.spriteFrame;
    auto options=pose.spriteOptions;options.sheetFrames=layout.frameCount;
    if(layout.program.stepCount)
        return cat::sprite_sequence::SampleProgram(layout.clip,layout.program,pose.spriteElapsedMs,options).frame;
    return cat::sprite_sequence::Sample(layout.clip,pose.spriteElapsedMs,options).frame;
}

const CareSpriteAtlas* CareSpriteFor(const cat::animation::Pose& pose,cat::Character character) {
    using Clip=cat::sprite_sequence::Clip;
    const auto clip=pose.fishMeal?Clip::Fish:pose.spriteClip;
    if(clip==Clip::None || (clip!=Clip::Jump && !pose.groundContact))return nullptr;
    if(const auto* sprite=LoadedCareSprite(clip,character)) {
        const auto index=SpriteFrameFor(pose,*sprite->layout);
        if(index<sprite->layout->frameCount && (sprite->frames[index] || sprite->image))return sprite;
    }
    return nullptr;
}
bool DrawCareSprite(Graphics& graphics,const cat::animation::Pose& pose,REAL center,REAL bottom,REAL scale,
                    cat::Character character,const cat::nutrition::Visual& body) {
    const auto* sprite=CareSpriteFor(pose,character);
    if(!sprite)return false;
    const auto index=SpriteFrameFor(pose,*sprite->layout);
    const auto& frame=sprite->layout->frames[index];
    const double reference=scale*CurrentArt(character).base.standingHeight/sprite->layout->referenceHeight;
    const REAL sx=static_cast<REAL>(reference*body.bodyWidth),sy=static_cast<REAL>(reference*body.bodyHeight);
    // Source rectangles retain their offsets and artist-authored support/root
    // anchors. Never resize an individual frame to its own tight alpha bounds.
    const auto saved=graphics.Save();graphics.TranslateTransform(center,bottom);
    graphics.ScaleTransform(pose.flip && sprite->layout->clip!=cat::sprite_sequence::Clip::Box?-1.0f:1.0f,1);
    ImageAttributes attributes;attributes.SetWrapMode(WrapModeClamp,Color(0,0,0,0));
    auto* isolated=sprite->frames[index].get();
    const auto status=graphics.DrawImage(isolated?isolated:sprite->image.get(),
        RectF(static_cast<REAL>(-frame.anchor.x*sx),static_cast<REAL>(-frame.anchor.y*sy),
            frame.rect.width*sx,frame.rect.height*sy),
        isolated?0:frame.rect.x,isolated?0:frame.rect.y,frame.rect.width,frame.rect.height,UnitPixel,&attributes);
    graphics.Restore(saved);
    return status==Ok;
}

void DrawStyledAnimationDirect(Graphics& graphics, cat::animation::Pose pose, REAL center, REAL bottom, REAL scale,
                         cat::Character character, cat::Outfit outfit, const cat::nutrition::Visual& body,
                         Surface& blendFrom, Surface& blendTo, int canvasWidth, int canvasHeight) {
    if(DrawCareSprite(graphics,pose,center,bottom,scale,character,body))return;
    if(pose.originalWalk && !cat::species::UsesNewArt(character) &&
            DrawOriginalWalkingPet(graphics,pose,center,bottom,scale,character,body))return;
    if(pose.jointWalk && !cat::species::UsesNewArt(character) &&
            DrawJointWalkingPet(graphics,pose,center,bottom,scale,character,body))return;
    const bool useWalkingRig=pose.walkingRig || (pose.originalWalk && cat::species::UsesNewArt(character));
    if (useWalkingRig && DrawWalkingPet(graphics,pose,center,bottom,scale,character,outfit,body)) return;
    auto draw = [&](Graphics& target, int frame) {
        DrawPet(target, frame, center, bottom, scale, pose.flip,
                static_cast<REAL>(pose.scaleX * body.bodyWidth),
                static_cast<REAL>(pose.scaleY * body.bodyHeight), static_cast<REAL>(pose.angle), pose.centerPivot,
                pose.joints, pose.groundContact, pose.bellyRig, character, outfit, pose.face);
    };
    if (pose.blendFrame < 0 || pose.blend <= 0 || !blendFrom.image || !blendTo.image) {
        draw(graphics, pose.frame);
        if (pose.bellyRig) DrawBellyHead(graphics, pose, center, bottom, scale, character, outfit, body);
        return;
    }
    if (pose.blend >= 1) {
        draw(graphics, pose.blendFrame);
        if (pose.bellyRig) DrawBellyHead(graphics, pose, center, bottom, scale, character, outfit, body);
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
    if(!DrawRegisteredReclineLayers(pose,center,bottom,scale,character,body,blendFrom,blendTo,canvasWidth,canvasHeight)) {
        layer(blendFrom, pose.frame); layer(blendTo, pose.blendFrame);
    }
    const unsigned weight = static_cast<unsigned>(std::lround(std::clamp(pose.blend, 0.0, 1.0) * 255));
    const std::size_t bytes = static_cast<std::size_t>(canvasWidth) * canvasHeight * 4;
    // Interpolate premultiplied BGRA itself. Drawing two translucent images over
    // one another would make the cat's opaque body transparent midway through.
    for (std::size_t i = 0; i < bytes; ++i)
        blendFrom.pixels[i] = static_cast<BYTE>((blendFrom.pixels[i] * (255 - weight) +
                                                 blendTo.pixels[i] * weight + 127) / 255);
    graphics.DrawImage(blendFrom.image.get(), Rect(0, 0, canvasWidth, canvasHeight),
                       0, 0, canvasWidth, canvasHeight, UnitPixel);
    if (pose.bellyRig) DrawBellyHead(graphics, pose, center, bottom, scale, character, outfit, body);
}

struct CarePresentationRenderer {
    cat::care_presentation::Transition transition;
    Surface previous,from,target;
    double center=0,bottom=0,scale=0,bodyWidth=0,bodyHeight=0;
    bool geometry=false;
    void Reset() {transition.Reset();geometry=false;}
};
CarePresentationRenderer& PrimaryCarePresentation() {static CarePresentationRenderer state;return state;}
CarePresentationRenderer& CompanionCarePresentation() {static CarePresentationRenderer state;return state;}
void PausePrimaryCarePresentation(std::uint64_t milliseconds) {PrimaryCarePresentation().transition.Pause(milliseconds);}
void ResetPrimaryCarePresentation() {PrimaryCarePresentation().Reset();}
void ReleaseCarePresentation() {
    for(auto* state:{&PrimaryCarePresentation(),&CompanionCarePresentation()}) {
        state->Reset();state->previous.Reset();state->from.Reset();state->target.Reset();
    }
}

void DrawStyledAnimation(Graphics& graphics, cat::animation::Pose pose, REAL center, REAL bottom, REAL scale,
                         cat::Character character, cat::Outfit outfit, const cat::nutrition::Visual& body,
                         Surface& blendFrom, Surface& blendTo, int canvasWidth, int canvasHeight,
                         CarePresentationRenderer* presentation=nullptr,std::uint64_t ticks=0,
                         std::uint64_t generation=0,bool paused=false) {
    const auto direct=[&](Graphics& target,const cat::animation::Pose& drawing) {
        DrawStyledAnimationDirect(target,drawing,center,bottom,scale,character,outfit,body,
            blendFrom,blendTo,canvasWidth,canvasHeight);
    };
    if(!presentation) {direct(graphics,pose);return;}
    auto& state=*presentation;
    const auto* authored=CareSpriteFor(pose,character);
    using Clip=cat::sprite_sequence::Clip;
    const auto calm=[](Clip clip) {return clip==Clip::Groom||clip==Clip::Sleep||clip==Clip::Eat||clip==Clip::Treat||clip==Clip::Fish;};
    const bool hasCalm=std::any_of(g.careSprites.begin(),g.careSprites.end(),[&](const auto& sprite) {
        return sprite->layout->character==static_cast<unsigned>(character)&&calm(sprite->layout->clip);
    });
    if(!hasCalm) {state.Reset();direct(graphics,pose);return;}
    const bool fixed=pose.groundContact&&!pose.jointWalk&&!pose.walkingRig&&!pose.originalWalk&&
        !pose.bellyRig&&!pose.centerPivot&&pose.x==0&&pose.lift==0&&pose.angle==0&&
        pose.scaleX==1&&pose.scaleY==1&&pose.blendFrame<0;
    // Large curled/supine silhouettes and all physical impacts must stay
    // single drawings. The authored waking story reaches neutral on its own.
    const bool curled=authored&&authored->layout->clip==Clip::Sleep&&SpriteFrameFor(pose,*authored->layout)>=3;
    const bool eligible=fixed&&!curled&&(authored?calm(authored->layout->clip):pose.frame==4)&&
        pose.joints.pawReachX==0&&pose.joints.pawReachY==0&&pose.joints.pawAnchorX==0;
    if(!eligible) {state.Reset();direct(graphics,pose);return;}
    const bool changed=!state.previous.image||!state.from.image||!state.target.image||
        state.previous.width!=canvasWidth||state.previous.height!=canvasHeight;
    if(changed && (!state.previous.Create(canvasWidth,canvasHeight)||!state.from.Create(canvasWidth,canvasHeight)||
            !state.target.Create(canvasWidth,canvasHeight))) {state.Reset();direct(graphics,pose);return;}
    if(changed || (state.geometry&&(std::abs(state.center-center)>.5||std::abs(state.bottom-bottom)>.5||
            std::abs(state.scale-scale)>.01||std::abs(state.bodyWidth-body.bodyWidth)>.015||
            std::abs(state.bodyHeight-body.bodyHeight)>.015)))state.transition.Reset();
    state.center=center;state.bottom=bottom;state.scale=scale;state.bodyWidth=body.bodyWidth;state.bodyHeight=body.bodyHeight;state.geometry=true;
    const bool neutral=cat::sprite_assets::Find(Clip::Sleep,static_cast<unsigned>(character),cat::sprite_assets::kCareSprites)!=nullptr;
    cat::care_presentation::Tag tag;
    tag.representation=authored?1+static_cast<unsigned>(authored->layout->clip):0;
    tag.identity=static_cast<unsigned>(character);tag.generation=generation;tag.eligible=true;
    tag.authored=authored!=nullptr;tag.flip=pose.flip;tag.neutralBridge=neutral;
    const auto plan=state.transition.Update(tag,ticks,paused);
    const std::size_t bytes=static_cast<std::size_t>(canvasWidth)*canvasHeight*4;
    if(plan.capture)std::memcpy(state.from.pixels,state.previous.pixels,bytes);
    auto drawing=pose;
    if(plan.bridge) {
        drawing.fishMeal=false;drawing.spriteClip=Clip::Sleep;drawing.spriteFrame=0;drawing.spriteTimed=false;
        cat::animation::UseAuthoredSprite(drawing);
    }
    {
        Graphics target(state.target.image.get());
        target.SetCompositingMode(CompositingModeSourceCopy);target.Clear(Color(0,0,0,0));
        target.SetCompositingMode(CompositingModeSourceOver);
        target.SetInterpolationMode(InterpolationModeHighQualityBicubic);target.SetSmoothingMode(SmoothingModeAntiAlias);
        target.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        direct(target,drawing);target.Flush(FlushIntentionSync);
    }
    if(plan.blend)cat::care_presentation::BlendPremultiplied(state.from.pixels,state.target.pixels,state.previous.pixels,bytes,plan.weight);
    else std::memcpy(state.previous.pixels,state.target.pixels,bytes);
    graphics.DrawImage(state.previous.image.get(),Rect(0,0,canvasWidth,canvasHeight),0,0,canvasWidth,canvasHeight,UnitPixel);
}

void DrawAnimatedPet(Graphics& graphics, const cat::animation::Pose& pose, REAL center, REAL bottom, REAL scale,
                     CarePresentationRenderer* presentation=nullptr,std::uint64_t ticks=0) {
    DrawStyledAnimation(graphics, pose, center, bottom, scale, g.preferences.character, g.preferences.outfit,
        cat::nutrition::VisualModifiers(g.nutrition), g.blendFrom, g.blendTo, g.surface.width, g.surface.height,
        presentation,ticks,0,g.menuOpen||g.dragging);
}
void DrawPetStyle(Graphics& graphics, cat::Character character, cat::Outfit outfit,
                  const cat::animation::Pose& pose, const cat::nutrition::Visual& body,
                  REAL center, REAL bottom, REAL desiredHeight,CarePresentationRenderer* presentation=nullptr,
                  std::uint64_t ticks=0,std::uint64_t generation=0,bool paused=false) {
    auto& from=g.styleBlendFrom; auto& to=g.styleBlendTo;
    RectF bounds; graphics.GetVisibleClipBounds(&bounds);
    const int width=std::max(1,static_cast<int>(std::ceil(bounds.X+bounds.Width)));
    const int height=std::max(1,static_cast<int>(std::ceil(bounds.Y+bounds.Height)));
    if (from.width!=width || from.height!=height) { from.Create(width,height); to.Create(width,height); }
    const REAL scale=desiredHeight/std::max(1,CurrentArt(character).base.standingHeight);
    DrawStyledAnimation(graphics,pose,center,bottom,scale,character,outfit,body,from,to,width,height,
        presentation,ticks,generation,paused);
}

bool DrawAuthoredBoxProp(Graphics& graphics,REAL localX,REAL ground,bool foreground=false) {
    if(g.toy!=ToyKind::Box || g.smallBoxEvent || g.preferences.character!=cat::Character::Cream ||
            g.careSprites.empty() || !g.authoredBoxProp.image)return false;
    const auto& source=g.authoredBoxProp.frames[0];
    const auto visual=cat::nutrition::VisualModifiers(g.nutrition);
    const double growth=cat::GrowthScale(g.pet.bornUtc,UtcNow());
    const REAL width=static_cast<REAL>(105*g.dpiScale*growth*visual.bodyWidth);
    const REAL height=static_cast<REAL>(47*g.dpiScale*growth*visual.bodyHeight);
    const int cut=foreground?static_cast<int>(source.Height*.45):0;
    const REAL fraction=static_cast<REAL>(source.Height-cut)/source.Height;
    ImageAttributes attributes;attributes.SetWrapMode(WrapModeClamp,Color(0,0,0,0));
    return graphics.DrawImage(g.authoredBoxProp.image.get(),
        RectF(localX-width/2,ground-height*fraction,width,height*fraction),
        source.X,source.Y+cut,source.Width,source.Height-cut,UnitPixel,&attributes)==Ok;
}

void DrawToyBackground(Graphics& graphics, REAL localX, REAL ground, ULONGLONG ticks) {
    if (g.toy == ToyKind::None) return;
    const REAL u = static_cast<REAL>(g.dpiScale);
    if(DrawAuthoredBoxProp(graphics,localX,ground))return;
    (void)ticks;
    if (g.toy == ToyKind::Ball) {
        const REAL lift = static_cast<REAL>(g.toyBall.lift * g.dpiScale);
        SolidBrush shadow(Color(55, 50, 45, 40)); graphics.FillEllipse(&shadow, localX-13*u, ground-4*u, 26*u, 4*u);
        const auto saved = graphics.Save();
        graphics.TranslateTransform(localX, ground-12*u-lift);
        graphics.RotateTransform(static_cast<REAL>(-g.toyBall.angle * 180 / cat::animation::kPi));
        SolidBrush ball(Color(255, 236, 111, 76)); Pen seam(Color(220, 159, 68, 46), 1.2f*u);
        graphics.FillEllipse(&ball, -12*u, -12*u, 24*u, 24*u);
        graphics.DrawArc(&seam, -9*u, -9*u, 18*u, 18*u, 210, 145);
        graphics.Restore(saved);
        SolidBrush shine(Color(170,255,255,255)); graphics.FillEllipse(&shine,localX-6*u,ground-20*u-lift,5*u,5*u);
    } else if (g.toy == ToyKind::Box) {
        const REAL u = static_cast<REAL>(g.dpiScale * (g.smallBoxEvent ? .65 : 1.0) * std::max(1.35, 1.55 * cat::GrowthScale(g.pet.bornUtc, UtcNow()) * cat::nutrition::VisualModifiers(g.nutrition).bodyWidth));
        SolidBrush back(Color(235, 188, 137, 82)); Pen edge(Color(220, 126, 82, 48), 1.3f*u);
        RectF box(localX-39*u, ground-48*u, 78*u, 48*u);
        graphics.FillRectangle(&back, box); graphics.DrawRectangle(&edge, box);
        PointF flapL[3]={{localX-39*u,ground-48*u},{localX-15*u,ground-62*u},{localX,ground-48*u}};
        PointF flapR[3]={{localX,ground-48*u},{localX+17*u,ground-62*u},{localX+39*u,ground-48*u}};
        SolidBrush flap(Color(230, 208, 157, 96)); graphics.FillPolygon(&flap,flapL,3); graphics.FillPolygon(&flap,flapR,3);
        SolidBrush inside(Color(255, 102, 71, 44)); graphics.FillEllipse(&inside,localX-32*u,ground-51*u,64*u,13*u);
    } else if (g.toy == ToyKind::PaperBag) {
        const REAL s = static_cast<REAL>(g.dpiScale * std::max(1.0, 1.16 * cat::GrowthScale(g.pet.bornUtc, UtcNow())));
        SolidBrush paper(Color(255, 190, 151, 105)), fold(Color(255, 158, 120, 79)), inside(Color(255, 91, 68, 47));
        Pen crease(Color(220, 130, 95, 61), 1.2f*s);
        PointF bag[4]={{localX-40*s,ground},{localX-46*s,ground-77*s},{localX+42*s,ground-77*s},{localX+38*s,ground}};
        graphics.FillPolygon(&paper,bag,4); graphics.DrawPolygon(&crease,bag,4);
        graphics.FillEllipse(&inside,localX-43*s,ground-84*s,86*s,16*s);
        PointF side[3]={{localX+26*s,ground},{localX+42*s,ground-77*s},{localX+38*s,ground}};
        graphics.FillPolygon(&fold,side,3);
        graphics.DrawLine(&crease,localX-29*s,ground-69*s,localX-24*s,ground-8*s);
    } else if (g.toy == ToyKind::Catnip) {
        SolidBrush cloth(Color(255, 197, 207, 146)), leaf(Color(255, 72, 139, 77)), light(Color(255, 117, 174, 95));
        Pen stem(Color(220, 65, 110, 58), 1.3f*u);
        graphics.FillEllipse(&cloth,localX-32*u,ground-8*u,64*u,8*u);
        for (int index=0; index<5; ++index) {
            const REAL x=localX+(index-2)*9*u, y=ground-(7+(index%2)*5)*u;
            graphics.DrawLine(&stem,x-5*u,y+3*u,x+6*u,y-5*u);
            graphics.FillEllipse(index%2 ? &leaf : &light,x-7*u,y-7*u,11*u,6*u);
            graphics.FillEllipse(&leaf,x,y-12*u,9*u,6*u);
        }
    } else if (g.toy == ToyKind::FishingRod) {
        Pen rod(Color(255, 105, 78, 63), 4*u), line(Color(200, 105, 111, 121), .9f*u);
        const REAL featherX=localX+static_cast<REAL>(g.toyLure.offset)*u;
        const REAL reachHeight=static_cast<REAL>(58*cat::GrowthScale(g.pet.bornUtc,UtcNow())*
            cat::nutrition::VisualModifiers(g.nutrition).bodyHeight);
        const REAL featherY=ground-(reachHeight-static_cast<REAL>(std::abs(g.toyLure.offset))*.12f)*u;
        graphics.DrawLine(&rod,localX+49*u,ground-2*u,localX+34*u,ground-145*u);
        graphics.DrawLine(&line,localX+34*u,ground-145*u,featherX,featherY-6*u);
        SolidBrush base(Color(255, 138, 117, 99)); graphics.FillEllipse(&base,localX+34*u,ground-7*u,30*u,7*u);
        SolidBrush pink(Color(255, 214, 119, 154)), gold(Color(255, 233, 180, 91));
        graphics.FillEllipse(&pink,featherX-11*u,featherY-7*u,13*u,20*u);
        graphics.FillEllipse(&gold,featherX-1*u,featherY-7*u,10*u,18*u);
    } else if (g.toy == ToyKind::Cushion) {
        const REAL s=static_cast<REAL>(g.dpiScale * std::max(.95, cat::GrowthScale(g.pet.bornUtc,UtcNow())));
        SolidBrush edge(Color(255, 135, 151, 190)), top(Color(255, 178, 192, 222));
        Pen seam(Color(220, 111, 129, 171), 1.0f*s);
        graphics.FillEllipse(&edge,localX-64*s,ground-11*s,128*s,11*s);
        graphics.FillEllipse(&top,localX-62*s,ground-12*s,124*s,9*s);
        graphics.DrawEllipse(&seam,localX-57*s,ground-10*s,114*s,6*s);
    }
}

void DrawToyForeground(Graphics& graphics, REAL localX, REAL ground) {
    if(g.toy==ToyKind::Box && g.authoredBoxVisible)return;
    const bool smallBox=g.smallBoxEvent && g.toy==ToyKind::Box && g.motion==Motion::BoxSit;
    if ((!g.toyEngaged && !smallBox) || g.perch.active || g.perch.transitioning || !g.physics.CurrentPose().grounded) return;
    const auto phase=g.toySession.phase;
    const double growth=cat::GrowthScale(g.pet.bornUtc,UtcNow());
    if (std::abs(g.toyX-g.centerX)>60*g.dpiScale*growth ||
            (!smallBox && (phase==cat::toy::Phase::Discover || phase==cat::toy::Phase::Approach || phase==cat::toy::Phase::Inspect))) return;
    if(DrawAuthoredBoxProp(graphics,localX,ground,true))return;
    if (g.toy == ToyKind::PaperBag) {
        const REAL u=static_cast<REAL>(g.dpiScale*std::max(1.0,1.16*growth));
        SolidBrush front(Color(255, 201, 163, 118)); Pen edge(Color(230, 131, 95, 60),1.1f*u);
        RectF panel(localX-39*u,ground-49*u,78*u,50*u);
        graphics.FillRectangle(&front,panel); graphics.DrawRectangle(&edge,panel);
        graphics.DrawLine(&edge,localX-30*u,ground-42*u,localX+29*u,ground-39*u);
        graphics.DrawLine(&edge,localX+25*u,ground-40*u,localX+21*u,ground-3*u);
        return;
    }
    if (g.toy == ToyKind::Cushion) {
        const REAL u=static_cast<REAL>(g.dpiScale*std::max(.95,growth));
        SolidBrush front(Color(255, 135, 151, 190));
        graphics.FillEllipse(&front,localX-63*u,ground-3*u,126*u,4*u);
        return;
    }
    if (g.toy != ToyKind::Box) return;
    const REAL u = static_cast<REAL>(g.dpiScale * (smallBox ? .65 : 1.0) * std::max(1.35, 1.55 * cat::GrowthScale(g.pet.bornUtc, UtcNow()) * cat::nutrition::VisualModifiers(g.nutrition).bodyWidth));
    SolidBrush front(Color(248, 201, 146, 87)); Pen edge(Color(230, 126, 82, 48), 1.3f*u);
    RectF panel(localX-40*u, ground-29*u, 80*u, 30*u);
    graphics.FillRectangle(&front,panel); graphics.DrawRectangle(&edge,panel);
    Pen tape(Color(180, 230, 203, 145), 4*u); graphics.DrawLine(&tape,localX,ground-28*u,localX,ground-1*u);
}

// Toys have their own click-through desktop layer, so walking away cannot
// clip them to the pet's 320-pixel drawing surface. They remain on the floor.
void RenderToy() {
    if (g.hidden || g.toy == ToyKind::None || (g.authoredBoxVisible && g.toy==ToyKind::Box)) {
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
    const int width = static_cast<int>(std::ceil(280 * g.dpiScale));
    const int height = static_cast<int>(std::ceil(200 * g.dpiScale));
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
    const Motion displayed = cat::motion::DisplayedMotion(g.motion,g.walkingMotion,
        physical.grounded,g.waitingForStop,WalkingVelocity(physical));
    auto pose = cat::animation::Evaluate(displayed, actionElapsed, ticks, g.facing, g.gait);
    if(pose.spriteTimed && (displayed==Motion::Eat || displayed==Motion::Treat || displayed==Motion::Punch))
        pose.spriteOptions.durationMs=g.nextDecision>=g.motionStart?g.nextDecision-g.motionStart:0;
    const bool authoredSleep=LoadedCareSprite(cat::sprite_sequence::Clip::Sleep,g.preferences.character)!=nullptr;
    if(displayed==Motion::Sleep) pose=cat::animation::EvaluateSleep(g.sleepVariant,actionElapsed,ticks,g.facing,g.perch.active&&!authoredSleep);
    if (displayed == Motion::Jump || displayed == Motion::Pounce) pose.lift = 0;
    if (physical.hanging) {
        pose=cat::animation::Evaluate(Motion::Hang,actionElapsed,ticks,g.facing,g.gait);
        pose.groundContact=false;
    } else if (!physical.grounded) {
        // Box2D owns height and landing time. Art follows the actual vertical
        // velocity, so changing weight or interrupting a jump cannot desync it.
        pose = cat::animation::AirbornePose(physical.velocityY, g.facing);
    } else if ((g.perch.preparing || g.jumpPending || g.waitingForStop) &&
            (displayed==Motion::Jump || displayed==Motion::Pounce)) {
        pose=cat::animation::JumpPreparationPose(actionElapsed,g.facing,displayed==Motion::Pounce);
    } else if (!g.perch.preparing && !g.waitingForStop && (g.jumpLaunched || !g.jumpPending) &&
            (displayed == Motion::Jump || displayed == Motion::Pounce)) {
        pose = cat::animation::LandingPose(g.landedAt && ticks>=g.landedAt ? ticks-g.landedAt : 280,
            g.landingStrength,g.facing);
    }
    if(physical.grounded && !g.jumpPending && !g.perch.preparing && g.landedAt && ticks>=g.landedAt &&
            (displayed==Motion::Jump || displayed==Motion::Pounce || displayed==Motion::Watch)) {
        if(const auto* jump=LoadedCareSprite(cat::sprite_sequence::Clip::Jump,g.preferences.character)) {
            const auto contactElapsed=ticks-g.landedAt;
            if(contactElapsed<cat::sprite_sequence::Duration(jump->layout->program,jump->layout->program.outro)) {
                cat::sprite_sequence::Options options;
                options.jump=cat::sprite_sequence::JumpPhase::Landing;options.contactElapsedMs=contactElapsed;
                cat::animation::AttachSprite(pose,cat::sprite_sequence::Clip::Jump,0,options);
            }
        }
    }
    if (!pose.bellyRig && pose.frame==4 && physical.grounded && g.landedAt && ticks>=g.landedAt) {
        pose.joints.crouch=std::max(pose.joints.crouch,
            cat::animation::LandingPose(ticks-g.landedAt,g.landingStrength,g.facing).joints.crouch);
    }
    if(displayed==Motion::Groom && physical.grounded && !g.waitingForStop) {
        cat::sprite_sequence::Options sprite;
        sprite.durationMs=g.nextDecision>=g.motionStart?g.nextDecision-g.motionStart:0;
        sprite.variant=g.groomVariant;
        cat::animation::AttachSprite(pose,cat::sprite_sequence::Clip::Groom,actionElapsed,sprite);
    }
    const bool cursorAttack = ApplyCursorAttackPose(pose,ticks,physical);
    if(displayed==Motion::Stretch && g.wakingFromSleep && g.sleepVariant==0 && physical.grounded &&
            (!physical.onPlatform || authoredSleep) && !g.waitingForStop &&
            !cursorAttack) {
        cat::sprite_sequence::Options sprite;sprite.sleep=cat::sprite_sequence::SleepStage::Waking;
        cat::animation::AttachSprite(pose,cat::sprite_sequence::Clip::Sleep,actionElapsed,sprite);
    }
    pose.fishMeal=displayed==Motion::Eat && g.fishMeal && physical.grounded && !cursorAttack;
    if(pose.fishMeal) {
        cat::sprite_sequence::Options sprite;
        sprite.durationMs=g.nextDecision>=g.motionStart?g.nextDecision-g.motionStart:0;
        cat::animation::AttachSprite(pose,cat::sprite_sequence::Clip::Fish,actionElapsed,sprite);
    }
    if(g.toy==ToyKind::Box && g.toyEngaged && physical.grounded && !physical.onPlatform &&
            !g.waitingForStop && !cursorAttack && std::abs(WalkingVelocity(physical))<=2 &&
            std::abs(g.centerX-g.toyX)<=4*g.dpiScale) {
        const auto phase=g.toySession.phase;
        if(phase==cat::toy::Phase::Interact || phase==cat::toy::Phase::Sleep || phase==cat::toy::Phase::BoxExit) {
            cat::sprite_sequence::Options options;
            if(phase==cat::toy::Phase::Sleep)options.stage=cat::sprite_sequence::Stage::Looping;
            if(phase==cat::toy::Phase::BoxExit)options.stage=cat::sprite_sequence::Stage::Exiting;
            cat::animation::AttachSprite(pose,cat::sprite_sequence::Clip::Box,
                static_cast<std::uint64_t>(std::max(0.0,g.toySession.elapsedMs)),options);
            if(phase==cat::toy::Phase::Sleep) {
                const auto* box=LoadedCareSprite(cat::sprite_sequence::Clip::Box,g.preferences.character);
                if(box && !box->layout->program.stepCount){pose.spriteFrame=7;pose.spriteTimed=false;}
            }
        }
    }
    cat::animation::ApplyAuthoredLocomotion(pose,displayed,g.gait,
        LoadedCareSprite(cat::animation::AuthoredLocomotionClip(g.gait),g.preferences.character)!=nullptr);
    // The final sheet's awake pose is the common neutral, not a different
    // legacy cat between every meal, grooming action and nap.
    if((displayed==Motion::Idle || displayed==Motion::Watch) && physical.grounded &&
            !cursorAttack && pose.spriteClip==cat::sprite_sequence::Clip::None &&
            LoadedCareSprite(cat::sprite_sequence::Clip::Sleep,g.preferences.character)) {
        pose.spriteClip=cat::sprite_sequence::Clip::Sleep;pose.spriteFrame=0;pose.spriteTimed=false;
    }
    const auto* authored=CareSpriteFor(pose,g.preferences.character);
    g.authoredBoxVisible=authored && authored->layout->clip==cat::sprite_sequence::Clip::Box;
    if(authored)cat::animation::UseAuthoredSprite(pose);
    pose = g.poseTransition.Apply(pose,ticks,g.motionSerial,
        static_cast<unsigned>(g.preferences.character), !cursorAttack,g.motionStart);
    Graphics graphics(g.surface.image.get());
    graphics.SetCompositingMode(CompositingModeSourceCopy); graphics.Clear(Color(0, 0, 0, 0));
    graphics.SetCompositingMode(CompositingModeSourceOver); graphics.SetCompositingQuality(CompositingQualityHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic); graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality); graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    const LONG windowLeft = static_cast<LONG>(std::round(g.centerX - g.surface.width / 2.0));
    const REAL center = static_cast<REAL>(g.centerX - windowLeft);
    const double bodyScale = g.dpiScale * cat::GrowthScale(g.pet.bornUtc, now);
    auto placement = cat::render::BuildPlacement(g.work.bottom, g.surface.height,
        g.dpiScale, physical.lift, pose.lift, bodyScale);
    // Some props and airborne legs extend below the registered root. Reserve
    // transparent room below the registered root for props and airborne legs,
    // without lifting/resizing the cat to fit the image's bounding box.
    int bottomInset=0;
    if(authored && (authored->layout->clip==cat::sprite_sequence::Clip::Fish ||
            authored->layout->clip==cat::sprite_sequence::Clip::Jump)) {
        bottomInset=cat::render::SpriteBottomSafetyInset(*authored->layout,110*bodyScale,
            cat::nutrition::VisualModifiers(g.nutrition).bodyHeight);
        placement=cat::render::WithBottomSafetyInset(placement,bottomInset);
    }
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
    DrawAnimatedPet(graphics, pose, static_cast<REAL>(center + pose.x * bodyScale), bottom, scale,
        &PrimaryCarePresentation(),ticks);
    DrawToyForeground(graphics, toyLocalX, static_cast<REAL>(g.surface.height - 1-bottomInset));
    if (g.motion == Motion::Sleep) {
        Font font(L"Segoe UI", static_cast<REAL>(13 * g.dpiScale), FontStyleRegular, UnitPixel);
        SolidBrush text(Color(165, 140, 136, 150));
        const REAL rise = static_cast<REAL>((ticks % 2600) / 2600.0 * 13 * g.dpiScale);
        graphics.DrawString(L"z", -1, &font, PointF(center + 40 * g.dpiScale, bottom - 62 * g.dpiScale - rise), &text);
    }
    if (ticks < g.bubbleUntil && !g.bubble.empty()) {
        const bool twoLines = g.bubble.find(L'\n') != std::wstring::npos;
        const REAL width = static_cast<REAL>(132 * g.dpiScale), height = static_cast<REAL>((twoLines ? 44 : 28) * g.dpiScale);
        const REAL x = center - width / 2, y = static_cast<REAL>(g.surface.height - bottomInset - (twoLines ? 172 : 156) * g.dpiScale);
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
    BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(std::lround(255*g.lifeCrossing.Opacity())), AC_SRC_ALPHA};
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
#include "companion_host.hpp"
void MaybeAnnounceUpdate(ULONGLONG ticks) {
    cat::update::NoticeContext context;
    context.now = ticks; context.notBefore = g.updateAnnounceAfter;
    context.enabled = updates.automatic && updates.hasUpdate;
    context.visible = !g.hidden;
    context.calm = g.motion == Motion::Idle || g.motion == Motion::Watch || g.motion == Motion::Walk;
    context.busy = g.closing || g.menuOpen || g.dragging || updates.applying ||
        updates.operation != UpdateState::Operation::None || g.prank.Active() ||
        !g.physics.CurrentPose().grounded;
    context.bubbleVisible = ticks < g.bubbleUntil;
    if (!updates.announcement.Ready(context)) return;
    Speak(cat::update::kCatUpdateMessage, 6500);
    updates.announcement.MarkAnnounced();
    // Optional notice history is independent of birthday and nutrition records. An unwritable history must not interrupt the desktop pet.
    const std::wstring path = g.directory + L"\\update-notice.ini", temporary = path + L".tmp";
    if (!WriteBytes(temporary, updates.announcement.Serialize()) ||
        !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        DeleteFileW(temporary.c_str());
}
void CareFood(cat::feeding::FoodKind kind) {
    if (!cat::feeding::IsValid(kind) ||
        (kind == cat::feeding::FoodKind::Fish && !cat::feeding::CanOfferFish(g.preferences.character))) return;
    if (!g.initialized || g.closing || updates.applying || g.menuOpen || g.dragging) return;
    const bool snack = cat::feeding::IsTreat(kind);
    const auto ticks = GetTickCount64();
    if(g.landedAt && ticks>=g.landedAt) {
        const auto* jump=LoadedCareSprite(cat::sprite_sequence::Clip::Jump,g.preferences.character);
        if(jump && ticks-g.landedAt<cat::sprite_sequence::Duration(jump->layout->program,jump->layout->program.outro)) {
            g.pendingCare=static_cast<int>(kind);return;
        }
    }
    if (g.perch.transitioning || !g.physics.CurrentPose().grounded) {
        g.pendingCare = static_cast<int>(kind);
        return;
    }
    g.pendingCare = -1;
    SyncNutrition(UtcNow());
    const auto result = cat::feeding::TryFeed(g.nutrition, g.nutrition.updatedUtc, kind, Random(0, 1));
    g.pet.hunger = g.nutrition.fullness;
    if (result != cat::nutrition::FeedResult::Accepted) {
        // Repeated hotkeys neither add food/care counts nor restart the reply.
        if (ticks < g.nextCareReply) return;
        g.nextCareReply = ticks + 12000;
        if (result == cat::nutrition::FeedResult::Full) {
            static constexpr const wchar_t* replies[]{L"배불러…\n조금 이따 먹을래", L"지금은 배가 빵빵해 ♡", L"조금만 쉬었다 먹을래"};
            Speak(replies[std::uniform_int_distribution<int>(0, 2)(g.random)], 3400);
        } else Speak(L"아직 소화 중이야 ♡", 2600);
        Render();
        return;
    }
    g.prank.Clear();
    if (g.brain) g.brain->Fed(ticks, snack);
    // Keep the established lifetime counts/affection, with nutrition as the
    // sole owner of fullness and elapsed-time body changes.
    cat::Feed(g.pet, g.nutrition.updatedUtc, snack);
    Remember(cat::feeding::MemoryEvent(kind));
    g.pet.hunger = g.nutrition.fullness;
    g.nextCareReply = ticks + 6500;
    const auto foodClip=snack?cat::sprite_sequence::Clip::Treat:
        kind==cat::feeding::FoodKind::Fish?cat::sprite_sequence::Clip::Fish:cat::sprite_sequence::Clip::Eat;
    const auto* foodAsset=LoadedCareSprite(foodClip,g.preferences.character);
    const bool authoredFood=foodAsset!=nullptr;
    const auto foodDuration=authoredFood && foodAsset->layout->program.stepCount?
        cat::sprite_sequence::ProgramDuration(foodAsset->layout->program):
        authoredFood?cat::sprite_sequence::CareDuration(foodClip,0):
        snack?cat::animation::kTreatDurationMs:3300;
    SetMotion(snack ? Motion::Treat : Motion::Eat,
        foodDuration);
    g.fishMeal = kind == cat::feeding::FoodKind::Fish;
    g.nextDecision=g.motionStart+foodDuration;
    NotifyCompanionFood();
    Speak(snack ? L"츄르다! 할짝할짝 ♡" : g.fishMeal ? L"생선이다! 냠냠 ♡" : L"냠냠, 잘 먹을게 ♡", 3000);
    SavePet(); Render();
}
void Care(bool snack) {
    CareFood(snack ? cat::feeding::FoodKind::Treat : cat::feeding::FoodKind::Meal);
}
void ResumePendingCare() {
    if (g.pendingCare < 0) return;
    const auto kind = static_cast<cat::feeding::FoodKind>(g.pendingCare);
    g.pendingCare = -1;
    CareFood(kind);
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
    SavePet(false); // Persist the two-hour request cooldown across restarts.
}
void ToggleHidden() {
    g.hidden = !g.hidden;
    g.prank.Clear();
    if (g.hidden) { ShowWindow(g.window, SW_HIDE); if (g.toyWindow) ShowWindow(g.toyWindow, SW_HIDE); }
    else { g.foodRequestAfter = std::max(g.foodRequestAfter, GetTickCount64() + 30000); g.updateAnnounceAfter = std::max(g.updateAnnounceAfter, GetTickCount64() + 30000); KeepOnWorkArea(); Render(); ShowWindow(g.window, SW_SHOWNOACTIVATE); SetWindowPos(g.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
    RefreshAnimationTimer();
}
const wchar_t* PrimaryPersonalityLabel() {
    if (g.life.enabled() && g.life.state.temperament != cat::life::Temperament::Legacy)
        return cat::life::TemperamentName(g.life.state.temperament);
    return g.brain ? cat::behavior::ArchetypeName(g.brain->GetArchetype()) : L"시크냥";
}
const wchar_t* PersonalityLabel() { return PrimaryPersonalityLabel(); }
void StartPunch() {
    if (g.hidden) ToggleHidden();
    // A paw contact effect requires the cat to be standing at its drawn feet.
    if (g.perch.transitioning || !g.physics.CurrentPose().grounded) return;
    g.prank.Clear();
    g.direction = g.centerX < (g.work.left + g.work.right) / 2.0 ? 1 : -1;
    g.bubbleUntil = 0;
    SetMotion(Motion::Punch, cat::animation::kPunchDurationMs);
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
    Render();
}

void PlaceToy(ToyKind toy) {
    if (g.hidden && toy!=ToyKind::None) return;
    const auto ticks=GetTickCount64();
    if (g.brain) g.brain->Interrupt(ticks);
    g.toy = toy;
    g.toyEngaged=false; g.approachingToy=false; g.toyContact=false;
    // Items now appear through autonomous events, never through user care.
    g.toyRewardPending=false;
    g.toyLure={};
    if (toy == ToyKind::None) {
        g.toySession.Cancel();
        if (!g.perch.transitioning) SetMotion(Motion::Idle, 2600);
        Speak(L"장난감은 치워둘게 냥", 2200);
        Render();
        return;
    }
    const double offset = (toy == ToyKind::Ball || toy == ToyKind::FishingRod ? 105.0 : 110.0) * g.dpiScale;
    const double margin = std::min(96.0 * g.dpiScale, (g.work.right - g.work.left) / 2.0);
    const double low = g.work.left + margin, high = g.work.right - margin;
    g.toyX = cat::toy::ClampCenter(g.centerX + g.direction * offset, low, high);
    g.toyBall.Reset(g.toyX / g.dpiScale);
    g.direction = g.toyX < g.centerX ? -1 : 1;
    g.facing = g.direction;
    g.toySession.Start(toy,g.direction); g.toyAppliedRevision=0;
    g.nextToyAction=ticks;
    // A toy lives on the taskbar floor. Leave a supporting window through
    // gravity instead of resetting the cat to the toy's position.
    if (g.perch.active && !g.perch.transitioning && !g.pet.roamingPaused)
        BeginPerchTransition(nullptr, {}, false);
    if (!g.perch.transitioning && g.physics.CurrentPose().grounded && !g.pet.roamingPaused) {
        g.toyDispatching=true; SetMotion(Motion::Watch,250); g.toyDispatching=false;
        g.toyMotion=Motion::Watch; g.toyEngaged=true;
    }
    const wchar_t* message=L"새 장난감이다!";
    switch(toy) {
    case ToyKind::Ball: message=L"공이다! 잡아볼까냥"; break;
    case ToyKind::Box: message=L"박스다! 들어가 봐야지"; break;
    case ToyKind::PaperBag: message=L"바스락… 안에 뭐가 있을까?"; break;
    case ToyKind::Catnip: message=L"킁킁… 좋은 냄새다냥"; break;
    case ToyKind::FishingRod: message=L"깃털이다! 잡아줄게냥"; break;
    case ToyKind::Cushion: message=L"폭신해 보인다 ♡"; break;
    default: break;
    }
    Speak(message,2200);
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

cat::cursor::Geometry CursorAttackGeometry(cat::Character character = g.preferences.character);

struct CursorInteraction {
    cat::environment::CursorSession session;
    Motion motion = Motion::Watch;
    cat::cursor::PawContact contacts;
    double cursorX = 0, cursorHeight = 0, cursorWorldHeight = 0;
    bool available = false, landingContact = false;
} cursorInteraction;

bool EnvironmentBusy() { return cursorInteraction.session.Active(); }
double CursorReactionVelocity() { return cursorInteraction.session.Velocity(); }

void PauseCursorReaction(ULONGLONG elapsedMs) {
    cursorInteraction.session.Pause(elapsedMs,
        {g.motion == cursorInteraction.motion, g.waitingForStop, g.motionStart, g.motionSerial});
}

void UpdateCursorReactionSample(ULONGLONG ticks, ULONGLONG elapsedMs, POINT cursor, bool available) {
    auto& interaction = cursorInteraction;
    cat::environment::CursorInput input;
    input.nowMs = ticks;
    input.pouncePreparationMs=PrimaryTakeoffMs(true);
    input.enabled = WorkInterferenceEnabled();
    const auto physical = g.physics.CurrentPose();
    input.grounded = physical.grounded;
    input.sizeScale = cat::GrowthScale(g.pet.bornUtc, UtcNow());
    input.movementScale = cat::nutrition::VisualModifiers(g.nutrition).speed *
        cat::species::ProfileFor(g.preferences.character).movementSpeedMultiplier;
    const auto pawGeometry = cat::cursor::Bounded(CursorAttackGeometry());
    input.pawStandOff = std::max(6.0,(pawGeometry.pawAnchorX-.5)*pawGeometry.width+3);
    if(const auto* punch=LoadedCareSprite(cat::sprite_sequence::Clip::Punch,g.preferences.character);
            punch && punch->layout->program.stepCount) {
        input.pawDurationMs=input.punchDurationMs=cat::sprite_sequence::ProgramDuration(punch->layout->program);
        input.pawStandOff=38.15*cat::nutrition::VisualModifiers(g.nutrition).bodyWidth;
    }
    input.canContinue = g.physicsReady && (physical.grounded || interaction.session.InFlight()) && !g.pet.roamingPaused &&
        !g.hidden && !g.menuOpen && !g.dragging && !g.closing && !updates.applying &&
        !g.perch.transitioning && !g.lifeSequenceCount && !g.lifeCrossing.Active() && !g.physics.Hanging() &&
        !g.approachingToy && !g.toyEngaged && !g.approachingUser &&
        !g.memorySequenceCount && g.pendingCare < 0;
    input.canStart = input.canContinue && !g.waitingForStop && !g.companionModel.Busy() &&
        (g.motion == Motion::Idle || g.motion == Motion::Watch || g.motion == Motion::Walk ||
         g.motion == Motion::Stalk || (g.motion == Motion::Loaf && ticks>=g.nextDecision));
    input.available = available;
    input.cursorX = (cursor.x - g.work.left) / g.dpiScale;
    interaction.cursorWorldHeight = (g.work.bottom-cursor.y)/g.dpiScale;
    input.cursorHeight = interaction.cursorWorldHeight-physical.lift;
    input.supportHeight = physical.lift;
    input.catX = physical.x;
    interaction.cursorX = input.cursorX; interaction.cursorHeight = input.cursorHeight;
    interaction.available = available;
    const auto bounds = MovementBoundsLogical();
    input.minX = bounds.first; input.maxX = bounds.second;
    input.interest = (g.brain ? g.brain->GetPersonality().curiosity : .5) *
        cat::species::ProfileFor(g.preferences.character).cursorInterest;
    input.roll = Random(0, 1);
    const bool wasActive=interaction.session.Active();
    const auto output = interaction.session.Step(input, elapsedMs,
        {g.motion == interaction.motion, g.waitingForStop, g.motionStart, g.motionSerial});
    if (output.changed) {
        if(!wasActive && g.brain)g.brain->Interrupt(ticks);
        using Action = cat::environment::CursorAction;
        switch (output.action) {
        case Action::Hunt: case Action::Sneak: case Action::ApproachSit: interaction.motion = Motion::Stalk; break;
        case Action::Chase: interaction.motion = Motion::Zoomies; break;
        case Action::Pounce: interaction.motion = Motion::Pounce; break;
        case Action::Sniff: interaction.motion = Motion::Sniff; break;
        case Action::Paw: case Action::Punch: interaction.motion = Motion::ToyPaw; break;
        case Action::Sit: interaction.motion = Motion::Loaf; break;
        case Action::Abandon: interaction.motion = Motion::EarFlick; break;
        default: interaction.motion = Motion::Watch; break;
        }
        SetMotion(interaction.motion, output.action == Action::None ? 1800 : 8000);
        interaction.session.AdoptMotion(g.motionStart, g.waitingForStop, g.motionSerial);
        interaction.contacts.Reset();
        interaction.landingContact = false;
    }
    if (interaction.session.Active()) g.direction = output.direction;
}

void UpdateCursorReaction(ULONGLONG ticks, ULONGLONG elapsedMs) {
    POINT cursor{};
    const bool available = GetCursorPos(&cursor) && MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL) == g.monitor;
    UpdateCursorReactionSample(ticks, elapsedMs, cursor, available);
}

bool CursorPlaybackAllowed() {
    return WorkInterferenceEnabled() && !g.pet.roamingPaused && !g.hidden && !g.menuOpen &&
        !g.dragging && !g.closing && !updates.applying && g.pendingCare < 0;
}

bool TryCursorPounce(ULONGLONG ticks) {
    auto& interaction = cursorInteraction;
    if (interaction.session.Action() != cat::environment::CursorAction::Pounce ||
            interaction.session.InFlight() || !CursorPlaybackAllowed() || !interaction.available ||
            !interaction.session.OwnsSerial(g.motionSerial) || g.motion != Motion::Pounce ||
            g.waitingForStop || ticks < g.motionStart+PrimaryTakeoffMs(true))
        return false;
    const auto bounds = MovementBoundsLogical();
    const double growth = cat::GrowthScale(g.pet.bornUtc, UtcNow());
    const double jump = cat::nutrition::VisualModifiers(g.nutrition).jump *
        cat::species::ProfileFor(g.preferences.character).jumpHeightMultiplier;
    const bool launched = cat::cursor::LaunchPounce(g.physics, interaction.session.TargetX(),
        bounds.first, bounds.second, growth, jump);
    interaction.session.ConfirmLaunch(launched);
    return launched;
}

cat::cursor::Geometry CursorAttackGeometry(cat::Character character) {
    const auto& art = CurrentArt(character);
    const bool newArt = cat::species::UsesNewArt(character);
    const auto& frame = art.base.frames[newArt ? 0 : 4];
    const auto body = cat::nutrition::VisualModifiers(g.nutrition);
    const double scale = 110.0 / std::max(1, art.base.standingHeight);
    return {frame.Width*scale*body.bodyWidth, frame.Height*scale*body.bodyHeight, newArt ? .80 : .69};
}

bool ApplyCursorAttackPose(cat::animation::Pose& pose, ULONGLONG ticks, const cat::physics::Pose& physical) {
    const auto& interaction = cursorInteraction;
    const auto action = interaction.session.Action();
    if (!interaction.session.Active() || !interaction.session.ContactAllowed() || !CursorPlaybackAllowed() ||
            (action != cat::environment::CursorAction::Paw && action != cat::environment::CursorAction::Punch) ||
            g.motion != interaction.motion || !interaction.session.OwnsSerial(g.motionSerial) || g.waitingForStop || !physical.grounded ||
            std::abs(WalkingVelocity(physical)) > 2 || !interaction.available)
        return false;
    const auto kind = action == cat::environment::CursorAction::Punch ?
        cat::cursor::AttackKind::Strong : cat::cursor::AttackKind::Gentle;
    if(const auto* punch=LoadedCareSprite(cat::sprite_sequence::Clip::Punch,g.preferences.character);
            punch && punch->layout->program.stepCount) {
        pose={};pose.flip=g.direction<0;
        cat::animation::AttachSprite(pose,cat::sprite_sequence::Clip::Punch,
            ticks>=g.motionStart?ticks-g.motionStart:0);
        return true;
    }
    const double growth = cat::GrowthScale(g.pet.bornUtc, UtcNow());
    pose = cat::cursor::AttackPose(kind, ticks >= g.motionStart ? ticks-g.motionStart : 0, g.direction,
        (interaction.cursorX-physical.x)/growth, (interaction.cursorWorldHeight-physical.lift)/growth, CursorAttackGeometry());
    return true;
}

bool UpdateCursorContact(ULONGLONG ticks, const cat::physics::Pose& physical) {
    auto& interaction = cursorInteraction;
    const bool allowed = interaction.session.Active() && interaction.session.ContactAllowed() &&
        interaction.available && CursorPlaybackAllowed() && !g.waitingForStop &&
        g.motion == interaction.motion && interaction.session.OwnsSerial(g.motionSerial);
    const double growth = cat::GrowthScale(g.pet.bornUtc, UtcNow());
    bool contact = cat::cursor::LandingContact(allowed && interaction.session.InFlight() && !interaction.landingContact, physical,
        interaction.cursorX, interaction.cursorWorldHeight-physical.lift, growth);
    if (contact) interaction.landingContact = true;
    cat::animation::Pose pose;
    if (allowed && ApplyCursorAttackPose(pose,ticks,physical)) {
        const auto kind = interaction.session.Action() == cat::environment::CursorAction::Punch ?
            cat::cursor::AttackKind::Strong : cat::cursor::AttackKind::Gentle;
        auto paw = cat::cursor::PawPosition(pose,CursorAttackGeometry());
        auto contactIndex=cat::cursor::ContactIndex(kind,ticks-g.motionStart);
        if(const auto* punch=LoadedCareSprite(cat::sprite_sequence::Clip::Punch,g.preferences.character);
                punch && punch->layout->program.stepCount) {
            cat::sprite_sequence::Options options;options.sheetFrames=punch->layout->frameCount;
            contactIndex=static_cast<int>(cat::sprite_sequence::ProgramContactIndex(cat::sprite_sequence::Clip::Punch,
                punch->layout->program,ticks-g.motionStart,options));
            const auto body=cat::nutrition::VisualModifiers(g.nutrition);
            paw={g.direction*38.15*body.bodyWidth,38.89*body.bodyHeight};
        }
        contact = interaction.contacts.Observe(contactIndex, true,
            physical.x+paw.x*growth, paw.y*growth, interaction.cursorX, interaction.cursorWorldHeight-physical.lift, 7*growth);
    }
    if (contact) Remember(cat::memory::Event::Play);
    return contact;
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
         << L"\n성격: " << PersonalityLabel() << L"\n\n"
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
    text << L"\n시간과 체형은 실제 날짜에 따라 천천히 변해요.\n알맞게 먹으면 몸 상태도 서서히 돌아와요.\n캐릭터를 바꿔도 생일과 기록은 이어지고, 바빠도 사라지지 않아요.\n\n밥: Ctrl+Alt+F    츄르: Ctrl+Alt+T\n숨기기/보이기: Ctrl+Alt+H\n고양이를 끌면 다른 모니터로 옮길 수 있어요.\n\n우클릭 메뉴에서 캐릭터를 고르고 밥과 츄르를 줄 수 있어요. 행동과 장난감 놀이는 스스로 시작해요.\n냥냥펀치의 화면 금은 잠깐 보였다 사라지는 장난이에요.";
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
    g.menuOpen = true; g.prank.Clear();
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
            _wcsicmp(file, (g.directory + L"\\sound-settings.bak").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\life.ini").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\life.bak").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\companion.ini").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\companion.bak").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\interaction-settings.ini").c_str()) == 0 ||
            _wcsicmp(file, (g.directory + L"\\interaction-settings.ini.bak").c_str()) == 0) {
            UpdateMessage(L"지금 사용 중인 기록 파일 대신 다른 폴더나 파일 이름을 선택해 주세요.", kTitle, MB_OK | MB_ICONINFORMATION); return;
        }
        const std::wstring temporary = std::wstring(file) + L".tmp";
        if (WriteBytes(temporary, cat::Serialize(g.pet)) && MoveFileExW(temporary.c_str(), file, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) Balloon(L"기록을 보관했어요", L"이 파일은 첫 동물의 생일·성장 기록이에요. 두 번째 동물·친밀도·기분·의상·설정까지 옮기려면 기록 폴더 전체를 함께 보관해 주세요.");
        else UpdateMessage(L"선택한 위치에 백업하지 못했어요. 다른 폴더를 선택해 주세요.", kTitle, MB_OK | MB_ICONERROR);
    }
}
void RunCommand(UINT command) {
    if(command>=kCharacterCommandBase && command<kCharacterCommandBase+cat::kCharacterCount){SelectCharacter(static_cast<cat::Character>(command-kCharacterCommandBase));return;}
    switch (command) {
    case kWorkInterferenceCommand: ChangeInteractionSettings(true); break;
    case kSeasonalEventsCommand: ChangeInteractionSettings(false); break;
    case kCompanionToggle: ToggleCompanion(); break;
    case kCompanionFeed: FeedCompanion(false); break;
    case kCompanionTreat: FeedCompanion(true); break;
    case kCompanionFish: FeedCompanionFood(cat::feeding::FoodKind::Fish); break;
    case kCompanionStatus: ShowCompanionStatus(); break;
    case FeedCommand: Care(false); break;
    case SnackCommand: Care(true); break;
    case kFishCommand: CareFood(cat::feeding::FoodKind::Fish); break;
    case StatusCommand: ShowPetStatus(); break;
    case PauseCommand: if (g.brain) g.brain->Interrupt(GetTickCount64()); g.pet.roamingPaused = !g.pet.roamingPaused; g.prank.Clear(); SetMotion(Motion::Idle, 5000); SavePet(); break;
    case HideCommand: ToggleHidden(); break;
    case BackupCommand: ExportBackup(); break;
    case FolderCommand: ShellExecuteW(g.window, L"open", g.directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case QuitCommand: PostMessageW(g.window, WM_CLOSE, 0, 0); break;
    case CreamCommand: SelectCharacter(cat::Character::Cream); break;
    case SilverCommand: SelectCharacter(cat::Character::SilverTabby); break;
    case UpdateCheckCommand: StartUpdateCheck(true); break;
    case UpdateViewCommand: ShowUpdateDetails(); break;
    case UpdateAutomaticCommand: ToggleAutomaticUpdates(); break;
    }
}
HMENU BuildPetMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return nullptr;
    const auto age = cat::Age(g.pet.bornUtc, UtcNow());
    const std::wstring title = L"옆자리 고양이 · 함께 " + std::to_wstring(age.days + 1) + L"일";
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, title.c_str()); AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, FeedCommand, L"밥 주기\tCtrl+Alt+F");
    AppendMenuW(menu, MF_STRING, SnackCommand, L"츄르 주기\tCtrl+Alt+T");
    AppendMenuW(menu, MF_STRING | (cat::feeding::CanOfferFish(g.preferences.character) ? MF_ENABLED : MF_GRAYED),
        kFishCommand, L"생선 주기");
    HMENU companionMenu = CreatePopupMenu();
    AppendMenuW(companionMenu, MF_STRING | (g.companionStore.enabled() ? MF_CHECKED : MF_UNCHECKED),
        kCompanionToggle, g.companionStore.available() ? L"두 번째 동물 함께 지내기" : L"두 번째 동물 만나기");
    if (g.companionStore.available()) {
        const UINT enabled = g.companionStore.enabled() ? MF_ENABLED : MF_GRAYED;
        AppendMenuW(companionMenu, MF_STRING | enabled, kCompanionFeed, L"두 번째 동물에게 밥 주기");
        AppendMenuW(companionMenu, MF_STRING | enabled, kCompanionTreat, L"두 번째 동물에게 간식 주기");
        AppendMenuW(companionMenu, MF_STRING | enabled |
            (cat::feeding::CanOfferFish(g.companionStore.state.preferences.character) ? MF_ENABLED : MF_GRAYED),
            kCompanionFish, L"두 번째 고양이에게 생선 주기");
        AppendMenuW(companionMenu, MF_STRING, kCompanionStatus, L"두 번째 동물의 성장 기록");
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(companionMenu), L"두 마리 함께 키우기");
    HMENU characters = CreatePopupMenu();
    for(int i=0;i<cat::kCharacterCount;++i) AppendMenuW(characters,MF_STRING,kCharacterCommandBase+i,cat::CharacterName(static_cast<cat::Character>(i)));
    CheckMenuRadioItem(characters,kCharacterCommandBase,kCharacterCommandBase+cat::kCharacterCount-1,kCharacterCommandBase+static_cast<int>(g.preferences.character),MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP | MF_STRING, reinterpret_cast<UINT_PTR>(characters), L"캐릭터 선택");

    AppendMenuW(menu, MF_STRING, StatusCommand, L"생일과 성장 기록"); AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g.pet.roamingPaused ? MF_CHECKED : MF_UNCHECKED), PauseCommand, L"이 자리에서 쉬기");
    AppendMenuW(menu, MF_STRING | (g.workInterference ? MF_CHECKED : MF_UNCHECKED), kWorkInterferenceCommand, L"업무 방해 모드 · 커서와 놀기");
    AppendMenuW(menu, MF_STRING | (g.interactionSettings.seasonalEvents ? MF_CHECKED : MF_UNCHECKED), kSeasonalEventsCommand, L"계절·기념일 이벤트");
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
    return menu;
}
void ShowMenu(POINT point) {
    if (g.menuOpen || g.closing || updates.applying) return;
    HMENU menu = BuildPetMenu();
    if (!menu) return;
    if (point.x == -1 && point.y == -1) GetCursorPos(&point);
    g.menuOpen = true; g.prank.Clear();
    SetForegroundWindow(g.window); // Only after an explicit context-menu request, never from a timer or care hotkey.
    UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, g.window, nullptr);
    PostMessageW(g.window, WM_NULL, 0, 0); DestroyMenu(menu); g.menuOpen = false;
    if (choice) RunCommand(choice);
}
bool ToyInteractionAvailable() {
    const auto physical=g.physics.CurrentPose();
    return cat::toy::InteractionAllowed(physical.grounded,physical.onPlatform,g.pet.roamingPaused,
        g.hidden || g.menuOpen || g.dragging || g.perch.active || g.perch.transitioning || g.waitingForStop ||
        g.lifeSequenceCount || g.lifeCrossing.Active() || g.physics.Hanging() || g.smallBoxEvent);
}

void AdvanceToyObjects(double elapsed) {
    const bool paused=g.pet.roamingPaused || g.hidden || g.menuOpen || g.dragging;
    if (g.toy==ToyKind::Ball) {
        // Floor toys retain work-area bounds while the cat visits a window.
        const double width=(g.work.right-g.work.left)/g.dpiScale;
        const double margin=std::min(96.0,width*.5);
        const double growth=cat::GrowthScale(g.pet.bornUtc,UtcNow());
        const auto [low,high]=cat::toy::RollingBounds(width,margin,width-margin,36*growth);
        g.toyBall.Advance(elapsed,g.work.left/g.dpiScale+low,g.work.left/g.dpiScale+high,paused);
        g.toyX=g.toyBall.x*g.dpiScale;
    } else if (g.toy==ToyKind::FishingRod) g.toyLure.Advance(elapsed,paused);
}

void AdvanceAutomaticToy(double elapsed) {
    const auto physical=g.physics.CurrentPose();
    cat::toy::AutoInput input;
    input.paused=g.pet.roamingPaused || g.hidden || g.menuOpen || g.dragging;
    if (input.paused || !std::isfinite(elapsed) || elapsed<=0 || elapsed>.25) return;
    input.visibleKind=g.toy;
    input.phase=g.toySession.phase;
    input.ballSettled=g.toyBall.grounded && std::abs(g.toyBall.velocity)<.5 &&
        std::abs(g.toyBall.verticalVelocity)<.5;
    input.protectedUse=g.smallBoxEvent;
    const bool calm=g.motion==Motion::Idle || g.motion==Motion::Watch || g.motion==Motion::Loaf;
    input.eligible=calm && CanStartAutonomousStory(GetTickCount64()) && ToyInteractionAvailable() && std::abs(WalkingVelocity(physical))<=2 &&
        !g.toyEngaged && !g.approachingUser && !g.memorySequenceCount && !EnvironmentBusy() &&
        !g.companionModel.Busy() && g.pendingCare<0 && !g.jumpPending && !g.jumpLaunched;
    const auto decision=g.automaticToy.Advance(elapsed,input,static_cast<std::uint32_t>(g.random()));
    if (decision.action==cat::toy::AutoAction::Place) {
        PlaceToy(decision.kind);
    } else if (decision.action==cat::toy::AutoAction::Retire && g.toy==decision.kind) {
        const bool wasEngaged=g.toyEngaged;
        g.toy=ToyKind::None; g.toyEngaged=false; g.approachingToy=false;
        g.toyContact=false; g.toyRewardPending=false; g.toySession.Cancel();
        g.toyLure={}; g.nextToyAction=0;
        // Expiry can interrupt a stuck approach, but a finished item's cleanup
        // must not replace a meal, companion interaction or other new action.
        if (wasEngaged) SetMotion(Motion::Watch,900);
        if (g.toyWindow) ShowWindow(g.toyWindow,SW_HIDE);
    }
}

Motion ToySessionMotion() {
    using Phase=cat::toy::Phase;
    switch(g.toySession.phase) {
    case Phase::Approach: case Phase::Exit: return Motion::Walk;
    case Phase::Enter: return Motion::Stalk;
    case Phase::Inspect: return g.toy==ToyKind::Catnip ? Motion::Groom :
        g.toy==ToyKind::Box ? Motion::ToyPaw : Motion::Watch;
    case Phase::Interact:
        if (g.toy==ToyKind::Ball || g.toy==ToyKind::FishingRod) return Motion::ToyPaw;
        if (g.toy==ToyKind::Catnip) return Motion::BellyRoll;
        if (g.toy==ToyKind::Cushion) return Motion::Knead;
        return Motion::BoxSit;
    case Phase::Rest: return g.toy==ToyKind::PaperBag ? Motion::Watch : Motion::Groom;
    case Phase::Sleep: return Motion::Sleep;
    case Phase::BoxExit: return Motion::BoxSit;
    default: return Motion::Watch;
    }
}

void ApplyToyPhase(ULONGLONG ticks) {
    if (g.toyAppliedRevision==g.toySession.revision) return;
    g.toyAppliedRevision=g.toySession.revision;
    const auto phase=g.toySession.phase;
    if(phase==cat::toy::Phase::Interact && g.toy==ToyKind::Box) {
        if(const auto* box=LoadedCareSprite(cat::sprite_sequence::Clip::Box,g.preferences.character)) {
            const auto& program=box->layout->program;
            g.toySession.boxVisitMs=program.stepCount?cat::sprite_sequence::Duration(program,program.intro):
                cat::sprite_sequence::kBoxDurationMs;
            g.toySession.boxExitMs=cat::sprite_sequence::Duration(program,program.outro);
        }
    }
    if (phase==cat::toy::Phase::Wait) {
        g.toyEngaged=false; g.approachingToy=false;
        g.nextToyAction=ticks+static_cast<ULONGLONG>(cat::toy::Duration(g.toy,phase));
        g.toyDispatching=true; SetMotion(Motion::Watch,900); g.toyDispatching=false;
        return;
    }
    g.toyMotion=ToySessionMotion();
    if (g.toyMotion==Motion::ToyPaw) {
        const double target=g.toyX+(g.toy==ToyKind::FishingRod ? g.toyLure.offset*g.dpiScale : 0);
        g.direction=target<g.centerX ? -1 : 1; g.facing=g.direction;
    }
    const auto duration=cat::toy::Travels(phase) ? 30000ULL :
        static_cast<ULONGLONG>(g.toySession.PhaseDuration()+100);
    g.toyDispatching=true; SetMotion(g.toyMotion,duration); g.toyDispatching=false;
    g.speed=38*g.dpiScale;
    g.approachingToy=cat::toy::Travels(phase);
    // Autonomous play is not evidence that the user provided care.
    g.toyRewardPending=false;
}

bool MaybePlayWithToy(ULONGLONG ticks) {
    if (g.toy==ToyKind::None || !ToyInteractionAvailable()) return false;
    if (g.automaticToy.ActiveKind()==g.toy &&
            (g.automaticToy.WaitingToRetire() || g.toySession.phase==cat::toy::Phase::Wait)) return false;
    if (g.toyEngaged) return true;
    if (!CanStartAutonomousStory(ticks)) return false;
    if (EnvironmentBusy() || g.memorySequenceCount || g.companionModel.Busy() ||
            g.approachingUser || g.pendingCare>=0) return false;
    if (ticks<g.nextToyAction) return false;
    if (g.motion!=Motion::Idle && g.motion!=Motion::Walk && g.motion!=Motion::Watch &&
            g.motion!=Motion::Loaf && g.motion!=Motion::Groom) return false;
    if (g.brain) g.brain->Interrupt(ticks);
    g.toySession.Start(g.toy,g.toyX<g.centerX ? -1 : 1);
    g.toyEngaged=true;
    ApplyToyPhase(ticks);
    return true;
}

void DecideMotion() {
    if (!g.brain) { SetMotion(Motion::Idle, 10000); return; }
    const auto ticks = GetTickCount64();
    SYSTEMTIME local{}; GetLocalTime(&local);
    POINT cursor{}; const bool nearby = CursorNear(&cursor);
    const auto age = cat::Age(g.pet.bornUtc, UtcNow());
    SyncExperience();
    if (g.life.enabled() && WorkInterferenceEnabled() && !g.pet.roamingPaused && nearby &&
            !g.perch.active && std::abs(cursor.x-g.centerX)<90*g.dpiScale) {
        const double bond=g.memory.enabled() ? cat::memory::Summarize(g.memory.state).bond : 0;
        const auto feeling=cat::life::Summarize(g.life.state,bond);
        if (bond<35 && Random(0,1)<.16*feeling.shyness) {
            g.direction=cursor.x<g.centerX ? 1 : -1;g.speed=38*g.dpiScale;
            SetMotion(Motion::Walk,1800);return;
        }
    }
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
    case Action::Punch: StartPunch(); return;
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
    if (selected == Motion::Sneeze) {
        Speak(L"에취!", 1200);
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
bool MaybeMemoryRoutine(ULONGLONG ticks) {
    if(!CanStartAutonomousStory(ticks))return false;
    if (!g.memory.enabled() || ticks < g.nextMemoryRoutineAttempt) return false;
    const auto now = UtcNow();
    cat::routine::Context context;
    context.nowUtc = now;
    SYSTEMTIME local{}; GetLocalTime(&local);
    context.localDay = cat::DaysFromCivil(local.wYear, local.wMonth, local.wDay);
    context.localHour = local.wHour;
    context.roamingPaused = g.pet.roamingPaused;
    context.allowRare = !g.life.enabled(); // Unified life quota owns migrated long-term sequences.
    context.busy = !g.initialized || g.hidden || g.menuOpen || g.dragging || g.closing || updates.applying ||
        g.perch.transitioning || EnvironmentBusy() || g.toyEngaged || g.approachingToy || g.approachingUser || g.memorySequenceCount ||
        g.lifeSequenceCount || g.lifeCrossing.Active() || g.physics.Hanging() ||
        g.pendingCare >= 0 || ticks < g.bubbleUntil || g.prank.Active() || !g.physics.CurrentPose().grounded ||
        (g.motion != Motion::Idle && g.motion != Motion::Watch && g.motion != Motion::Walk);
    if (context.busy) return false;
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
    context.cursorReachable = WorkInterferenceEnabled() && !g.companionModel.Busy() && nearby && target.has_value();
    const auto decision = cat::routine::Peek(g.memory.state, context, Random(0, 1));
    using Decision = cat::routine::Decision;
    if (decision == Decision::None) return false;
    // Persist the accepted schedule before showing it: restart cannot replay a
    // rare event. Failure leaves both the opportunity and UI state unchanged.
    const auto prior = g.memory.state;
    if (!cat::routine::Commit(g.memory.state, context, decision)) return false;
    if (!SaveMemory()) { g.memory.state = prior; g.nextMemoryRoutineAttempt = ticks + 30000; return false; }
    if (decision == Decision::LunchRequest || decision == Decision::TreatRequest) {
        if (!cat::routine::AcceptRequest(g.nutrition, decision, now)) return false;
        if (decision == Decision::LunchRequest) {
            Speak(L"점심시간이네…\n나도 밥 먹을래", 4200);
        } else {
            Speak(L"그때 그 츄르…\n조금만 줄래?", 4200);
        }
        SavePet(false);
        return true;
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
    return true;
}

cat::life::EventContext LifeEventContext(ULONGLONG ticks, bool decisionBoundary = false) {
    cat::life::EventContext context;
    context.nowUtc=UtcNow();
    SYSTEMTIME local{};GetLocalTime(&local);
    context.localDay=cat::DaysFromCivil(local.wYear,local.wMonth,local.wDay);
    context.sleeping=g.motion==Motion::Sleep;
    context.perched=g.perch.active;
    context.roamingPaused=g.pet.roamingPaused;
    context.seasonalEnabled=g.interactionSettings.seasonalEvents;
    context.bond=g.memory.enabled() ? cat::memory::Summarize(g.memory.state).bond : 0;
    context.inheritedRareUtc=g.memory.enabled() ? g.memory.state.lastRareUtc : 0;
    context.fullness=g.nutrition.fullness;
    context.crownWorn=false; // Legacy wardrobe values are retained only in storage.
    context.boxAvailable=g.toy==ToyKind::Box && std::abs(g.toyX-g.centerX)<42*g.dpiScale;
    const auto elapsed=ticks>=g.motionStart ? ticks-g.motionStart : 0;
    unsigned requiredHits=2;
    if(const auto* punch=LoadedCareSprite(cat::sprite_sequence::Clip::Punch,g.preferences.character);
            punch && punch->layout->program.stepCount) {
        requiredHits=0;
        for(unsigned i=0;i<punch->layout->program.cueCount;++i)
            if(punch->layout->program.cues[i].events&cat::sprite_sequence::Contact)++requiredHits;
    }
    if(g.motion==Motion::Punch && elapsed>=650 && requiredHits &&
            g.punchHits>=static_cast<int>(requiredHits) && g.prank.Active()) context.trigger=cat::life::Trigger::Punch;
    else if(g.motion==Motion::TailChase && elapsed>=1600) context.trigger=cat::life::Trigger::TailChase;
    else if(g.motion==Motion::Sneeze && elapsed>=1200) context.trigger=cat::life::Trigger::Sneeze;
    const auto physical=g.physics.CurrentPose();
    const auto [low,high]=MovementBoundsLogical();
    context.edgeDistanceLogical=std::max(0.0,std::min(physical.x-low,high-physical.x));
    const double width=(g.work.right-g.work.left)/g.dpiScale;
    if(g.perch.active && g.physics.OnPlatform()) {
        const double left=(std::max(g.perch.rect.left,g.work.left)-g.work.left)/g.dpiScale;
        const double right=(std::min(g.perch.rect.right,g.work.right)-g.work.left)/g.dpiScale;
        const double edge=physical.x<(left+right)/2 ? left : right;
        context.edgeSupported=edge>=96 && edge<=width-96 && std::abs(physical.x-edge)<=90;
    }
    context.screenCrossingSafe=!g.perch.active && width>480 && context.edgeDistanceLogical<20 &&
        !g.toyEngaged && !g.toyContact;
    const bool calm=g.motion==Motion::Idle || g.motion==Motion::Watch || g.motion==Motion::Walk || context.sleeping;
    context.busy=!g.initialized || g.hidden || g.menuOpen || g.dragging || g.closing || updates.applying ||
        !g.physicsReady || !physical.grounded || g.perch.transitioning || g.waitingForStop || EnvironmentBusy() ||
        g.approachingToy || g.toyEngaged || g.approachingUser || g.memorySequenceCount || g.lifeSequenceCount ||
        g.lifeCrossing.Active() || g.physics.Hanging() || g.pendingCare>=0 || ticks<g.bubbleUntil ||
        (g.prank.Active() && context.trigger!=cat::life::Trigger::Punch) ||
        (!decisionBoundary && !calm && context.trigger==cat::life::Trigger::None);
    return context;
}

void ObserveLife(ULONGLONG ticks) {
    if(!g.life.enabled())return;
    const auto now=UtcNow();SYSTEMTIME local{};GetLocalTime(&local);
    cat::life::Observe(g.life.state,now,{static_cast<int>(local.wHour),g.motion==Motion::Sleep || g.motion==Motion::SleepTalk,
        now<g.lifeInteractionUntil,!g.hidden && !g.menuOpen && !g.dragging && !g.closing,g.nutrition.fullness});
    if(ticks<g.nextLifeEventAttempt)return;
    const auto prior=g.life.state;
    if(cat::life::UnlockSeasonal(g.life.state,LifeEventContext(ticks)) && !SaveLife(false)) {
        g.life.state=prior;g.nextLifeEventAttempt=ticks+30000;
    }
}

bool TryBeginEdgeHang() {
    if(!g.perch.active || !g.physics.OnPlatform())return false;
    const double left=(std::max(g.perch.rect.left,g.work.left)-g.work.left)/g.dpiScale;
    const double right=(std::min(g.perch.rect.right,g.work.right)-g.work.left)/g.dpiScale;
    const auto physical=g.physics.CurrentPose();
    return g.physics.BeginHang(physical.x<(left+right)/2 ? left : right,(g.work.bottom-g.perch.rect.top)/g.dpiScale);
}

bool ContinueLifeSequence() {
    if(!g.lifeSequenceCount)return false;
    const auto next=g.lifeSequence[0];auto remainder=g.lifeSequence;
    const auto count=g.lifeSequenceCount-1;
    for(unsigned i=0;i<count;++i)remainder[i]=remainder[i+1];
    const ULONGLONG duration=next==Motion::Sleep ? 60000 : next==Motion::Loaf ? 12000 :
        next==Motion::Knead ? 5200 : next==Motion::Zoomies ? 1800 : next==Motion::Startled ? 1000 :
        next==Motion::BellyRoll ? cat::animation::kBellyRollDurationMs : 3200;
    if(g.pet.roamingPaused && cat::animation::Travels(next)) {
        g.lifeSequenceCount=0;SetMotion(Motion::Watch,3200);return true;
    }
    g.lifeDispatching=true;SetMotion(next,duration);g.lifeDispatching=false;
    g.lifeSequence=remainder;g.lifeSequenceCount=count;g.smallBoxEvent=false;
    return true;
}

bool DispatchLifeEvent(cat::life::Event event, ULONGLONG ticks) {
    using Event=cat::life::Event;
    if(event==Event::None)return false;
    if(event==Event::EdgeHang && !TryBeginEdgeHang())return false;
    if(g.brain)g.brain->Interrupt(ticks);
    switch(event) {
    case Event::PunchStartle:case Event::TailStartle:case Event::SneezeStartle:
        g.direction=g.centerX<(g.work.left+g.work.right)/2.0 ? 1 : -1;
        g.speed=125*g.dpiScale;SetMotion(Motion::Startled,850);
        g.lifeSequence[0]=Motion::Zoomies;g.lifeSequence[1]=Motion::Watch;g.lifeSequenceCount=2;
        Speak(event==Event::PunchStartle ? L"내가 했나?!" : event==Event::SneezeStartle ? L"…깜짝이야!" : L"앗, 내 꼬리!",1200);break;
    case Event::Yawn:SetMotion(Motion::Yawn,2400);break;
    case Event::SleepTalk:
        SetMotion(Motion::SleepTalk,2600);g.lifeSequence[0]=Motion::Sleep;g.lifeSequenceCount=1;
        Speak(L"냐… 츄르…",1800);break;
    case Event::Peek:SetMotion(Motion::Peek,cat::animation::kPeekDurationMs);break;
    case Event::SmallBox:
        SetMotion(Motion::BoxSit,2300);g.smallBoxEvent=true;
        g.lifeSequence[0]=Motion::Startled;g.lifeSequence[1]=Motion::Watch;g.lifeSequenceCount=2;
        Speak(L"분명 들어갔었는데…",2200);break;
    case Event::CrownPride:SetMotion(Motion::Pride,3500);Speak(L"짐이 바로 고양이랴",2600);break;
    case Event::EdgeHang:
        SetMotion(Motion::Hang,1600);g.lifeHangUntil=ticks+1400;g.waitingForStop=false;break;
    case Event::ScreenCrossing:
        SetMotion(Motion::Watch,1800);g.lifeCrossing.Start();break;
    case Event::BondKnead:
        SetMotion(Motion::Knead,5200);g.lifeSequence[0]=Motion::Loaf;g.lifeSequenceCount=1;
        Speak(L"네 옆이 편해졌어 ♡",3200);break;
    case Event::BondGreeting:
        SetMotion(Motion::TailRaise,cat::animation::kTailRaiseDurationMs);
        g.lifeSequence[0]=Motion::BellyRoll;g.lifeSequence[1]=Motion::Watch;g.lifeSequenceCount=2;
        Speak(L"오래 함께한 너니까 ♡",3200);break;
    case Event::BondLifetime:
        SetMotion(Motion::Stretch,3200);g.lifeSequence={Motion::Knead,Motion::Loaf,Motion::Sleep};g.lifeSequenceCount=3;
        Speak(L"앞으로도 쭉…\n네 옆에 있을게",4200);break;
    case Event::Birthday:case Event::FirstWeek:case Event::FirstYear:case Event::FifthYear:
        SetMotion(Motion::TailRaise,cat::animation::kTailRaiseDurationMs);
        g.lifeSequence[0]=Motion::Knead;g.lifeSequenceCount=1;
        Speak(event==Event::FirstWeek ? L"우리 만난 지 일주일 ♡" : event==Event::FirstYear ? L"함께한 첫 일 년 ♡" :
            event==Event::FifthYear ? L"벌써 오 년…\n내 집사는 너야 ♡" : L"오늘은 내 생일이랴 ♡",4200);break;
    case Event::None:return false;
    }
    return true;
}

bool MaybeLifeEvent(ULONGLONG ticks, bool decisionBoundary) {
    if(!g.life.enabled() || ticks<g.nextLifeEventAttempt)return false;
    const auto context=LifeEventContext(ticks,decisionBoundary);
    const bool open=CanStartAutonomousStory(ticks);
    if(!open && (!decisionBoundary || ticks<g.nextDecision ||
            (context.trigger==cat::life::Trigger::None && !context.sleeping)))return false;
    const auto event=cat::life::PeekEvent(g.life.state,context);
    if(event==cat::life::Event::None)return false;
    if(!open && event!=cat::life::Event::SleepTalk && event!=cat::life::Event::PunchStartle &&
            event!=cat::life::Event::TailStartle && event!=cat::life::Event::SneezeStartle)return false;
    const auto prior=g.life.state;
    if(!cat::life::CommitEvent(g.life.state,context,event))return false;
    if(!SaveLife()) {g.life.state=prior;g.nextLifeEventAttempt=ticks+30000;return false;}
    if(DispatchLifeEvent(event,ticks))return true;
    // A moving ledge can invalidate the final physics acceptance. Restore the
    // opportunity rather than spending the pet's daily event on a failed grip.
    g.life.state=prior;SaveLife(false);g.nextLifeEventAttempt=ticks+30000;return false;
}

void UpdateLifeEvent(ULONGLONG ticks, double elapsed) {
    if(g.physics.Hanging() && ticks>=g.lifeHangUntil) {
        g.physics.EndHang(true);g.lifeHangUntil=0;
        g.airborneMotion=Motion::Jump;SetMotion(Motion::Startled,2000);
    }
    if(!g.lifeCrossing.Active())return;
    if(g.lifeCrossing.Advance(elapsed*1000,true)) {
        const auto bounds=MovementBoundsLogical();
        const bool fromLeft=g.physics.CurrentPose().x<(bounds.first+bounds.second)/2;
        g.centerX=g.work.left+(fromLeft ? bounds.second : bounds.first)*g.dpiScale;
        g.direction=fromLeft ? -1 : 1;
        ResetMovement(); // Explicit rare event at opacity zero, on the floor.
    }
    if(!g.lifeCrossing.Active()) {g.speed=45*g.dpiScale;SetMotion(Motion::Walk,2400);}
}

void DispatchAutonomousMotion(ULONGLONG ticks) {
    if(EnvironmentBusy() || g.lifeCrossing.Active() || g.waitingForStop || g.approachingToy ||
            g.approachingUser || g.toyEngaged || g.pendingCare>=0 || g.perch.transitioning ||
            g.physics.Hanging() || ticks<g.nextDecision || !g.physics.CurrentPose().grounded ||
            g.autonomousClaimedAt==ticks || (g.companionReaction && g.companionModel.Busy()))return;
    g.autonomousDispatching=true;g.autonomousTick=ticks;
    const auto choose=[&] {
        if(!g.autonomousResting) {
            if(ContinueLifeSequence() || ContinueMemorySequence())return;
            if(MaybeLifeEvent(ticks,true))return; // Matching completed-act reactions only.
            if(g.brain && g.brain->HasContinuation()) {DecideMotion();return;}
            if(g.motion==Motion::Sleep) {SetMotion(Motion::Stretch,3500);return;}
            BeginAutonomousRest();return;
        }
        if(!CanStartAutonomousStory(ticks))return;
        const auto before=g.motionSerial;
        MaybeVisitWindow(ticks);
        if(g.motionSerial!=before)return;
        if(MaybeLifeEvent(ticks,true))return;
        if(MaybeMemoryRoutine(ticks)) {
            if(g.motionSerial==before)BeginAutonomousRest();
            return;
        }
        if(MaybePlayWithToy(ticks))return;
        DecideMotion();
    };
    choose();g.autonomousDispatching=false;
}

void Tick() {
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
        if (g.lifeHangUntil) g.lifeHangUntil += frozen;
        g.physics.Update(0, true);
        PauseCursorReaction(clockDelta);
        g.poseTransition.Pause(clockDelta);
        PausePrimaryCarePresentation(clockDelta);
        elapsed = 0;
        // A paused body resumes the same stride. Actual drag/teleport paths
        // already reset gait through ResetMovement().

    }
    if (!suspended && elapsed>0 && g.pet.roamingPaused && g.toyEngaged) {
        // A paused toy keeps its contact/pose clock aligned with Session time.
        // The physics update below still brakes any remaining walking speed.
        const auto frozen=std::min(clockDelta,ticks>=g.motionStart ? ticks-g.motionStart : 0);
        g.motionStart+=frozen; g.nextDecision+=frozen;
    }
    g.prank.Tick(ticks);
    if (ticks >= g.nextMaintenance) {
        KeepOnWorkArea(); SyncNutrition(UtcNow()); UpdateTrayTip(); g.nextMaintenance = ticks + 1500;
        if (!g.hidden) SetWindowPos(g.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    TickCompanion(ticks, elapsed);
    if (ticks >= g.nextSave) { SavePet(); g.nextSave = ticks + 60000; }
    RefreshAnimationTimer();
    ObserveLife(ticks);
    if (!suspended) {
        UpdateLifeEvent(ticks,elapsed);
        AdvanceToyObjects(elapsed);
        AdvanceAutomaticToy(elapsed);
        MaintainPerch(ticks);
        if (g.perch.transitioning) {
            UpdatePerchTransition(ticks, elapsed);
            Render();
            return;
        }
        if(elapsed>0)UpdateCursorReaction(ticks, clockDelta);
        if(elapsed>0)DispatchAutonomousMotion(ticks);
        if (g.perch.transitioning) { UpdatePerchTransition(ticks, elapsed); Render(); return; }
        cat::motion::AdvanceStopGate(g.waitingForStop, g.physics.CurrentPose().grounded,
            WalkingVelocity(g.physics.CurrentPose()), ticks, g.motionStart, g.nextDecision);
        const auto actionElapsed = ticks >= g.motionStart ? ticks - g.motionStart : 0;
        const auto body = cat::nutrition::VisualModifiers(g.nutrition);
        const double growth = cat::GrowthScale(g.pet.bornUtc, UtcNow());
        const bool pounceLanded = g.motion == Motion::Pounce && g.jumpLaunched && g.physics.CurrentPose().grounded;
        double targetSpeed = cat::animation::Travels(g.motion) && !g.pet.roamingPaused && !pounceLanded ?
            g.direction * g.speed / g.dpiScale * body.speed * cat::animation::TravelFactor(g.motion, actionElapsed) : 0;
        if (EnvironmentBusy()) targetSpeed = CursorReactionVelocity() * body.speed;
        if (g.lifeCrossing.Active() || g.physics.Hanging()) targetSpeed=0;
        if (g.approachingUser) {
            POINT cursor{};
            const auto [low, high] = MovementBoundsLogical();
            g.userTargetX = std::clamp(g.userTargetX, low, high);
            const double distance = g.userTargetX - (g.centerX - g.work.left) / g.dpiScale;
            const bool gone = !WorkInterferenceEnabled() || !CursorNear(&cursor) ||
                std::abs((cursor.x - g.work.left) / g.dpiScale - g.userTargetX) > 150;
            if (g.pet.roamingPaused || gone || ticks >= g.nextDecision ||
                (std::abs(distance) <= 2 && std::abs(WalkingVelocity(g.physics.CurrentPose())) <= 3)) {
                SetMotion(Motion::Watch, 5000); targetSpeed = 0;
            } else {
                targetSpeed = cat::toy::ApproachVelocity(distance, 18 * body.speed);
                if (targetSpeed != 0) g.direction = targetSpeed < 0 ? -1 : 1;
            }
        }
        if (g.toy==ToyKind::None) { g.toyEngaged=false; g.approachingToy=false; }
        else if (!g.toyEngaged && ticks>=g.nextToyAction) MaybePlayWithToy(ticks);
        if (g.toyEngaged) {
            const bool available=ToyInteractionAvailable();
            const auto toyBounds=MovementBoundsLogical();
            const double toyCenter=g.toyX/g.dpiScale+(g.toy==ToyKind::FishingRod ? g.toyLure.offset : 0);
            const auto distanceToTarget=[&]() {
                return cat::toy::SessionTarget(g.toySession,toyCenter,g.centerX/g.dpiScale,growth,
                    g.work.left/g.dpiScale+toyBounds.first,g.work.left/g.dpiScale+toyBounds.second)-g.centerX/g.dpiScale;
            };
            if (available) {
                ApplyToyPhase(ticks);
                const bool arrived=std::abs(distanceToTarget())<=2 &&
                    std::abs(WalkingVelocity(g.physics.CurrentPose()))<=3;
                if (g.toySession.Advance(elapsed,true,arrived)) ApplyToyPhase(ticks);
            }
            targetSpeed=0;
            if (g.toyEngaged && available && cat::toy::Travels(g.toySession.phase)) {
                const double speed=g.toySession.phase==cat::toy::Phase::Enter ? 22 :
                    g.toy==ToyKind::Ball || g.toy==ToyKind::FishingRod ? 58 : 38;
                targetSpeed=cat::toy::ApproachVelocity(distanceToTarget(),speed*body.speed);
                if (targetSpeed!=0) g.direction=targetSpeed<0 ? -1 : 1;
            }
            if (g.toyEngaged && g.toySession.phase==cat::toy::Phase::Interact && g.motion==Motion::ToyPaw) {
                const bool reachableHeight=g.toy!=ToyKind::Ball || g.toyBall.lift<=24*growth;
                if (cat::toy::PawContactAt(ticks>=g.motionStart ? ticks-g.motionStart : 0,g.toyContact,
                        toyCenter-g.centerX/g.dpiScale,55*growth,available && reachableHeight)) {
                    if (g.toy==ToyKind::Ball) g.toyBall.Kick(g.direction,90);
                    else if (g.toy==ToyKind::FishingRod) g.toyLure.Touch(g.direction);
                    if (g.toyRewardPending) { Remember(cat::memory::Event::Play); g.toyRewardPending=false; }
                }
            }
        }
        targetSpeed *= cat::species::ProfileFor(g.preferences.character).movementSpeedMultiplier;
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
            const ULONGLONG takeoff = PrimaryTakeoffMs(g.motion==Motion::Pounce);
            if (actionElapsed >= takeoff) {
                g.jumpPending = false;
                const double speciesJump = cat::species::ProfileFor(g.preferences.character).jumpHeightMultiplier;
                const bool cursorPounce = cursorInteraction.session.Action() == cat::environment::CursorAction::Pounce;
                const bool launched = cursorPounce ? TryCursorPounce(ticks) :
                    g.motion == Motion::Jump ? g.physics.Jump(55 * growth * body.jump * speciesJump) :
                    g.physics.Pounce(g.direction, 30 * growth * body.jump * speciesJump, g.pet.roamingPaused ? 0 : std::abs(targetSpeed));
                if (launched) {
                    g.jumpLaunched = true; g.airborneMotion = g.motion;
                }
            }
        }
        const auto previousRendered = g.physics.RenderPose();
        g.physics.Update(elapsed);
        const auto physical = g.physics.CurrentPose();
        const auto rendered = g.physics.RenderPose();
        UpdateCursorContact(ticks, physical);
        g.centerX = g.work.left + rendered.x * g.dpiScale;
        if (g.perch.active && physical.grounded && !physical.onPlatform) {
            // A moved ledge can be missed during an ordinary jump. A floor
            // contact must not leave the cat constrained to that old window.
            g.perch.landingOnWindow = false;
            FinishPerchTransition(ticks);
        }
        if (std::abs(WalkingVelocity(physical)) > 2) g.facing = WalkingVelocity(physical) < 0 ? -1 : 1;
        else g.facing = g.direction;
        if (g.motion == Motion::Walk || g.motion == Motion::Stalk || g.motion == Motion::Zoomies) g.walkingMotion = g.motion;
        const Motion gaitMotion=cat::motion::DisplayedMotion(g.motion,g.walkingMotion,
            rendered.grounded,g.waitingForStop,WalkingVelocity(rendered));
        ConfigurePrimaryAuthoredStride();
        g.gait.Advance(gaitMotion, (rendered.relativeTravelX-previousRendered.relativeTravelX) / growth, elapsed, rendered.grounded);
        if (physical.landed) {
            g.landedAt = ticks;
            g.landingStrength = std::clamp(physical.landingSpeed / 230.0, .35, 1.0);
            if(g.motion==Motion::Jump || g.motion==Motion::Pounce) {
                if(const auto* jump=LoadedCareSprite(cat::sprite_sequence::Clip::Jump,g.preferences.character))
                    g.nextDecision=std::max<ULONGLONG>(g.nextDecision,ticks+
                        cat::sprite_sequence::Duration(jump->layout->program,jump->layout->program.outro));
            }
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
            int hits=cat::animation::PunchHits(actionElapsed);
            bool contact=cat::animation::PunchContact(actionElapsed);
            double contactHeight=74;
            if(const auto* punch=LoadedCareSprite(cat::sprite_sequence::Clip::Punch,g.preferences.character);
                    punch && punch->layout->program.stepCount) {
                cat::sprite_sequence::Options options;options.sheetFrames=punch->layout->frameCount;
                options.durationMs=g.nextDecision-g.motionStart;
                const auto events=g.punchCues.Consume(cat::sprite_sequence::Clip::Punch,
                    punch->layout->program,actionElapsed,options);
                hits=g.punchHits+static_cast<int>(events.contactCount);
                contact=cat::sprite_sequence::ProgramContactActive(cat::sprite_sequence::Clip::Punch,
                    punch->layout->program,actionElapsed,options);
                contactHeight=38.89;
            }
            if (hits > g.punchHits) {
                // Consume skipped contacts after a menu or scheduler stall; don't crack
                // the screen later while the cat is already sitting innocently again.
                g.punchHits = hits;
                if (contact) {
                    const double bodyScale = g.dpiScale * cat::GrowthScale(g.pet.bornUtc, UtcNow());
                    const auto body = cat::nutrition::VisualModifiers(g.nutrition);
                    POINT paw{static_cast<LONG>(std::round(g.centerX + g.direction * 38 * bodyScale * body.bodyWidth)),
                              static_cast<LONG>(std::round(CurrentGroundY(ticks) - contactHeight * bodyScale * body.bodyHeight))};
                    g.prank.Trigger(paw, g.work, g.dpiScale);
                }
            }
            if (actionElapsed >= 1000 && g.bubbleUntil < g.motionStart) Speak(L"…나 아니야", 1300);
        }
        if (g.pendingCare >= 0 && physical.grounded) ResumePendingCare();
        MaybeAnnounceUpdate(ticks);
        if (!g.waitingForStop && !EnvironmentBusy()) {
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
        if(g.lifeCrossing.Opacity()<.15)return HTTRANSPARENT;
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
        GetCursorPos(&g.dragStart); g.dragOffset = g.centerX - g.dragStart.x;
        if (g.brain) g.brain->Interrupt(GetTickCount64());
        g.approachingToy = g.approachingUser = false; g.memorySequenceCount = 0;
        g.lifeSequenceCount=0;g.smallBoxEvent=false;g.lifeCrossing={};
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
        Speak(L"나 여기 있어 ♡", 3000); Render(); return 0;
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
    case WM_QUERYENDSESSION: SavePet(false); return TRUE;
    case WM_ENDSESSION: if (wParam) { SavePet(false); } return 0;
    case WM_CLOSE: if (!g.closing) { g.closing = true; updates.cancel.store(true); SavePet(false); DestroyWindow(window); } return 0;
    case WM_DESTROY:
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

#include "native_expanded_tests.hpp"
#include "native_care_sprite_tests.hpp"
#include "native_care_cache_tests.hpp"
#include "native_care_presentation_tests.hpp"

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
            if(const int spriteTest=NativeCareSpriteRenderTest(context))return spriteTest;
            if(const int cacheTest=NativeCareCacheCapacityTest(context))return cacheTest;
            if(const int actualSpriteTest=NativeActualCareSpriteRenderTest(context))return actualSpriteTest;
            if(const int presentationTest=NativeCarePresentationRenderTest(context))return presentationTest;
            if(const int walkingTest=NativeAuthoredWalkingRenderTest(context))return walkingTest;
            DisabledCareSpriteScope canonicalArtwork;
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
                            } else if (coveredBody * 100 < bodyOpaque * 95 || snapshot()!=body) return 37;
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
            const int expanded = NativeExpandedRenderTest(context);
            if (expanded) return expanded;
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

int NativeLifeHostTest() {
    // Exercise the actual host dispatch with an isolated in-memory cat. No
    // files, windows, audio devices, wall-clock aging or update requests.
    const auto now=UtcNow();
    g.pet=cat::NewPet(now-6*366*cat::life::Day);
    g.pet.updatedUtc=now;g.nutrition=cat::nutrition::NewState(g.pet.bornUtc,now,80);
    const auto savedPet=cat::Serialize(g.pet);
    g.brain=std::make_unique<cat::behavior::Brain>(g.pet.bornUtc,42);
    const std::wstring legacyPersonality=cat::behavior::ArchetypeName(g.brain->GetArchetype());
    g.life.state=cat::life::FromLegacy(g.pet,now);g.life.outcome=cat::life::storage::Outcome::Migrated;
    g.memory.state=cat::memory::FromLegacy(g.pet,now);g.memory.state.bond=80;
    g.memory.outcome=cat::memory::storage::Outcome::Migrated;
    g.work={0,0,1200,900};g.dpiScale=1;g.centerX=96;ResetMovement();
    const auto ticks=GetTickCount64();
    using Event=cat::life::Event;
    for(const auto entry:{std::pair<Event,Motion>{Event::PunchStartle,Motion::Startled},
        {Event::TailStartle,Motion::Startled},{Event::SneezeStartle,Motion::Startled},
        {Event::Yawn,Motion::Yawn},{Event::SleepTalk,Motion::SleepTalk},{Event::Peek,Motion::Peek},
        {Event::SmallBox,Motion::BoxSit},{Event::CrownPride,Motion::Pride},
        {Event::BondKnead,Motion::Knead},{Event::BondGreeting,Motion::TailRaise},
        {Event::BondLifetime,Motion::Stretch},{Event::Birthday,Motion::TailRaise},
        {Event::FirstWeek,Motion::TailRaise},{Event::FirstYear,Motion::TailRaise},{Event::FifthYear,Motion::TailRaise}}) {
        if(!DispatchLifeEvent(entry.first,ticks)||g.motion!=entry.second)return 160;
        if(cat::Serialize(g.pet)!=savedPet)return 161;
    }
    DispatchLifeEvent(Event::BondLifetime,ticks);
    for(const auto expected:{Motion::Knead,Motion::Loaf,Motion::Sleep})
        if(!ContinueLifeSequence()||g.motion!=expected)return 162;
    if(ContinueLifeSequence())return 163;
    DispatchLifeEvent(Event::SmallBox,ticks);
    if(!g.smallBoxEvent)return 164;
    SetMotion(Motion::Watch,2000);
    if(g.smallBoxEvent||g.lifeSequenceCount)return 165;
    DispatchLifeEvent(Event::ScreenCrossing,ticks);
    // A nearby companion must not replace Watch while this rare effect is
    // fading; its normal social callback would otherwise cancel the crossing.
    if(!CompanionContext(ticks,0).primaryBusy)return 175;
    for(int i=1;i<=12;++i)UpdateLifeEvent(ticks+i*100,.1);
    if(g.lifeCrossing.Active()||g.lifeCrossing.Opacity()!=1||g.physics.CurrentPose().x<1100 ||
        !g.physics.CurrentPose().grounded||g.physics.CurrentPose().lift>.1)return 166;
    g.centerX=500;ResetMovement();
    if(!g.physics.LaunchToPlatform(300,900,400,370,45))return 167;
    for(int i=0;i<600&&!g.physics.OnPlatform();++i)g.physics.Update(1.0/60);
    if(!g.physics.OnPlatform())return 168;
    g.perch.active=true;g.perch.rect={300,500,900,850};
    if(!DispatchLifeEvent(Event::EdgeHang,ticks)||!g.physics.Hanging()||g.motion!=Motion::Hang)return 169;
    for(int i=0;i<80;++i)g.physics.Update(1.0/60);
    if(!g.physics.CurrentPose().hanging)return 170;
    UpdateLifeEvent(ticks+1500,.016);
    if(g.physics.Hanging())return 171;
    g.life.state.temperament=cat::life::Temperament::Coward;
    if(std::wstring(PrimaryPersonalityLabel())!=L"겁쟁이")return 172;
    g.life.state.temperament=cat::life::Temperament::Legacy;
    if(std::wstring(PrimaryPersonalityLabel())!=legacyPersonality)return 173;
    if(cat::Serialize(g.pet)!=savedPet)return 174;
    return 0;
}

int NativeToyHostTest() {
    // This runs only from --self-test, after other in-memory host fixtures.
    // No application window, AppData loader, audio startup, save or update path.
    g.initialized=false; g.window=nullptr; g.hidden=g.menuOpen=g.dragging=g.closing=false;
    g.memory.outcome=cat::memory::storage::Outcome::Blocked;
    g.brain.reset();
    g.lifeSequenceCount=0; g.lifeCrossing={}; g.smallBoxEvent=false;
    g.lifeDispatching=false; g.toyDispatching=false;
    const auto now=UtcNow();
    g.pet=cat::NewPet(now-366*cat::life::Day);
    g.pet.updatedUtc=now;
    g.nutrition=cat::nutrition::NewState(g.pet.bornUtc,now,80);
    g.work={0,0,1280,900}; g.dpiScale=1; g.centerX=400; g.direction=g.facing=1;
    g.toy=ToyKind::None; g.toyEngaged=false; g.toySession.Cancel();
    ResetMovement();
    const auto birthdayRecord=cat::Serialize(g.pet);

    ULONG_PTR token=0;
    GdiplusStartupInput input;
    if (GdiplusStartup(&token,&input,nullptr)!=Ok) return 190;
    int result=0;
    {
        Surface canvas;
        result=[&]() -> int {
            if (!canvas.Create(320,220)) return 191;
            const auto painted=[&](bool foreground) {
                Graphics graphics(canvas.image.get());
                graphics.SetCompositingMode(CompositingModeSourceCopy); graphics.Clear(Color(0,0,0,0));
                graphics.SetCompositingMode(CompositingModeSourceOver);
                if (foreground) DrawToyForeground(graphics,160,190);
                else DrawToyBackground(graphics,160,190,GetTickCount64());
                graphics.Flush(FlushIntentionSync);
                unsigned count=0;
                for (std::size_t pixel=0;pixel<320*220;++pixel) count+=canvas.pixels[pixel*4+3]>16;
                return count;
            };
            const auto walkTo=[&](double target) {
                for (unsigned frame=0;frame<1200;++frame) {
                    const auto pose=g.physics.CurrentPose();
                    const double distance=target-g.centerX;
                    if (std::abs(distance)<=2 && std::abs(WalkingVelocity(pose))<=3) {
                        g.physics.SetTargetVelocity(0);
                        for (int settle=0;settle<20;++settle) g.physics.Update(1.0/60);
                        g.centerX=g.physics.RenderPose().x;
                        return true;
                    }
                    g.physics.SetTargetVelocity(cat::toy::ApproachVelocity(distance,38));
                    g.physics.Update(1.0/60);
                    g.centerX=g.physics.RenderPose().x;
                }
                return false;
            };
            const auto before=g.physics.CurrentPose().x;
            PlaceToy(ToyKind::Box);
            if (g.toy!=ToyKind::Box || !g.toyEngaged || g.toySession.phase!=cat::toy::Phase::Discover ||
                    g.physics.CurrentPose().x!=before || !MaybePlayWithToy(GetTickCount64())) return 192;
            g.toySession.Set(cat::toy::Phase::Approach); ApplyToyPhase(GetTickCount64());
            if (g.motion!=Motion::Walk || !g.approachingToy) return 193;
            const auto bounds=MovementBoundsLogical();
            if (!walkTo(cat::toy::SessionTarget(g.toySession,g.toyX,g.centerX,1,bounds.first,bounds.second))) return 194;
            g.toySession.Set(cat::toy::Phase::Enter); ApplyToyPhase(GetTickCount64());
            if (g.motion!=Motion::Stalk || !walkTo(g.toyX)) return 195;
            g.toySession.Set(cat::toy::Phase::Sleep); ApplyToyPhase(GetTickCount64());
            if (g.motion!=Motion::Sleep || !g.toyEngaged || painted(true)<1000) return 196;
            g.toySession.Set(cat::toy::Phase::Exit); ApplyToyPhase(GetTickCount64());
            if (g.motion!=Motion::Walk || painted(true)<1000) return 197;
            g.perch.active=true;
            if (painted(true)!=0) return 198;
            g.perch.active=false;

            SetMotion(Motion::Eat,2100);
            if (g.toyEngaged || g.approachingToy || g.nextToyAction<g.motionStart+3600 ||
                    MaybePlayWithToy(g.motionStart+300)) return 199;
            g.pet.roamingPaused=true;
            PlaceToy(ToyKind::Ball);
            if (g.toyEngaged || MaybePlayWithToy(GetTickCount64())) return 200;
            const auto ballX=g.toyBall.x,ballVelocity=g.toyBall.velocity;
            AdvanceToyObjects(.1);
            if (g.toyBall.x!=ballX || g.toyBall.velocity!=ballVelocity) return 201;
            g.pet.roamingPaused=false;

            for (const auto entry:{std::pair<ToyKind,Motion>{ToyKind::PaperBag,Motion::BoxSit},
                    {ToyKind::Catnip,Motion::BellyRoll},{ToyKind::FishingRod,Motion::ToyPaw},
                    {ToyKind::Cushion,Motion::Knead}}) {
                PlaceToy(entry.first);
                if (!g.toyEngaged || painted(false)<100) return 202;
                g.toySession.Set(cat::toy::Phase::Interact); ApplyToyPhase(GetTickCount64());
                if (g.motion!=entry.second) return 203;
            }
            PlaceToy(ToyKind::None);
            if (g.toy!=ToyKind::None || g.toyEngaged || g.approachingToy || g.toyWindow ||
                    cat::Serialize(g.pet)!=birthdayRecord) return 204;

            // Drive the real automatic host hook, with all persistence still
            // blocked. Remember() changes this interaction clock even when
            // memory writes are blocked, so it detects accidental care credit.
            g.automaticToy={}; g.random.seed(42);
            g.approachingUser=false; g.memorySequenceCount=0;
            cursorInteraction={};
            SetMotion(Motion::Watch,5000);
            const auto interactionClock=g.lifeInteractionUntil;
            const auto memoryBefore=cat::memory::Serialize(g.memory.state);
            const auto positionBefore=g.physics.CurrentPose().x;
            g.pet.roamingPaused=true;
            for (unsigned step=0;step<1000;++step) AdvanceAutomaticToy(.25);
            if (g.toy!=ToyKind::None || g.automaticToy.RemainingSeconds()!=0) return 205;
            g.pet.roamingPaused=false;
            AdvanceAutomaticToy(7200); // Resuming the computer must not replay offline events.
            if (g.toy!=ToyKind::None || g.automaticToy.RemainingSeconds()!=0) return 205;
            g.memorySequenceCount=1;
            for (unsigned step=0;step<1000;++step) AdvanceAutomaticToy(.25);
            if (g.toy!=ToyKind::None || g.automaticToy.RemainingSeconds()!=0) return 206;
            g.memorySequenceCount=0;
            // The item countdown is due, but an uncompleted quiet action must
            // still keep this independent owner from replacing its pose.
            AdvanceAutomaticToy(.25);
            if(g.toy!=ToyKind::None)return 216;
            g.autonomousResting=true;g.nextDecision=GetTickCount64();
            g.autonomousClaimedAt=std::numeric_limits<ULONGLONG>::max();
            AdvanceAutomaticToy(.25);
            if (g.toy==ToyKind::None || g.automaticToy.ActiveKind()!=g.toy || !g.toyEngaged ||
                    g.toyRewardPending || g.physics.CurrentPose().x!=positionBefore) return 207;
            g.toySession.Set(cat::toy::Phase::Interact); ApplyToyPhase(GetTickCount64());
            if (g.toyRewardPending || g.lifeInteractionUntil!=interactionClock ||
                    cat::memory::Serialize(g.memory.state)!=memoryBefore ||
                    cat::Serialize(g.pet)!=birthdayRecord) return 208;
            g.toySession.Set(cat::toy::Phase::Wait); ApplyToyPhase(GetTickCount64());
            if (MaybePlayWithToy(GetTickCount64()+60000)) return 209;
            SetMotion(Motion::Eat,2100); // Cleanup must leave this newer care action alone.
            const auto careStart=g.motionStart,careEnd=g.nextDecision;
            AdvanceAutomaticToy(.25);
            if (g.toy!=ToyKind::None || g.toyEngaged || g.approachingToy ||
                    g.motion!=Motion::Eat || g.motionStart!=careStart || g.nextDecision!=careEnd ||
                    g.automaticToy.ActiveKind()!=ToyKind::None || g.automaticToy.RemainingSeconds()<180 ||
                    g.physics.CurrentPose().x!=positionBefore || cat::Serialize(g.pet)!=birthdayRecord) return 210;

            // A paused item must yield on resumption as well as first placement.
            PlaceToy(ToyKind::Box); SetMotion(Motion::Watch,5000); g.nextToyAction=0;
            g.autonomousResting=true;g.nextDecision=GetTickCount64();
            g.autonomousClaimedAt=std::numeric_limits<ULONGLONG>::max();
            const auto ownerStart=g.motionStart;
            const auto resumeBlocked=[&] {
                return !MaybePlayWithToy(GetTickCount64()) && !g.toyEngaged && g.motionStart==ownerStart;
            };
            g.memorySequenceCount=1;
            if (!resumeBlocked()) return 211;
            g.memorySequenceCount=0;
            cat::environment::CursorInput cursor;
            cursor.nowMs=GetTickCount64(); cursor.catX=g.physics.CurrentPose().x;
            cursor.cursorX=cursor.catX+30; cursor.cursorHeight=40;
            cursor.minX=96; cursor.maxX=1184;
            cursor.available=cursor.enabled=cursor.canStart=cursor.canContinue=true;
            cursorInteraction.session.Step(cursor,16,{true,false,ownerStart});
            if (!EnvironmentBusy() || !resumeBlocked()) return 212;
            cursorInteraction={};
            g.approachingUser=true;
            if (!resumeBlocked()) return 213;
            g.approachingUser=false; g.pendingCare=0;
            if (!resumeBlocked()) return 213;
            g.pendingCare=-1;
            g.companionStore.state=cat::companion::NewState(now);
            g.companionModel.Bind(g.companionStore.state);
            g.companionModel.Reset(96,1184,740);
            cat::companion::Context social;
            social.nowUtc=now; social.ticks=GetTickCount64(); social.primaryVisible=true;
            social.primary=g.physics.CurrentPose(); social.primaryMotion=Motion::Watch;
            if (!g.companionModel.TrySocial(cat::companion::Interaction::Follow,social) ||
                    !g.companionModel.Busy() || !resumeBlocked()) return 214;
            g.companionStore.state.enabled=false;
            if (!MaybePlayWithToy(GetTickCount64()) || !g.toyEngaged) return 215;
            // An unrelated autonomous story may start only after a complete
            // neutral interval; repeated polling cannot shorten it. Explicit
            // care can still replace that interval immediately.
            PlaceToy(ToyKind::None);
            SetMotion(Motion::Groom,6000);
            g.waitingForStop=false;
            const auto pacedStart=g.motionStart,pacedSerial=g.motionSerial;
            for(const auto offset:{0ULL,1000ULL,5999ULL}) {
                MaybeVisitWindow(pacedStart+offset);
                MaybeMemoryRoutine(pacedStart+offset);
                MaybePlayWithToy(pacedStart+offset);
                if(g.motionSerial!=pacedSerial || g.motion!=Motion::Groom)return 217;
            }
            const auto finished=GetTickCount64();
            g.motionStart=finished>=6000?finished-6000:0;g.nextDecision=finished;
            DispatchAutonomousMotion(finished);
            if(g.motion!=Motion::Idle || !g.autonomousResting ||
                    g.nextDecision-g.motionStart<18000 || g.nextDecision-g.motionStart>45000)return 218;
            const auto restSerial=g.motionSerial,restDeadline=g.nextDecision;
            DispatchAutonomousMotion(finished);DispatchAutonomousMotion(finished+1);
            if(g.motionSerial!=restSerial || g.nextDecision!=restDeadline)return 219;
            SetMotion(Motion::Eat,3300);
            if(g.motion!=Motion::Eat || g.autonomousResting || g.motionSerial==restSerial)return 219;

            // Join a real running primary through the production host context.
            // Matching its story must not restart the primary's run or permit
            // an unrelated social gesture to replace unfinished care.
            SetMotion(Motion::Zoomies,5000);
            g.physics.SetTargetVelocity(70);
            for(unsigned frame=0;frame<30;++frame)g.physics.Update(.016);
            g.companionStore.state=cat::companion::NewState(now);
            g.companionStore.state.pet.position=std::clamp(
                (g.physics.CurrentPose().x-60-96)/(1184.0-96),0.0,1.0);
            g.companionModel.Bind(g.companionStore.state);g.companionModel.Reset(96,1184,740);
            const auto runSerial=g.motionSerial;
            auto paired=CompanionContext(GetTickCount64(),.016);
            if(paired.primaryBusy || paired.primaryMotion!=Motion::Zoomies)return 235;
            if(!g.companionModel.TrySocial(cat::companion::Interaction::RunTogether,paired))return 236;
            for(unsigned frame=0;frame<30;++frame) {
                g.physics.Update(.016);paired=CompanionContext(paired.ticks+16,.016);
                g.companionModel.Tick(paired);
            }
            if(g.companionModel.Physical().velocityX<30 || g.companionStore.state.mutualPlay!=1 ||
                    g.motionSerial!=runSerial || cat::Serialize(g.pet)!=birthdayRecord)return 237;
            g.physics.SetTargetVelocity(0);
            for(unsigned frame=0;frame<80;++frame)g.physics.Update(.016);
            for(const auto protectedMotion:{Motion::Eat,Motion::Treat,Motion::Groom,Motion::Punch}) {
                SetMotion(protectedMotion,5000);
                if(g.waitingForStop || !CompanionContext(GetTickCount64(),.016).primaryBusy)return 238;
            }
            SetMotion(Motion::Zoomies,5000);g.lifeSequenceCount=1;
            if(!CompanionContext(GetTickCount64(),.016).primaryBusy)return 238;
            g.lifeSequenceCount=0;
            return 0;
        }();
    }
    GdiplusShutdown(token);
    g.toy=ToyKind::None; g.toyEngaged=false; g.approachingToy=false; g.toySession.Cancel();
    g.automaticToy={}; g.memorySequenceCount=0;
    cursorInteraction={}; g.approachingUser=false; g.pendingCare=-1;
    g.companionStore.state.enabled=false;
    g.pet.roamingPaused=false; g.perch={}; g.physics.SetTargetVelocity(0);
    return result;
}

// Inspect actual Win32 menus, including nested radio/check/disabled state.
// The fixture runs only in --self-test, before LoadPet or any application files.
int NativeMenuContractTest() {
    static_assert(FeedCommand == 1010 && SnackCommand == 1011 && StatusCommand == 1012 &&
        PauseCommand == 1013 && HideCommand == 1014 && BackupCommand == 1015 &&
        FolderCommand == 1016 && QuitCommand == 1017 && CreamCommand == 1020 &&
        SilverCommand == 1021 && UpdateCheckCommand == 1024 && UpdateViewCommand == 1025 &&
        UpdateAutomaticCommand == 1026 && OutfitNoneCommand == 1032 &&
        OutfitBowCommand == 1033 && OutfitScarfCommand == 1034 && OutfitCrownCommand == 1035,
        "Retired command IDs must not renumber the remaining commands");
    static_assert(kFishCommand==2302 && kCompanionFish==2405 &&
        static_cast<int>(cat::feeding::FoodKind::Meal)==0 &&
        static_cast<int>(cat::feeding::FoodKind::Treat)==1 &&
        static_cast<int>(cat::feeding::FoodKind::Fish)==2,
        "Deferred food kinds and new IDs must not reuse retired commands");
    // Old installed versions used these IDs for manual motions, sound and toys.
    constexpr UINT retired[]{1018, 1019, 1022, 1023, 1027, 1028, 1029, 1030, 1031,
        1032, 1033, 1034, 1035, 1036, 1037, 1038, 1039, 1040, 1041, 1042, 1043, 1044, 1045};
    struct Item { UINT id, state, type; };
    const auto collect = [](auto&& self, HMENU menu, std::vector<Item>& items) -> bool {
        const int count = GetMenuItemCount(menu);
        if (count < 0) return false;
        for (int index = 0; index < count; ++index) {
            MENUITEMINFOW item{}; item.cbSize = sizeof(item);
            item.fMask = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU;
            if (!GetMenuItemInfoW(menu, index, TRUE, &item)) return false;
            if (item.hSubMenu) { if (!self(self, item.hSubMenu, items)) return false; }
            else if (!(item.fType & MFT_SEPARATOR) && item.wID != 0)
                items.push_back({item.wID, item.fState, item.fType});
        }
        return true;
    };
    const auto now = UtcNow();
    g.window = nullptr; g.initialized = true; g.closing = false;
    g.hidden = false; g.menuOpen = false; g.dragging = false;
    g.pet = cat::NewPet(now - 86400); g.pet.updatedUtc = now;
    g.nutrition = cat::nutrition::NewState(g.pet.bornUtc, now, 65);
    g.preferences = {cat::Character::SilverTabby, cat::Outfit::Scarf};
    g.life.outcome = cat::life::storage::Outcome::Blocked;
    g.memory.outcome = cat::memory::storage::Outcome::Blocked;
    g.companionStore.state = cat::companion::NewState(now);
    g.companionStore.outcome = cat::companion::Outcome::Adopted;
    updates.applying = false;
    for (int variant = 0; variant < 3; ++variant) {
        const bool enabled = variant == 0;
        g.pet.roamingPaused = enabled; g.workInterference = enabled;
        g.interactionSettings.seasonalEvents = enabled;
        g.companionStore.state.enabled = enabled;
        if (variant == 2) g.companionStore.outcome = cat::companion::Outcome::Missing;
        updates.operation = enabled ? UpdateState::Operation::None : UpdateState::Operation::Checking;
        updates.automatic = enabled; updates.hasUpdate = enabled;
        updates.feed = enabled ? "https://example.invalid/latest.txt" : "";
        updates.available.versionText = "2.0.2";
        HMENU menu = BuildPetMenu();
        if (!menu) return 220;
        std::vector<Item> items;
        const bool read = collect(collect, menu, items);
        DestroyMenu(menu);
        if (!read) return 221;
        std::vector<UINT> actual, expected{1010, 1011, 1012, 1013, 1014, 1015, 1016, 1017,
            1024, 1026, 2300, 2301, 2302, 2400};
        if (enabled) expected.push_back(1025);
        if (variant != 2) for (UINT id : {2401u, 2402u, 2403u, 2405u}) expected.push_back(id);
        for (int i = 0; i < cat::kCharacterCount; ++i) expected.push_back(2500 + i);
        for (const auto& item : items) actual.push_back(item.id);
        std::sort(actual.begin(), actual.end()); std::sort(expected.begin(), expected.end());
        if (actual != expected) return 222; // Also catches duplicates and removed commands in submenus.
        for (UINT id : retired) if (std::binary_search(actual.begin(), actual.end(), id)) return 223;
        const auto state = [&](UINT id) {
            for (const auto& item : items) if (item.id == id) return item.state;
            return static_cast<UINT>(-1);
        };
        for (UINT id : {1013u, 2300u, 2301u, 2400u, 1026u})
            if (((state(id) & MFS_CHECKED) != 0) != enabled) return 224;
        for (int i = 0; i < cat::kCharacterCount; ++i)
            if (((state(2500 + i) & MFS_CHECKED) != 0) !=
                    (i == static_cast<int>(g.preferences.character))) return 225;

        for (UINT id : {1024u, 1026u})
            if (((state(id) & MFS_DISABLED) != 0) == enabled) return 226;
        if (variant != 2) for (UINT id : {2401u, 2402u, 2405u})
            if (((state(id) & MFS_DISABLED) != 0) == enabled) return 226;
        for (UINT id : {1010u, 1011u, 1012u, 1013u, 1014u, 1015u, 1016u, 1017u, 2302u, 2400u})
            if (state(id) & MFS_DISABLED) return 226;
    }
    // Both menu locations offer fish to cats only, independently of the
    // other pet's character. Meals and treats remain enabled for all animals.
    g.companionStore.outcome=cat::companion::Outcome::Adopted;
    g.companionStore.state.enabled=true;
    for(int index=0;index<cat::kCharacterCount;++index) {
        g.preferences.character=static_cast<cat::Character>(index);
        g.companionStore.state.preferences.character=static_cast<cat::Character>((index+1)%cat::kCharacterCount);
        HMENU menu=BuildPetMenu();if(!menu)return 229;
        std::vector<Item> items;const bool read=collect(collect,menu,items);DestroyMenu(menu);
        if(!read)return 229;
        for(const auto& item:items) {
            if(item.id==kFishCommand && (((item.state&MFS_DISABLED)==0)!=cat::feeding::CanOfferFish(g.preferences.character)))return 229;
            if(item.id==kCompanionFish && (((item.state&MFS_DISABLED)==0)!=cat::feeding::CanOfferFish(g.companionStore.state.preferences.character)))return 229;
        }
    }
    g.preferences.character=cat::Character::SilverTabby;
    // Exercise the real dispatcher with all normal dispatch preconditions met.
    // No retained command is invoked: those may open dialogs or persist care.
    updates.operation = UpdateState::Operation::None;
    g.pet.roamingPaused = false; g.brain.reset(); g.perch = {};
    g.work = {0, 0, 1200, 900}; g.dpiScale = 1; g.centerX = 600;
    g.physics.Reset(600, 50, 1150); g.physicsReady = true;
    g.motion = Motion::Watch; g.motionStart = 1234; g.nextDecision = 5678; g.fishMeal=false;
    g.pendingCare = -1; g.bubble = L"menu fixture"; g.bubbleUntil = 8765;
    g.toy = ToyKind::Ball; g.toySession.Start(ToyKind::Ball);
    g.approachingToy = false; g.toyEngaged = false;
    const auto snapshot = [] {
        const auto physical = g.physics.CurrentPose();
        std::wostringstream out; out.precision(17);
        out << cat::Serialize(g.pet).c_str() << cat::SerializePreferences(g.preferences).c_str()
            << cat::nutrition::Serialize(g.nutrition).c_str()
            << cat::companion::Serialize(g.companionStore.state).c_str()
            << static_cast<int>(g.motion) << L'|' << g.motionStart << L'|' << g.nextDecision
            << L'|' << g.hidden << L'|' << g.initialized << L'|' << g.closing << L'|' << g.menuOpen
            << L'|' << g.direction << L'|' << g.facing << L'|' << g.speed << L'|' << g.centerX
            << L'|' << g.bubble << L'|' << g.bubbleUntil << L'|' << g.pendingCare << L'|' << g.fishMeal
            << L'|' << static_cast<int>(g.toy) << L'|' << g.toyX << L'|' << g.toyEngaged
            << L'|' << g.approachingToy << L'|' << g.toyContact << L'|' << g.nextToyAction
            << L'|' << static_cast<int>(g.toySession.kind) << L'|' << static_cast<int>(g.toySession.phase)
            << L'|' << g.toySession.revision << L'|' << g.toySession.activeMs
            << L'|' << physical.x << L'|' << physical.lift << L'|' << physical.velocityX
            << L'|' << physical.velocityY << L'|' << physical.grounded << L'|' << physical.onPlatform
            << L'|' << g.jumpPending << L'|' << g.jumpLaunched << L'|' << g.waitingForStop
            << L'|' << g.workInterference << L'|' << g.interactionSettings.seasonalEvents
            << L'|' << static_cast<int>(updates.operation) << L'|' << updates.automatic
            << L'|' << updates.hasUpdate << L'|' << updates.applying;
        return out.str();
    };
    const auto unchanged = snapshot();
    for (UINT id : retired) { RunCommand(id); if (snapshot() != unchanged) return 227; }
    for (int i=0;i<cat::kOutfitCount;++i) { RunCommand(kOutfitCommandBase+i); if(snapshot()!=unchanged)return 227; }
    // A stale Ctrl+Alt+B WM_HOTKEY notification must also have no effect.
    WindowProc(nullptr, WM_HOTKEY, 4, 0);
    if (snapshot() != unchanged) return 228;
    // Exercise the real deferred-care host without writing application files:
    // full cats refuse on landing, so only the queued kind and natural time
    // may change. Fish must stay kind2 rather than being converted to Treat.
    g.nutritionReady=true;g.nutrition.fullness=g.pet.hunger=100;
    g.nextCareReply=std::numeric_limits<ULONGLONG>::max();
    for(const auto kind:{cat::feeding::FoodKind::Meal,cat::feeding::FoodKind::Treat,cat::feeding::FoodKind::Fish}) {
        g.pendingCare=-1;g.perch.transitioning=true;
        const auto petBefore=cat::Serialize(g.pet);
        CareFood(kind);
        if(g.pendingCare!=static_cast<int>(kind) || cat::Serialize(g.pet)!=petBefore)return 230;
        g.perch.transitioning=false;ResumePendingCare();
        if(g.pendingCare!=-1 || g.pet.feedCount || g.pet.snackCount || g.fishMeal)return 230;
    }
    // A character switch while food is queued cannot leave the care gate
    // blocked forever or serve a fish to a dog/rabbit.
    g.pendingCare=static_cast<int>(cat::feeding::FoodKind::Fish);
    g.preferences.character=cat::Character::Rabbit;
    const auto petBefore=cat::Serialize(g.pet);ResumePendingCare();
    if(g.pendingCare!=-1 || cat::Serialize(g.pet)!=petBefore)return 230;
    g.preferences.character=cat::Character::SilverTabby;
    SetMotion(Motion::Eat,3300);g.fishMeal=true;
    CareFood(cat::feeding::FoodKind::Fish);
    if(!g.fishMeal)return 230; // A refusal must not replace an accepted meal.
    SetMotion(Motion::Watch,1000);if(g.fishMeal)return 230;
    g.initialized = false; g.toy = ToyKind::None; g.toySession.Cancel();
    return 0;
}

#include "native_cursor_tests.hpp"
#include "native_perch_tests.hpp"

int SelfTest(HINSTANCE instance) {
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
    // Retired sound resources must never reappear in a silent release.
    for (int id = 301; id <= 314; ++id)
        if (FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA)) return id;
    const int perchResult = NativePerchTransitionTest(instance);
    if (perchResult) return perchResult;
    const int lifeResult = NativeLifeHostTest();
    if (lifeResult) return lifeResult;
    const int toyResult = NativeToyHostTest();
    if (toyResult) return toyResult;
    const int menuResult = NativeMenuContractTest();
    if (menuResult) return menuResult;
    return NativeCursorHostTest(instance);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    if (commandLine && wcsstr(commandLine, L"--render-self-test")) {
        g.instance = instance;
        return RenderSelfTest();
    }
    if (const auto helperResult = cat::update::HandleUpdateHelperCommandLine()) return *helperResult;
    if (commandLine && wcsstr(commandLine, L"--self-test")) return SelfTest(instance);
    g.instance = instance;
    g.instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\YeopjariCat.OneInstance.v1");
    if (!g.instanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g.instanceMutex) CloseHandle(g.instanceMutex);
        if (HWND existing = FindWindowW(kClass, nullptr)) { PostMessageW(existing, kShowExisting, 0, 0); return 0; }
        MessageBoxW(nullptr, L"고양이는 이미 곁에 있어요.\n\nCtrl+Alt+H로 보이게 하거나, 작업표시줄 오른쪽 숨겨진 아이콘에서 고양이를 찾아 주세요.", kTitle, MB_OK | 0); return 0;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!LoadPet()) { ReleaseMutex(g.instanceMutex); CloseHandle(g.instanceMutex); if (SUCCEEDED(com)) CoUninitialize(); return 1; }
    LoadNutrition();
    LoadMemory();
    LoadLife();
    LoadCompanion();
    LoadPreferences();
    LoadInteractionSettings();
    InitializeUpdates();
    g.brain = std::make_unique<cat::behavior::Brain>(g.pet.bornUtc,
        static_cast<std::uint64_t>(g.random()) << 32 | g.random());
    SyncExperience();
    GdiplusStartupInput startup;
    if (GdiplusStartup(&g.gdiplusToken, &startup, nullptr) != Ok || !LoadAtlas()) {
        MessageBoxW(nullptr, L"고양이 그림을 불러오지 못했어요. 실행 파일을 다시 받아 주세요. 성장 기록은 그대로 남아 있어요.", kTitle, 0);
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
    g.updateAnnounceAfter = g.lastTick + 30000;
    g.nextCuriosity = g.lastTick + 20000;
    g.perch.nextTry = g.lastTick + static_cast<ULONGLONG>(Random(30000, 60000));
    SetMotion(Motion::Idle, 5000); Speak(L"안녕. 오늘도 네 옆에 ♡", 4500);
    Render(); ShowWindow(g.window, SW_SHOWNOACTIVATE); RefreshAnimationTimer();
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
    g.companionWindow.Close();
    if (g.toyWindow) DestroyWindow(g.toyWindow);
    g.toySurface.Reset();
    g.prank.Shutdown(); g.surface.Reset(); g.blendFrom.Reset(); g.blendTo.Reset(); ReleaseAtlases();
    if (g.trayIcon) DestroyIcon(g.trayIcon); GdiplusShutdown(g.gdiplusToken);
    ReleaseMutex(g.instanceMutex); CloseHandle(g.instanceMutex); if (SUCCEEDED(com)) CoUninitialize();
    return static_cast<int>(message.wParam);
}
