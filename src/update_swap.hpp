#pragma once
#include <optional>
#include "update_client.hpp"
#include <shellapi.h>

namespace cat::update {
namespace native {
inline std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { quoted.append(slashes * 2 + 1, L'\\'); quoted += c; slashes = 0; continue; }
        quoted.append(slashes, L'\\'); slashes = 0; quoted += c;
    }
    quoted.append(slashes * 2, L'\\'); quoted += L'\"';
    return quoted;
}
inline bool ParseUnsigned(const wchar_t* text, std::uint64_t limit, std::uint64_t& value) {
    if (!text || !*text) return false;
    std::uint64_t result = 0;
    for (; *text; ++text) {
        if (*text < L'0' || *text > L'9') return false;
        const auto digit = static_cast<unsigned>(*text - L'0');
        if (result > limit / 10 || (result == limit / 10 && digit > limit % 10)) return false;
        result = result * 10 + digit;
    }
    value = result; return true;
}
inline bool SameFile(const std::wstring& a, const std::wstring& b) {
    Handle one(CreateFileW(a.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle two(CreateFileW(b.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    BY_HANDLE_FILE_INFORMATION first{}, second{};
    return one && two && GetFileInformationByHandle(one.value, &first) && GetFileInformationByHandle(two.value, &second) &&
        first.dwVolumeSerialNumber == second.dwVolumeSerialNumber && first.nFileIndexHigh == second.nFileIndexHigh &&
        first.nFileIndexLow == second.nFileIndexLow;
}
inline std::wstring ProcessImage(HANDLE process) {
    std::vector<wchar_t> buffer(32768); DWORD size = static_cast<DWORD>(buffer.size());
    return QueryFullProcessImageNameW(process, 0, buffer.data(), &size) ? std::wstring(buffer.data(), size) : std::wstring{};
}
inline bool LaunchProgram(const std::wstring& target) {
    std::wstring command = QuoteArgument(target);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const auto folder = ParentDirectory(target);
    if (!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        folder.c_str(), &startup, &process)) return false;
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return true;
}
inline void HelperMessage(const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), L"옆자리 고양이 · 업데이트", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}
inline bool IsStagePath(const std::wstring& stage) {
    const std::wstring folder = ParentDirectory(stage);
    const auto slash = folder.find_last_of(L"\\/");
    if (slash == std::wstring::npos || stage != folder + L"\\download.exe") return false;
    const auto name = folder.substr(slash + 1);
    if (name.size() != 39 || name.compare(0, 7, L"update-") != 0) return false;
    for (std::size_t i = 7; i < name.size(); ++i)
        if (!((name[i] >= L'0' && name[i] <= L'9') || (name[i] >= L'a' && name[i] <= L'f'))) return false;
    const DWORD attributes = GetFileAttributesW(folder.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
inline bool MatchesHash(const std::wstring& file, const std::string& digest, std::uint64_t size) {
    std::string actual; std::uint64_t length = 0; std::wstring error;
    return FilePresent(file) && HashFile(file, actual, length, error) && actual == digest && length == size;
}
inline bool RestoreOriginal(const std::wstring& target, const std::wstring& backup, const std::string& oldHash,
                            std::uint64_t oldSize, const std::wstring& token) {
    if (MatchesHash(target, oldHash, oldSize)) return true;
    if (!MatchesHash(backup, oldHash, oldSize)) return false;
    const auto temporary = target + L".restore-" + token + L".tmp";
    if (!CopyFileW(backup.c_str(), temporary.c_str(), TRUE)) return false;
    if (!MatchesHash(temporary, oldHash, oldSize) ||
        !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); return false;
    }
    return true;
}

inline int ApplyUpdateHelper(int argc, wchar_t** argv) {
    if (argc != 11) return 30;
    std::uint64_t pid = 0, parentNumber = 0, eventNumber = 0, abortNumber = 0, size = 0;
    if (!ParseUnsigned(argv[2], MAXDWORD, pid) || !pid ||
        !ParseUnsigned(argv[3], UINTPTR_MAX, parentNumber) || !parentNumber ||
        !ParseUnsigned(argv[4], UINTPTR_MAX, eventNumber) || !eventNumber ||
        !ParseUnsigned(argv[10], UINTPTR_MAX, abortNumber) || !abortNumber ||
        !ParseUnsigned(argv[9], kMaxDownloadBytes, size) || size < kMinDownloadBytes) return 31;
    Handle parent(reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(parentNumber)));
    Handle ready(reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(eventNumber)));
    Handle abort(reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(abortNumber)));
    if (GetProcessId(parent.value) != pid || pid == GetCurrentProcessId() || WaitForSingleObject(parent.value, 0) != WAIT_TIMEOUT) return 32;
    // Handles are no longer allowed to leak into the restarted program.
    if (!SetHandleInformation(parent.value, HANDLE_FLAG_INHERIT, 0) || !SetHandleInformation(ready.value, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(abort.value, HANDLE_FLAG_INHERIT, 0)) return 33;
    const std::wstring target = argv[5], stage = argv[6], helper = CurrentExePath();
    const std::wstring actualParent = ProcessImage(parent.value);
    if (actualParent.empty() || !FilePresent(target) || !SameFile(actualParent, target) ||
        !IsStagePath(stage) || helper != ParentDirectory(stage) + L"\\helper.exe" ||
        SameFile(target, helper) || SameFile(target, stage)) return 34;
    Manifest manifest;
    manifest.versionText = NarrowAscii(argv[7]); manifest.sha256 = NarrowAscii(argv[8]); manifest.sizeBytes = size;
    if (!ParseVersion(manifest.versionText, manifest.version) || manifest.sha256.size() != 64) return 35;
    for (const char c : manifest.sha256) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 35;
    std::wstring error;
    Version oldVersion, helperVersion;
    if (!ExeVersion(target, oldVersion, error) || !ExeVersion(helper, helperVersion, error) ||
        Compare(oldVersion, helperVersion) != 0 || Compare(manifest.version, oldVersion) <= 0 || !VerifyExe(stage, manifest, error)) return 36;
    std::string oldHash; std::uint64_t oldSize = 0;
    if (!HashFile(target, oldHash, oldSize, error)) return 37;
    std::wstring token;
    if (!RandomToken(token)) return 38;
    const auto temporary = target + L".update-" + token + L".tmp";
    const auto backup = target + L".backup-" + token + L".exe";
    if (!CopyFileW(stage.c_str(), temporary.c_str(), TRUE) || !VerifyExe(temporary, manifest, error)) {
        DeleteFileW(temporary.c_str()); return 39;
    }
    // Only signal ready once target identity, download, version and destination write access are checked.
    if (!SetEvent(ready.value)) { DeleteFileW(temporary.c_str()); return 40; }
    ready.Reset();
    HANDLE exitOrAbort[] = {abort.value, parent.value};
    if (WaitForMultipleObjects(2, exitOrAbort, FALSE, 60000) != WAIT_OBJECT_0 + 1) {
        DeleteFileW(temporary.c_str()); CleanupStagedDownload(stage); return 41;
    }
    parent.Reset(); abort.Reset();
    // Block a second cat process while the program file is being replaced.
    Handle instance(CreateMutexW(nullptr, FALSE, L"Local\\YeopjariCat.OneInstance.v1"));
    if (!instance || WaitForSingleObject(instance.value, 0) != WAIT_OBJECT_0) {
        DeleteFileW(temporary.c_str()); CleanupStagedDownload(stage);
        HelperMessage(L"다른 고양이 프로그램이 실행 중이어서 업데이트를 멈췄어요. 모두 종료한 뒤 다시 시도해 주세요."); return 42;
    }
    if (!MatchesHash(target, oldHash, oldSize) || !VerifyExe(temporary, manifest, error)) {
        ReleaseMutex(instance.value); instance.Reset();
        DeleteFileW(temporary.c_str()); CleanupStagedDownload(stage);
        HelperMessage(L"업데이트 준비 중 프로그램 파일이 바뀌어서 교체를 멈췄어요. 원래 프로그램을 다시 실행해 주세요."); return 43;
    }
    bool replaced = false;
    DWORD replaceError = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (ReplaceFileW(target.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr)) { replaced = true; break; }
        replaceError = GetLastError();
        if (replaceError != ERROR_SHARING_VIOLATION && replaceError != ERROR_ACCESS_DENIED) break;
        Sleep(100);
    }
    if (!replaced) {
        const bool recovered = RestoreOriginal(target, backup, oldHash, oldSize, token);
        ReleaseMutex(instance.value); instance.Reset();
        DeleteFileW(temporary.c_str()); CleanupStagedDownload(stage);
        if (recovered) LaunchProgram(target);
        HelperMessage(recovered ? L"프로그램을 교체하지 못해 기존 버전을 유지했어요. 프로그램 폴더 권한이나 백신 차단 여부를 확인해 주세요."
                                : L"프로그램 교체를 완료하지 못했어요. 성장 기록은 그대로 있어요. 프로그램을 다시 받아 실행해 주세요. 이전 실행 파일: " + backup);
        return 44;
    }
    ReleaseMutex(instance.value); instance.Reset();
    if (!LaunchProgram(target)) {
        const bool recovered = RestoreOriginal(target, backup, oldHash, oldSize, token);
        if (recovered) LaunchProgram(target);
        CleanupStagedDownload(stage);
        HelperMessage(recovered ? L"새 버전을 시작하지 못해 기존 실행 파일로 돌아왔어요. 새 배포 파일을 확인해 주세요."
                                : L"새 버전을 시작하지 못했어요. 이전 실행 파일을 보관했어요: " + backup);
        return 45;
    }
    // Keep one previous executable for manual recovery. This is not a startup-health guarantee.
    const auto previous = target + L".previous";
    MoveFileExW(backup.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    CleanupStagedDownload(stage); // Running helper.exe is removed on a later cleanup, not with a shell command.
    return 0;
}
} // namespace native

// Call before the single-instance mutex, pet-state loading, COM or GDI+ initialization.
inline std::optional<int> HandleUpdateHelperCommandLine() {
    int count = 0; wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return std::nullopt;
    std::optional<int> result;
    if (count >= 2 && wcscmp(arguments[1], L"--apply-update") == 0) result = native::ApplyUpdateHelper(count, arguments);
    LocalFree(arguments); return result;
}

// The caller must save pet state BEFORE this call and exit only after true is returned.
// No shell is involved; only the current, validated executable is copied and started.
inline bool LaunchUpdateHelper(const std::wstring& target, const std::wstring& stage,
                               const Manifest& manifest, std::wstring& error) {
    error.clear();
    if (!native::IsStagePath(stage) || !native::SameFile(target, CurrentExePath()) || !native::VerifyExe(stage, manifest, error)) {
        if (error.empty()) error = L"업데이트 대상 파일을 확인하지 못했어요."; return false;
    }
    const auto helper = native::ParentDirectory(stage) + L"\\helper.exe";
    if (!CopyFileW(target.c_str(), helper.c_str(), TRUE)) { error = L"업데이트 도우미를 준비하지 못했어요."; return false; }
    SECURITY_ATTRIBUTES security{}; security.nLength = sizeof(security); security.bInheritHandle = TRUE;
    native::Handle parent(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, GetCurrentProcessId()));
    native::Handle ready(CreateEventW(&security, TRUE, FALSE, nullptr));
    native::Handle abort(CreateEventW(&security, TRUE, FALSE, nullptr));
    if (!parent || !ready || !abort) { DeleteFileW(helper.c_str()); error = L"업데이트 도우미 연결을 준비하지 못했어요."; return false; }
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<BYTE> attributeStorage(attributeBytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if (!attributeBytes || !InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes)) {
        DeleteFileW(helper.c_str()); error = L"업데이트 도우미의 실행 정보를 준비하지 못했어요."; return false;
    }
    HANDLE inherited[] = {parent.value, ready.value, abort.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attributes); DeleteFileW(helper.c_str());
        error = L"업데이트 도우미의 접근 권한을 준비하지 못했어요."; return false;
    }
    std::wstring command = native::QuoteArgument(helper) + L" --apply-update " + std::to_wstring(GetCurrentProcessId()) + L" " +
        std::to_wstring(reinterpret_cast<UINT_PTR>(parent.value)) + L" " + std::to_wstring(reinterpret_cast<UINT_PTR>(ready.value)) + L" " +
        native::QuoteArgument(target) + L" " + native::QuoteArgument(stage) + L" " + native::QuoteArgument(native::WidenAscii(manifest.versionText)) + L" " +
        native::QuoteArgument(native::WidenAscii(manifest.sha256)) + L" " + std::to_wstring(manifest.sizeBytes) + L" " +
        std::to_wstring(reinterpret_cast<UINT_PTR>(abort.value));
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    const auto folder = native::ParentDirectory(helper);
    const bool launched = CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT,
                                         nullptr, folder.c_str(), &startup.StartupInfo, &process) != FALSE;
    DeleteProcThreadAttributeList(attributes);
    if (!launched) { DeleteFileW(helper.c_str()); error = L"업데이트 도우미를 시작하지 못했어요. 백신 차단 여부를 확인해 주세요."; return false; }
    native::Handle child(process.hProcess), thread(process.hThread);
    HANDLE waiting[] = {ready.value, child.value};
    const DWORD waited = WaitForMultipleObjects(2, waiting, FALSE, 15000);
    if (waited == WAIT_OBJECT_0) return true;
    SetEvent(abort.value);
    // No replacement can occur before this parent exits. On failure keep the parent running.
    DWORD code = 0;
    if (waited == WAIT_OBJECT_0 + 1) GetExitCodeProcess(child.value, &code);
    error = L"업데이트를 준비하지 못했어요. 실행 파일을 쓸 수 있는 폴더에 두었는지 확인해 주세요. 기존 프로그램은 계속 실행돼요.";
    if (code) error += L" (준비 오류 " + std::to_wstring(code) + L")";
    return false;
}
} // namespace cat::update
