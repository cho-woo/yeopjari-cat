#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <winver.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "update_protocol.hpp"

namespace cat::update {
namespace native {

// These wrappers are deliberately non-copyable: a network/file handle has one owner.
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { Reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void Reset(HANDLE h = INVALID_HANDLE_VALUE) {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = h;
    }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
struct HttpHandle {
    HINTERNET value = nullptr;
    explicit HttpHandle(HINTERNET h = nullptr) : value(h) {}
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
};
inline std::wstring WidenAscii(const std::string& s) { return {s.begin(), s.end()}; }
inline std::string NarrowAscii(const std::wstring& s) {
    std::string result;
    for (const wchar_t c : s) { if (c < 0x20 || c > 0x7e) return {}; result += static_cast<char>(c); }
    return result;
}
inline std::wstring ParentDirectory(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}
inline bool FilePresent(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}
inline bool MakeDirectory(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
inline bool RandomToken(std::wstring& token) {
    std::array<unsigned char, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
    constexpr wchar_t digits[] = L"0123456789abcdef";
    token.clear();
    for (const auto byte : bytes) { token += digits[byte >> 4]; token += digits[byte & 15]; }
    return true;
}
inline bool IsCancelled(const std::atomic_bool& cancel, ULONGLONG deadline, std::wstring& error) {
    if (cancel.load()) { error = L"업데이트 작업을 취소했어요."; return true; }
    if (GetTickCount64() > deadline) { error = L"업데이트 서버의 응답이 늦어요. 잠시 뒤 다시 확인해 주세요."; return true; }
    return false;
}
inline bool NetworkFailure(std::wstring& error) {
    error = L"업데이트 서버에 연결하지 못했어요. 인터넷 연결과 Windows 날짜를 확인한 뒤 다시 시도해 주세요. (오류 " + std::to_wstring(GetLastError()) + L")";
    return false;
}

inline bool FetchHttps(const std::string& initialUrl, std::uint64_t byteLimit,
                       const std::atomic_bool& cancel, ULONGLONG budgetMs,
                       const std::function<bool(const char*, DWORD)>& receive, std::wstring& error) {
    error.clear();
    const ULONGLONG deadline = GetTickCount64() + budgetMs;
    HttpHandle session(WinHttpOpen(L"YeopjariCat-Updater/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value || !WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000)) return NetworkFailure(error);
    DWORD retries = 0, protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    if (!WinHttpSetOption(session.value, WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries)) ||
        !WinHttpSetOption(session.value, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) return NetworkFailure(error);
    std::string url = initialUrl;
    for (int redirect = 0; redirect <= 5; ++redirect) {
        if (IsCancelled(cancel, deadline, error)) return false;
        if (!IsHttpsUrl(url)) { error = L"안전한 HTTPS 업데이트 주소가 아니에요."; return false; }
        const std::wstring wideUrl = WidenAscii(url);
        URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts) ||
            parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT) {
            error = L"업데이트 주소를 읽을 수 없어요."; return false;
        }
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path;
        if (parts.dwUrlPathLength) path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (path.empty()) path = L"/";
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        HttpHandle connection(WinHttpConnect(session.value, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connection.value) return NetworkFailure(error);
        HttpHandle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!request.value) return NetworkFailure(error);
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        DWORD headerLimit = 32768, responseTimeout = 5000;
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
            !WinHttpSetOption(request.value, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headerLimit, sizeof(headerLimit)) ||
            !WinHttpSetOption(request.value, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &responseTimeout, sizeof(responseTimeout))) return NetworkFailure(error);
        // Certificate validation uses WinHTTP defaults. No IGNORE_CERT_* flags are set.
        if (!WinHttpSendRequest(request.value, L"Accept-Encoding: identity\r\nCache-Control: no-cache\r\n", static_cast<DWORD>(-1),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return NetworkFailure(error);
        if (IsCancelled(cancel, deadline, error)) return false;
        if (!WinHttpReceiveResponse(request.value, nullptr)) return NetworkFailure(error);
        if (IsCancelled(cancel, deadline, error)) return false;
        DWORD status = 0, statusBytes = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes, WINHTTP_NO_HEADER_INDEX)) return NetworkFailure(error);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (redirect == 5) { error = L"업데이트 주소가 너무 여러 번 이동해요."; return false; }
            std::array<wchar_t, 8192> location{}; DWORD locationBytes = static_cast<DWORD>(location.size() * sizeof(wchar_t));
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                     location.data(), &locationBytes, WINHTTP_NO_HEADER_INDEX)) return NetworkFailure(error);
            std::string next = NarrowAscii(location.data());
            if (!next.empty() && next.front() == '/' && (next.size() < 2 || next[1] != '/'))
                next = "https://" + NarrowAscii(host) + next;
            if (!IsHttpsUrl(next)) { error = L"업데이트 서버가 안전하지 않은 주소로 이동하려고 했어요."; return false; }
            url = std::move(next);
            continue;
        }
        if (status != 200) { error = L"업데이트 파일을 받지 못했어요. 배포 주소를 확인해 주세요. (HTTP " + std::to_wstring(status) + L")"; return false; }
        std::array<wchar_t, 64> lengthText{}; DWORD lengthBytes = static_cast<DWORD>(sizeof(lengthText));
        if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                lengthText.data(), &lengthBytes, WINHTTP_NO_HEADER_INDEX)) {
            std::uint64_t length = 0;
            for (const wchar_t* c = lengthText.data(); *c; ++c) {
                if (*c < L'0' || *c > L'9' || length > byteLimit / 10) { error = L"업데이트 파일의 크기가 올바르지 않아요."; return false; }
                length = length * 10 + static_cast<unsigned>(*c - L'0');
                if (length > byteLimit) { error = L"업데이트 파일이 허용 크기를 넘었어요."; return false; }
            }
        }
        std::uint64_t total = 0;
        std::array<char, 16384> chunk{};
        for (;;) {
            if (IsCancelled(cancel, deadline, error)) return false;
            DWORD received = 0;
            if (!WinHttpReadData(request.value, chunk.data(), static_cast<DWORD>(chunk.size()), &received)) return NetworkFailure(error);
            if (IsCancelled(cancel, deadline, error)) return false;
            if (!received) return true;
            if (received > byteLimit - total) { error = L"업데이트 파일이 허용 크기를 넘었어요."; return false; }
            total += received;
            if (!receive(chunk.data(), received)) { if (error.empty()) error = L"업데이트 파일을 저장하지 못했어요."; return false; }
        }
    }
    return false;
}

inline bool ReadAt(HANDLE file, std::uint64_t offset, void* buffer, DWORD length) {
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD read = 0;
    return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) && ReadFile(file, buffer, length, &read, nullptr) && read == length;
}
inline bool ExeVersion(const std::wstring& file, Version& version, std::wstring& error) {
    Handle input(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER length{}; IMAGE_DOS_HEADER dos{};
    if (!input || !GetFileSizeEx(input.value, &length) || length.QuadPart < 1024 ||
        !ReadAt(input.value, 0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 64 ||
        static_cast<std::uint64_t>(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > static_cast<std::uint64_t>(length.QuadPart)) {
        error = L"올바른 Windows 실행 파일이 아니에요."; return false;
    }
    IMAGE_NT_HEADERS64 nt{};
    if (!ReadAt(input.value, static_cast<std::uint64_t>(dos.e_lfanew), &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || (nt.FileHeader.Characteristics & IMAGE_FILE_DLL) ||
        !(nt.FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.Subsystem != IMAGE_SUBSYSTEM_WINDOWS_GUI) {
        error = L"Windows x64용 고양이 실행 파일이 아니에요."; return false;
    }
    DWORD unused = 0; const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &unused);
    if (!size || size > 1048576) { error = L"업데이트 버전 정보를 확인하지 못했어요."; return false; }
    std::vector<BYTE> info(size);
    if (!GetFileVersionInfoW(file.c_str(), 0, size, info.data())) { error = L"업데이트 버전 정보를 읽지 못했어요."; return false; }
    VS_FIXEDFILEINFO* fixed = nullptr; UINT fixedSize = 0;
    wchar_t* product = nullptr; UINT productSize = 0;
    if (!VerQueryValueW(info.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixedSize) || fixedSize < sizeof(VS_FIXEDFILEINFO) ||
        fixed->dwSignature != 0xfeef04bd || fixed->dwFileType != VFT_APP || LOWORD(fixed->dwFileVersionLS) != 0 ||
        !VerQueryValueW(info.data(), L"\\StringFileInfo\\040904B0\\ProductName", reinterpret_cast<void**>(&product), &productSize) ||
        productSize != 13 || std::wstring(product, productSize - 1) != L"Yeopjari Cat") {
        error = L"옆자리 고양이의 업데이트 파일이 아니에요."; return false;
    }
    version = {HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS), HIWORD(fixed->dwFileVersionLS)};
    return true;
}
inline bool HashFile(const std::wstring& file, std::string& digest, std::uint64_t& size, std::wstring& error) {
    Handle input(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER length{};
    if (!input || !GetFileSizeEx(input.value, &length) || length.QuadPart < 0 ||
        static_cast<std::uint64_t>(length.QuadPart) > kMaxDownloadBytes) { error = L"업데이트 파일을 검증할 수 없어요."; return false; }
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) { error = L"Windows 파일 검증 기능을 시작하지 못했어요."; return false; }
    DWORD objectSize = 0, resultBytes = 0;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultBytes, 0) >= 0 && objectSize < 1048576;
    std::vector<BYTE> object(ok ? objectSize : 0);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) >= 0;
    std::array<BYTE, 16384> bytes{};
    std::uint64_t total = 0;
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(input.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) { ok = false; break; }
        if (!read) break;
        total += read;
        if (total > kMaxDownloadBytes || BCryptHashData(hash, bytes.data(), read, 0) < 0) { ok = false; break; }
    }
    std::array<BYTE, 32> result{};
    if (ok) ok = total == static_cast<std::uint64_t>(length.QuadPart) && BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) { error = L"업데이트 파일 검증에 실패했어요."; return false; }
    constexpr char digits[] = "0123456789abcdef";
    digest.clear(); for (const auto byte : result) { digest += digits[byte >> 4]; digest += digits[byte & 15]; }
    size = total; return true;
}
inline bool VerifyExe(const std::wstring& path, const Manifest& manifest, std::wstring& error) {
    if (!FilePresent(path)) { error = L"업데이트 실행 파일이 없거나 올바른 파일이 아니에요."; return false; }
    std::string digest; std::uint64_t size = 0;
    if (!HashFile(path, digest, size, error)) return false;
    if (digest != manifest.sha256 || size != manifest.sizeBytes) { error = L"받은 업데이트 파일의 검증값이 달라요. 다시 내려받아 주세요."; return false; }
    Version version;
    if (!ExeVersion(path, version, error)) return false;
    if (Compare(version, manifest.version) != 0) { error = L"업데이트 안내와 실행 파일의 버전이 달라요."; return false; }
    return true;
}
} // namespace native

inline std::wstring CurrentExePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return count && count < buffer.size() ? std::wstring(buffer.data(), count) : std::wstring{};
}
inline std::string ReadFeed(const std::wstring& executable, std::wstring& error) {
    error.clear();
    const std::wstring folder = native::ParentDirectory(executable);
    if (folder.empty()) { error = L"프로그램 폴더를 찾지 못했어요."; return {}; }
    const std::wstring path = folder + L"\\update-feed.txt";
    native::Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        const DWORD failure = GetLastError();
        if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PATH_NOT_FOUND) error = L"update-feed.txt를 읽지 못했어요.";
        return {};
    }
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.value, &length) || length.QuadPart < 1 || length.QuadPart > 8192) { error = L"update-feed.txt에 HTTPS 주소 한 줄을 넣어 주세요."; return {}; }
    std::string url(static_cast<std::size_t>(length.QuadPart), '\0'); DWORD read = 0;
    if (!ReadFile(file.value, url.data(), static_cast<DWORD>(url.size()), &read, nullptr) || read != url.size()) { error = L"update-feed.txt를 읽지 못했어요."; return {}; }
    if (url.compare(0, 3, "\xef\xbb\xbf") == 0) url.erase(0, 3);
    while (!url.empty() && (url.back() == '\r' || url.back() == '\n' || url.back() == ' ' || url.back() == '\t')) url.pop_back();
    if (!IsHttpsUrl(url)) { error = L"update-feed.txt에 올바른 HTTPS 배포 주소 한 줄을 넣어 주세요."; return {}; }
    return url;
}
inline bool Check(const std::string& feed, Manifest& result, std::wstring& error, const std::atomic_bool& cancel) {
    std::string text;
    if (!native::FetchHttps(feed, kMaxManifestBytes, cancel, 30000,
        [&text](const char* bytes, DWORD count) { text.append(bytes, count); return true; }, error)) return false;
    std::string parseError;
    if (!ParseManifest(text, result, parseError)) { error = L"업데이트 안내 파일의 형식이 올바르지 않아요. 배포자가 최신 버전 정보를 확인해야 해요."; return false; }
    return true;
}

// Only updater-owned names in a randomly created update-* folder may be removed.
inline void CleanupStagedDownload(const std::wstring& staged) {
    const auto folder = native::ParentDirectory(staged);
    const auto nameAt = folder.find_last_of(L"\\/");
    const auto name = nameAt == std::wstring::npos ? folder : folder.substr(nameAt + 1);
    if (name.size() != 39 || name.compare(0, 7, L"update-") != 0 || staged != folder + L"\\download.exe") return;
    for (std::size_t i = 7; i < name.size(); ++i)
        if (!((name[i] >= L'0' && name[i] <= L'9') || (name[i] >= L'a' && name[i] <= L'f'))) return;
    const DWORD attributes = GetFileAttributesW(folder.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return;
    DeleteFileW(staged.c_str());
    DeleteFileW((folder + L"\\helper.exe").c_str());
    RemoveDirectoryW(folder.c_str());
}
inline void CleanupOldDownloads(const std::wstring& appDataDirectory) {
    const auto root = appDataDirectory + L"\\updates";
    const DWORD rootAttributes = GetFileAttributesW(root.c_str());
    if (rootAttributes == INVALID_FILE_ATTRIBUTES || !(rootAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return;
    FILETIME nowFile{}; GetSystemTimeAsFileTime(&nowFile);
    ULARGE_INTEGER now{}; now.LowPart = nowFile.dwLowDateTime; now.HighPart = nowFile.dwHighDateTime;
    constexpr ULONGLONG oneDay = 24ULL * 60 * 60 * 10000000;
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((root + L"\\update-*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    unsigned inspected = 0;
    do {
        if (++inspected > 256) break;
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        ULARGE_INTEGER written{}; written.LowPart = found.ftLastWriteTime.dwLowDateTime; written.HighPart = found.ftLastWriteTime.dwHighDateTime;
        if (now.QuadPart <= written.QuadPart || now.QuadPart - written.QuadPart < oneDay) continue;
        CleanupStagedDownload(root + L"\\" + found.cFileName + L"\\download.exe");
    } while (FindNextFileW(search, &found));
    FindClose(search);
}
inline bool Download(const Manifest& manifest, const std::wstring& appDataDirectory,
                     std::wstring& stagedExe, std::wstring& error, const std::atomic_bool& cancel) {
    error.clear(); stagedExe.clear();
    if (manifest.sizeBytes < kMinDownloadBytes || manifest.sizeBytes > kMaxDownloadBytes || !IsHttpsUrl(manifest.downloadUrl)) {
        error = L"업데이트 파일 정보가 올바르지 않아요."; return false;
    }
    CleanupOldDownloads(appDataDirectory);
    const std::wstring root = appDataDirectory + L"\\updates";
    if (!native::MakeDirectory(root)) { error = L"업데이트 임시 폴더를 만들지 못했어요."; return false; }
    std::wstring folder;
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::wstring token;
        if (!native::RandomToken(token)) break;
        const std::wstring candidate = root + L"\\update-" + token;
        if (CreateDirectoryW(candidate.c_str(), nullptr)) { folder = candidate; break; }
        if (GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    if (folder.empty()) { error = L"업데이트 임시 폴더를 준비하지 못했어요."; return false; }
    const std::wstring path = folder + L"\\download.exe";
    native::Handle output(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!output) { RemoveDirectoryW(folder.c_str()); error = L"업데이트 파일을 만들지 못했어요."; return false; }
    bool ok = native::FetchHttps(manifest.downloadUrl, manifest.sizeBytes, cancel, 180000,
        [&output, &error](const char* bytes, DWORD count) {
            DWORD written = 0;
            if (WriteFile(output.value, bytes, count, &written, nullptr) && written == count) return true;
            error = L"업데이트 파일을 저장하지 못했어요. 디스크 공간을 확인해 주세요."; return false;
        }, error);
    if (ok && !FlushFileBuffers(output.value)) { error = L"업데이트 파일을 디스크에 저장하지 못했어요."; ok = false; }
    output.Reset();
    if (ok) ok = native::VerifyExe(path, manifest, error);
    if (ok && cancel.load()) { error = L"업데이트 작업을 취소했어요."; ok = false; }
    if (!ok) { CleanupStagedDownload(path); return false; }
    stagedExe = path; return true;
}
} // namespace cat::update
