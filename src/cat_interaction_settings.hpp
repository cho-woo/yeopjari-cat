#pragma once
#include "pet_state.hpp"
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace cat::interaction {
struct Settings {
    bool workInterference = false;
    bool seasonalEvents = true;
};
inline std::string Serialize(const Settings& value) {
    const std::string body = std::string("schema=1\nwork_interference=") +
        (value.workInterference ? "1" : "0") + "\nseasonal_events=" +
        (value.seasonalEvents ? "1" : "0") + "\n";
    std::ostringstream out; out.imbue(std::locale::classic());
    out << body << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << Checksum(body) << '\n';
    return out.str();
}
inline bool Parse(const std::string& bytes, Settings& result) {
    // Four exact encodings keep unknown/duplicate fields and damaged settings
    // from silently enabling cursor hunting after an update.
    if (bytes.size() > 256) return false;
    for (const bool interference : {false, true}) for (const bool seasonal : {false, true}) {
        const Settings candidate{interference, seasonal};
        if (bytes == Serialize(candidate)) { result = candidate; return true; }
    }
    return false;
}
} // namespace cat::interaction
