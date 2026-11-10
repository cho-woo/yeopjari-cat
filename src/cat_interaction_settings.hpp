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
    bool cursorPlay = true;
};
inline std::string CheckedBody(const std::string& body) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << body << "checksum=" << std::hex << std::setw(16) << std::setfill('0') << Checksum(body) << '\n';
    return out.str();
}
inline std::string Serialize(const Settings& value) {
    return CheckedBody(std::string("schema=2\nwork_interference=")+
        (value.workInterference?"1":"0")+"\nseasonal_events="+(value.seasonalEvents?"1":"0")+
        "\ncursor_play="+(value.cursorPlay?"1":"0")+"\n");
}
inline bool Parse(const std::string& bytes, Settings& result) {
    // Exact checksummed encodings preserve an explicit cursor-play choice and
    // reject unknown/duplicate fields. Older settings acquire the new nearby
    // play default without enabling unrelated work-interference events.
    if (bytes.size() > 256) return false;
    for (const bool interference : {false, true}) for (const bool seasonal : {false, true}) {
        for(const bool cursor:{false,true}) {
            const Settings candidate{interference,seasonal,cursor};
            if(bytes==Serialize(candidate)){result=candidate;return true;}
        }
        const auto legacy=CheckedBody(std::string("schema=1\nwork_interference=")+
            (interference?"1":"0")+"\nseasonal_events="+(seasonal?"1":"0")+"\n");
        if(bytes==legacy){result={interference,seasonal,true};return true;}
    }
    return false;
}
} // namespace cat::interaction
