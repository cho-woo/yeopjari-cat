#pragma once

#include "update_protocol.hpp"
#include <cstdint>
#include <string>

namespace cat::update {

// A notice is separate from checking or installing an update. It never opens a
// dialog, starts a download, changes focus, or changes the pet's saved identity.
inline constexpr wchar_t kCatUpdateMessage[] = L"프로그램 업데이트\n해줘랴 냥!";

struct NoticeContext {
    std::uint64_t now = 0, notBefore = 0;
    bool enabled = false, visible = false, calm = false;
    bool busy = true, bubbleVisible = true;
};

class Announcement {
    std::string announced_, pending_;
public:
    // Only a successfully verified feed should reach this method. An equal,
    // older or invalid version also cancels a previously pending announcement.
    bool SetAvailable(const Version& current, const std::string& versionText) {
        Version version;
        if (!ParseVersion(versionText, version) || Compare(version, current) <= 0) {
            pending_.clear();
            return false;
        }
        pending_ = versionText == announced_ ? std::string{} : versionText;
        return true;
    }
    bool Ready(const NoticeContext& context) const {
        return !pending_.empty() && pending_ != announced_ &&
            context.enabled && context.visible && context.calm &&
            !context.busy && !context.bubbleVisible && context.now >= context.notBefore;
    }
    void MarkAnnounced() {
        if (!pending_.empty()) { announced_ = pending_; pending_.clear(); }
    }
    const std::string& LastAnnounced() const { return announced_; }
    std::string Serialize() const {
        return announced_.empty() ? std::string{} : "schema=1\nversion=" + announced_ + "\n";
    }
    bool Restore(const std::string& text) {
        constexpr char prefix[] = "schema=1\nversion=";
        constexpr std::size_t start = sizeof(prefix) - 1;
        if (text.size() <= start + 1 || text.size() > 96 || text.compare(0, start, prefix) != 0 || text.back() != '\n')
            return false;
        const std::string value = text.substr(start, text.size() - start - 1);
        Version version;
        if (!ParseVersion(value, version) || ToString(version) != value) return false;
        announced_ = value;
        return true;
    }
};

} // namespace cat::update
