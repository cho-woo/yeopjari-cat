#pragma once

// Portable, dependency-free reader for the release feed. Downloading and checking
// the SHA-256 of the executable are deliberately the caller's responsibility.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace cat::update {

inline constexpr std::size_t kMaxManifestBytes = 64 * 1024;
inline constexpr std::size_t kMaxNotesBytes = 8 * 1024;
inline constexpr std::uint64_t kMinDownloadBytes = 1024;
inline constexpr std::uint64_t kMaxDownloadBytes = 128 * 1024 * 1024;

struct Version {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;
};

struct Manifest {
    Version version;
    std::string versionText;
    std::string downloadUrl;
    std::string sha256;
    std::string notesUtf8;
    std::uint64_t sizeBytes = 0;
};

namespace detail {

inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }
inline bool IsAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
inline char LowerAscii(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
inline int HexValue(char c) {
    if (IsDigit(c)) return c - '0';
    c = LowerAscii(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// Decimal values have one canonical representation; neither signs nor redundant
// leading zeroes are accepted. The limit is applied before each multiplication.
inline bool ParseUnsigned(std::string_view text, std::uint64_t limit,
                          std::uint64_t& destination) {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (!IsDigit(c)) return false;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (digit > limit || value > (limit - digit) / 10) return false;
        value = value * 10 + digit;
    }
    destination = value;
    return true;
}

inline bool ParseVersionView(std::string_view text, Version& destination) {
    std::array<std::uint16_t, 3> components{};
    std::size_t start = 0;
    for (std::size_t i = 0; i < components.size(); ++i) {
        const auto dot = text.find('.', start);
        const bool last = i + 1 == components.size();
        if ((last && dot != std::string_view::npos) ||
            (!last && dot == std::string_view::npos)) return false;
        const auto end = dot == std::string_view::npos ? text.size() : dot;
        std::uint64_t value = 0;
        if (!ParseUnsigned(text.substr(start, end - start), 65535, value)) return false;
        components[i] = static_cast<std::uint16_t>(value);
        start = end + 1;
    }
    destination = {components[0], components[1], components[2]};
    return true;
}

inline bool ValidHost(std::string_view host) {
    // DNS names (including punycode) and ordinary dotted-decimal IPv4 addresses.
    // IP literals in brackets and alternate numeric IPv4 forms are not supported.
    if (host.empty() || host.size() > 253) return false;
    bool numeric = true;
    for (const char c : host) {
        if (!IsDigit(c) && c != '.') numeric = false;
    }
    std::size_t start = 0;
    std::size_t labels = 0;
    while (start < host.size()) {
        const auto dot = host.find('.', start);
        const auto end = dot == std::string_view::npos ? host.size() : dot;
        const auto label = host.substr(start, end - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' ||
            label.back() == '-') return false;
        for (const char c : label) {
            if (!IsAlpha(c) && !IsDigit(c) && c != '-') return false;
        }
        if (numeric) {
            std::uint64_t octet = 0;
            if (!ParseUnsigned(label, 255, octet)) return false;
        }
        ++labels;
        if (dot == std::string_view::npos) break;
        start = dot + 1;
        if (start == host.size()) return false;
    }
    return !numeric || labels == 4;
}

inline bool HttpsUrl(std::string_view url) {
    constexpr std::string_view scheme = "https://";
    if (url.size() <= scheme.size() || url.size() > kMaxManifestBytes) return false;
    for (std::size_t i = 0; i < scheme.size(); ++i) {
        if (LowerAscii(url[i]) != scheme[i]) return false;
    }
    // RFC 3986 printable ASCII characters only. Raw fragments, backslashes and
    // characters that can change command/URL interpretation are never accepted.
    constexpr std::string_view punctuation = "-._~:/?@!$&'()*+,;=%";
    for (std::size_t i = 0; i < url.size(); ++i) {
        const char c = url[i];
        if (!IsAlpha(c) && !IsDigit(c) && punctuation.find(c) == std::string_view::npos)
            return false;
        if (c == '%') {
            if (i + 2 >= url.size()) return false;
            const int high = HexValue(url[i + 1]);
            const int low = HexValue(url[i + 2]);
            if (high < 0 || low < 0) return false;
            const int decoded = high * 16 + low;
            if (decoded < 0x20 || decoded == 0x7f || decoded == '\\') return false;
            i += 2;
        }
    }
    const auto authorityEnd = url.find_first_of("/?", scheme.size());
    const auto authority = url.substr(scheme.size(),
        (authorityEnd == std::string_view::npos ? url.size() : authorityEnd) - scheme.size());
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        authority.find('%') != std::string_view::npos) return false;
    const auto colon = authority.find(':');
    auto host = authority;
    if (colon != std::string_view::npos) {
        if (authority.substr(colon + 1) != "443") return false;
        host = authority.substr(0, colon);
    }
    return ValidHost(host);
}

inline bool ValidSha256(std::string_view value) {
    if (value.size() != 64) return false;
    for (const char c : value) {
        if (!IsDigit(c) && !(c >= 'a' && c <= 'f')) return false;
    }
    return true;
}

inline bool ValidNotes(std::string_view text) {
    if (text.size() > kMaxNotesBytes) return false;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first == 0) return false;
        if (first <= 0x7f) continue;
        std::uint32_t codepoint = 0;
        std::uint32_t minimum = 0;
        std::size_t extra = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            codepoint = first & 0x1f; minimum = 0x80; extra = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            codepoint = first & 0x0f; minimum = 0x800; extra = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            codepoint = first & 0x07; minimum = 0x10000; extra = 3;
        } else {
            return false;
        }
        if (text.size() - i < extra) return false;
        for (std::size_t j = 0; j < extra; ++j) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}

inline int Base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

inline bool DecodeBase64(std::string_view text, std::string& destination) {
    if (text.size() % 4 != 0 || text.size() > ((kMaxNotesBytes + 2) / 3) * 4)
        return false;
    std::string decoded;
    decoded.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const int a = Base64Value(text[i]);
        const int b = Base64Value(text[i + 1]);
        const bool last = i + 4 == text.size();
        if (a < 0 || b < 0) return false;
        decoded += static_cast<char>((a << 2) | (b >> 4));
        if (text[i + 2] == '=') {
            if (!last || text[i + 3] != '=' || (b & 0x0f) != 0) return false;
        } else {
            const int c = Base64Value(text[i + 2]);
            if (c < 0) return false;
            decoded += static_cast<char>(((b & 0x0f) << 4) | (c >> 2));
            if (text[i + 3] == '=') {
                if (!last || (c & 0x03) != 0) return false;
            } else {
                const int d = Base64Value(text[i + 3]);
                if (d < 0) return false;
                decoded += static_cast<char>(((c & 0x03) << 6) | d);
            }
        }
    }
    if (!ValidNotes(decoded)) return false;
    destination = std::move(decoded);
    return true;
}

inline std::string EncodeBase64(std::string_view text) {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve((text.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < text.size(); i += 3) {
        const auto a = static_cast<unsigned char>(text[i]);
        const auto b = i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : 0;
        const auto c = i + 2 < text.size() ? static_cast<unsigned char>(text[i + 2]) : 0;
        encoded += alphabet[a >> 2];
        encoded += alphabet[((a & 0x03) << 4) | (b >> 4)];
        encoded += i + 1 < text.size() ? alphabet[((b & 0x0f) << 2) | (c >> 6)] : '=';
        encoded += i + 2 < text.size() ? alphabet[c & 0x3f] : '=';
    }
    return encoded;
}

} // namespace detail

inline bool ParseVersion(const std::string& text, Version& destination) {
    return detail::ParseVersionView(text, destination);
}

inline int Compare(const Version& left, const Version& right) {
    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
    return 0;
}

inline std::string ToString(const Version& version) {
    return std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' +
        std::to_string(version.patch);
}

inline bool IsHttpsUrl(const std::string& url) { return detail::HttpsUrl(url); }

// Canonical field order and LF endings make the producer output reproducible.
// A constructed manifest must provide the canonical versionText matching version.
inline std::string SerializeManifest(const Manifest& manifest) {
    if (manifest.versionText != ToString(manifest.version) ||
        !detail::HttpsUrl(manifest.downloadUrl) || !detail::ValidSha256(manifest.sha256) ||
        manifest.sizeBytes < kMinDownloadBytes || manifest.sizeBytes > kMaxDownloadBytes ||
        !detail::ValidNotes(manifest.notesUtf8)) return {};
    std::string text = "schema=1\napp_id=yeopjari-cat\nchannel=stable\nversion=";
    text += manifest.versionText;
    text += "\ndownload_url=";
    text += manifest.downloadUrl;
    text += "\nsha256=";
    text += manifest.sha256;
    text += "\nsize_bytes=";
    text += std::to_string(manifest.sizeBytes);
    text += "\nnotes_b64=";
    text += detail::EncodeBase64(manifest.notesUtf8);
    text += '\n';
    return text.size() <= kMaxManifestBytes ? text : std::string{};
}

inline bool ParseManifest(const std::string& text, Manifest& destination, std::string& error) {
    const auto fail = [&error](const char* message) { error = message; return false; };
    if (text.empty() || text.size() > kMaxManifestBytes)
        return fail("The update manifest is empty or exceeds 64 KiB.");
    constexpr std::array<std::string_view, 8> names = {
        "schema", "app_id", "channel", "version", "download_url", "sha256", "size_bytes", "notes_b64"
    };
    std::array<std::string_view, names.size()> values{};
    std::array<bool, names.size()> seen{};
    int newlineStyle = 0; // 1: LF; 2: CRLF. Mixing styles is rejected.
    std::size_t fieldCount = 0;
    std::size_t start = 0;
    const std::string_view view(text);
    while (start < text.size()) {
        const auto newline = view.find('\n', start);
        auto end = newline == std::string_view::npos ? view.size() : newline;
        const bool crlf = end > start && view[end - 1] == '\r';
        if (crlf) {
            if (newline == std::string_view::npos) return fail("A bare carriage return is not allowed.");
            --end;
        }
        if (newline != std::string_view::npos) {
            const int style = crlf ? 2 : 1;
            if (newlineStyle != 0 && newlineStyle != style)
                return fail("The update manifest mixes line endings.");
            newlineStyle = style;
        }
        const auto line = view.substr(start, end - start);
        if (line.empty()) return fail("Blank manifest lines are not allowed.");
        for (const auto c : line) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < 0x20 || byte > 0x7e)
                return fail("Manifest fields must contain printable ASCII; encode notes as base64.");
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) return fail("A manifest field is missing '='.");
        const auto name = line.substr(0, equals);
        std::size_t index = 0;
        while (index < names.size() && name != names[index]) ++index;
        if (index == names.size()) return fail("The update manifest has an unknown field.");
        if (seen[index]) return fail("The update manifest has a duplicate field.");
        seen[index] = true;
        ++fieldCount;
        values[index] = line.substr(equals + 1);
        if (newline == std::string_view::npos) break;
        start = newline + 1;
    }
    if (fieldCount != names.size()) return fail("The update manifest is missing a required field.");
    if (values[0] != "1") return fail("The update manifest schema is unsupported.");
    if (values[1] != "yeopjari-cat") return fail("The update manifest is for another application.");
    if (values[2] != "stable") return fail("The update manifest channel is unsupported.");
    Manifest parsed;
    if (!detail::ParseVersionView(values[3], parsed.version))
        return fail("The release version must contain three canonical numeric components.");
    if (!detail::HttpsUrl(values[4])) return fail("The download URL must be a valid HTTPS URL on port 443.");
    if (!detail::ValidSha256(values[5])) return fail("The release SHA-256 must have 64 lowercase hexadecimal characters.");
    if (!detail::ParseUnsigned(values[6], kMaxDownloadBytes, parsed.sizeBytes) ||
        parsed.sizeBytes < kMinDownloadBytes)
        return fail("The release size must be between 1 KiB and 128 MiB.");
    if (!detail::DecodeBase64(values[7], parsed.notesUtf8))
        return fail("Release notes must be canonical base64 containing at most 8 KiB of valid UTF-8 without NUL.");
    parsed.versionText = values[3];
    parsed.downloadUrl = values[4];
    parsed.sha256 = values[5];
    destination = std::move(parsed);
    error.clear();
    return true;
}

} // namespace cat::update
