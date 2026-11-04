#include "../src/update_protocol.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)

using cat::update::Compare;
using cat::update::IsHttpsUrl;
using cat::update::Manifest;
using cat::update::ParseManifest;
using cat::update::ParseVersion;
using cat::update::SerializeManifest;
using cat::update::ToString;
using cat::update::Version;

Manifest Example() {
    return {{1, 3, 0}, "1.3.0", "https://example.com/releases/YeopjariCat-1.3.0.exe",
        std::string(64, 'a'), "Hello\n", 8192};
}

std::string SetField(std::string text, const std::string& name, const std::string& value) {
    const auto start = text.find(name + '=');
    CHECK(start != std::string::npos);
    const auto valueStart = start + name.size() + 1;
    const auto end = text.find('\n', valueStart);
    CHECK(end != std::string::npos);
    text.replace(valueStart, end - valueStart, value);
    return text;
}

void RejectWithoutMutation(const std::string& text) {
    Manifest destination = Example();
    destination.version = {13, 42, 7};
    destination.versionText = "13.42.7";
    destination.notesUtf8 = u8"기존 고양이 설정은 그대로 😺";
    const auto previous = SerializeManifest(destination);
    CHECK(!previous.empty());
    std::string error = "previous error";
    CHECK(!ParseManifest(text, destination, error));
    CHECK(!error.empty() && error != "previous error");
    CHECK(SerializeManifest(destination) == previous);
}

void NumericVersions() {
    for (const std::string text : {"0.0.0", "1.3.0", "1.10.100", "65535.65535.65535"}) {
        Version destination{7, 8, 9};
        CHECK(ParseVersion(text, destination));
        CHECK(ToString(destination) == text);
    }
    for (const std::string text : {
        "", "1", "1.2", "1.2.3.4", ".1.2", "1..2", "1.2.", "1.02.3", "01.2.3",
        "1.2.00", "-1.2.3", "+1.2.3", "1.2.-3", "1.2.3-rc1", "1.2.3+build",
        "v1.2.3", "1.2.3 ", " 1.2.3", "1.2.3\n", "65536.1.2", "1.65536.2",
        "1.2.65536", "18446744073709551616.1.2", "1.2.e3"}) {
        Version destination{7, 8, 9};
        CHECK(!ParseVersion(text, destination));
        CHECK(ToString(destination) == "7.8.9");
    }
    Version destination{7, 8, 9};
    CHECK(!ParseVersion(std::string("1.2.3\0", 6), destination));
    CHECK(ToString(destination) == "7.8.9");
    CHECK(Compare({1, 10, 0}, {1, 9, 999}) == 1);
    CHECK(Compare({1, 2, 10}, {1, 2, 9}) == 1);
    CHECK(Compare({2, 0, 0}, {1, 65535, 65535}) == 1);
    CHECK(Compare({1, 3, 0}, {1, 3, 0}) == 0);
    CHECK(Compare({1, 3, 0}, {1, 3, 1}) == -1);
    CHECK(Compare({0, 0, 0}, {65535, 65535, 65535}) == -1);
}

void HttpsAddresses() {
    for (const std::string url : {
        "https://example.com", "HTTPS://EXAMPLE.COM/file.exe", "https://localhost/a",
        "https://example.com:443/a.exe", "https://127.0.0.1/a", "https://0.0.0.0/a",
        "https://255.255.255.255/a", "https://xn--bcher-kva.example/a%20b.exe",
        "https://example.com/a?name=x&sig=abc%2Fdef%3D", "https://example.com?version=1",
        "https://example.com/@release/update.exe", "https://example.com/a%23b"})
        CHECK(IsHttpsUrl(url));
    for (const std::string url : {
        "", "https://", "http://example.com/a", "file:///a.exe", "https:///a",
        " https://example.com", "https://example.com ", "https://example.com/\nX: yes",
        "https://example.com/\r\nLocation:https://evil.example", "https://example.com/\ta",
        "https://user@example.com/a", "https://user:password@example.com/a",
        "https://example.com@evil.example/a", "https://example.com%40evil.example/a",
        "https://example.com#fragment", "https://example.com/a#fragment",
        "https://example.com\\@evil.example/a", "https://example.com/a\\b",
        "https://example.com:80/a", "https://example.com:8443/a", "https://example.com:0443/a",
        "https://example.com:/a", "https://example.com:443:443/a", "https://:443/a",
        "https://[::1]/a", "https://bad_host.example/a", "https://-bad.example/a",
        "https://bad-.example/a", "https://bad..example/a", "https://.example/a",
        "https://example.com./a", "https://127.1/a", "https://127.00.0.1/a",
        "https://256.0.0.1/a", "https://123/a", "https://example.com/\"bad\"",
        "https://example.com/<script>", "https://example.com/a|b", "https://example.com/`cmd`",
        "https://example.com/a%", "https://example.com/a%2", "https://example.com/a%gg",
        "https://example.com/a%00b", "https://example.com/%0d%0aHeader:yes",
        "https://example.com/%5cb", "https://example.com/%7F", u8"https://고양이.example/a"})
        CHECK(!IsHttpsUrl(url));
    CHECK(!IsHttpsUrl(std::string("https://example.com/a\0b", 23)));
    CHECK(!IsHttpsUrl("https://" + std::string(64, 'a') + ".example/a"));
    CHECK(!IsHttpsUrl("https://" + std::string(63, 'a') + '.' + std::string(63, 'b') + '.' +
        std::string(63, 'c') + '.' + std::string(63, 'd') + "/a"));
}

void CanonicalRoundTrips() {
    const auto example = Example();
    const auto text = SerializeManifest(example);
    CHECK(text == "schema=1\napp_id=yeopjari-cat\nchannel=stable\nversion=1.3.0\n"
        "download_url=https://example.com/releases/YeopjariCat-1.3.0.exe\nsha256=" +
        std::string(64, 'a') + "\nsize_bytes=8192\nnotes_b64=SGVsbG8K\n");
    Manifest parsed;
    std::string error = "old error";
    CHECK(ParseManifest(text, parsed, error));
    CHECK(error.empty());
    CHECK(Compare(parsed.version, example.version) == 0);
    CHECK(parsed.versionText == example.versionText);
    CHECK(parsed.downloadUrl == example.downloadUrl);
    CHECK(parsed.sha256 == example.sha256);
    CHECK(parsed.sizeBytes == example.sizeBytes);
    CHECK(parsed.notesUtf8 == example.notesUtf8);
    CHECK(SerializeManifest(parsed) == text);
    CHECK(ParseManifest(text.substr(0, text.size() - 1), parsed, error));
    CHECK(SerializeManifest(parsed) == text);
    std::string crlf;
    for (const char c : text) { if (c == '\n') crlf += '\r'; crlf += c; }
    CHECK(ParseManifest(crlf, parsed, error));
    CHECK(SerializeManifest(parsed) == text);
    CHECK(ParseManifest(crlf.substr(0, crlf.size() - 2), parsed, error));
    CHECK(SerializeManifest(parsed) == text);
    // Fields may be reordered by another publisher; serialization is always canonical.
    const auto firstEnd = text.find('\n') + 1;
    CHECK(ParseManifest(text.substr(firstEnd) + text.substr(0, firstEnd), parsed, error));
    CHECK(SerializeManifest(parsed) == text);
    for (const std::string& notes : {std::string{}, std::string("M"), std::string("Ma"),
        std::string("Man"), std::string(u8"부드러운 점프와 귀 흔들기 😺\n츄르를 주세요."),
        std::string("\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80"
                    "\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf")}) {
        auto manifest = example;
        manifest.notesUtf8 = notes;
        CHECK(!SerializeManifest(manifest).empty());
        CHECK(ParseManifest(SerializeManifest(manifest), parsed, error));
        CHECK(parsed.notesUtf8 == notes);
    }
}

void InvalidFieldsAndLines() {
    const auto valid = SerializeManifest(Example());
    RejectWithoutMutation("");
    RejectWithoutMutation("invalid");
    RejectWithoutMutation("\xef\xbb\xbf" + valid);
    RejectWithoutMutation(valid + "\n");
    RejectWithoutMutation("\n" + valid);
    RejectWithoutMutation(valid + "unexpected=1\n");
    RejectWithoutMutation(valid + "schema=1\n");
    RejectWithoutMutation(valid + "notes_b64=\n");
    RejectWithoutMutation(valid.substr(valid.find('\n') + 1));
    RejectWithoutMutation(" schema=1\n" + valid.substr(valid.find('\n') + 1));
    RejectWithoutMutation("schema =1\n" + valid.substr(valid.find('\n') + 1));
    RejectWithoutMutation(SetField(valid, "schema", "01"));
    RejectWithoutMutation(SetField(valid, "schema", "2"));
    RejectWithoutMutation(SetField(valid, "schema", "1 "));
    RejectWithoutMutation(SetField(valid, "app_id", "another-cat"));
    RejectWithoutMutation(SetField(valid, "app_id", "yeopjari-Cat"));
    RejectWithoutMutation(SetField(valid, "channel", "beta"));
    RejectWithoutMutation(SetField(valid, "version", "1.03.0"));
    RejectWithoutMutation(SetField(valid, "version", "1.3.0-rc.1"));
    RejectWithoutMutation(SetField(valid, "download_url", "http://example.com/update.exe"));
    RejectWithoutMutation(SetField(valid, "download_url", "https://user@example.com/update.exe"));
    RejectWithoutMutation(SetField(valid, "download_url", "https://example.com/update.exe\nversion=9.0.0"));
    RejectWithoutMutation(SetField(valid, "sha256", std::string(64, 'A')));
    RejectWithoutMutation(SetField(valid, "sha256", std::string(64, 'g')));
    RejectWithoutMutation(SetField(valid, "sha256", std::string(63, 'a')));
    RejectWithoutMutation(SetField(valid, "sha256", std::string(65, 'a')));
    RejectWithoutMutation(SetField(valid, "version", std::string("1.3.0\0", 6)));
    RejectWithoutMutation(SetField(valid, "version", u8"１.3.0"));
    auto mixed = valid;
    mixed.insert(mixed.find('\n'), 1, '\r');
    RejectWithoutMutation(mixed);
    mixed = valid;
    mixed.back() = '\r';
    RejectWithoutMutation(mixed);
    RejectWithoutMutation(SetField(valid, "app_id", "yeopjari-\rcat"));
    for (const std::string name : {"schema", "app_id", "channel", "version", "download_url",
                                  "sha256", "size_bytes"})
        RejectWithoutMutation(SetField(valid, name, ""));
}

void SizeAndNoteBoundaries() {
    auto manifest = Example();
    Manifest parsed;
    std::string error;
    for (const std::uint64_t bytes : {cat::update::kMinDownloadBytes, cat::update::kMaxDownloadBytes}) {
        manifest.sizeBytes = bytes;
        CHECK(ParseManifest(SerializeManifest(manifest), parsed, error));
        CHECK(parsed.sizeBytes == bytes);
    }
    const auto valid = SerializeManifest(Example());
    for (const std::string value : {"0", "1023", "134217729", "18446744073709551615",
        "18446744073709551616", "-1024", "+1024", "01024", "1024 ", "1e6", "1024.0"})
        RejectWithoutMutation(SetField(valid, "size_bytes", value));
    for (const std::uint64_t bytes : {std::uint64_t{0}, cat::update::kMinDownloadBytes - 1,
                                      cat::update::kMaxDownloadBytes + 1}) {
        manifest.sizeBytes = bytes;
        CHECK(SerializeManifest(manifest).empty());
    }
    manifest = Example();
    manifest.notesUtf8.assign(cat::update::kMaxNotesBytes, 'a');
    const auto maximumNotes = SerializeManifest(manifest);
    CHECK(!maximumNotes.empty());
    CHECK(ParseManifest(maximumNotes, parsed, error));
    CHECK(parsed.notesUtf8 == manifest.notesUtf8);
    manifest.notesUtf8 += 'a';
    CHECK(SerializeManifest(manifest).empty());
    std::string tooManyNotes;
    for (int i = 0; i < 2731; ++i) tooManyNotes += "YWFh"; // 8193 decoded bytes.
    RejectWithoutMutation(SetField(valid, "notes_b64", tooManyNotes));

    // Exercise the outer limit independently of the decoded-note limit.
    manifest = Example();
    manifest.notesUtf8.clear();
    const auto baseSize = SerializeManifest(manifest).size();
    manifest.downloadUrl.append(cat::update::kMaxManifestBytes - baseSize, 'a');
    const auto maximumManifest = SerializeManifest(manifest);
    CHECK(maximumManifest.size() == cat::update::kMaxManifestBytes);
    CHECK(ParseManifest(maximumManifest, parsed, error));
    CHECK(parsed.downloadUrl == manifest.downloadUrl);
    RejectWithoutMutation(maximumManifest + '\n');
    manifest.downloadUrl += 'a';
    CHECK(SerializeManifest(manifest).empty());
}

void StrictBase64AndUtf8() {
    const auto valid = SerializeManifest(Example());
    Manifest parsed;
    std::string error;
    for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
        {"", ""}, {"TQ==", "M"}, {"TWE=", "Ma"}, {"TWFu", "Man"}, {"TWFuIQ==", "Man!"},
        {"wqA=", "\xc2\xa0"}, {"4oKs", u8"€"}, {"8J+Yug==", u8"😺"}}) {
        CHECK(ParseManifest(SetField(valid, "notes_b64", pair.first), parsed, error));
        CHECK(parsed.notesUtf8 == pair.second);
    }
    for (const std::string value : {
        "A", "AA", "AAA", "A===", "====", "TQ=", "TQ===", "TQ==AA==", "TWE==",
        "T Q==", "TQ==\t", "TR==", "TWF=", "TW=F", "@@==", "____", "----",
        "AA==", "TQAA", // Embedded NUL is forbidden even though base64 is well-formed.
        "wK8=", "4ICv", "7aCA", "9JCAgA==", "gA==", "wg==", "4oI=", "8J+Y",
        "77+", "//8=", "9aCAgA=="})
        RejectWithoutMutation(SetField(valid, "notes_b64", value));
    for (const std::string& notes : {
        std::string(1, '\0'), std::string("before\0after", 12), std::string("\xc0\xaf"),
        std::string("\xe0\x80\xaf"), std::string("\xed\xa0\x80"),
        std::string("\xf4\x90\x80\x80"), std::string("\x80"), std::string("\xc2"),
        std::string("\xe2\x82"), std::string("\xf0\x9f\x98"), std::string("\xff")}) {
        auto manifest = Example();
        manifest.notesUtf8 = notes;
        CHECK(SerializeManifest(manifest).empty());
    }
}

void InvalidWriterInput() {
    CHECK(SerializeManifest({}).empty());
    auto manifest = Example();
    manifest.versionText = "1.03.0";
    CHECK(SerializeManifest(manifest).empty());
    manifest.versionText = "1.3.1";
    CHECK(SerializeManifest(manifest).empty());
    manifest.versionText.clear();
    CHECK(SerializeManifest(manifest).empty());
    manifest = Example();
    manifest.downloadUrl = "https://user@example.com/update.exe";
    CHECK(SerializeManifest(manifest).empty());
    manifest = Example();
    manifest.sha256 = std::string(64, 'F');
    CHECK(SerializeManifest(manifest).empty());
}

} // namespace

int main() {
    try {
        NumericVersions();
        HttpsAddresses();
        CanonicalRoundTrips();
        InvalidFieldsAndLines();
        SizeAndNoteBoundaries();
        StrictBase64AndUtf8();
        InvalidWriterInput();
        std::cout << "PASS: 7 update protocol groups; " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
