#include "../src/cat_memory_storage.hpp"
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using cat::memory::storage::Store;
using cat::memory::storage::Outcome;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #condition); } while (false)

struct FakeIO {
    std::map<std::string, std::string> files;
    std::map<std::string, std::string> preserved;
    std::set<std::string> unreadable, replaceDenied, preserveDenied;
    std::string writeDeniedPrefix, corruptWritePrefix, readDeniedPrefix;
    bool removeDenied = false;
    std::string concurrentMain, concurrentBackupOnWrite, concurrentMainAfterPreserve,
        concurrentMainAfterBackupReplace;
    bool deleteMainOnWrite = false, unreadableMainOnWrite = false;
    int writes = 0, replacements = 0;
    bool Exists(const std::string& name) { return files.find(name) != files.end(); }
    bool Read(const std::string& name, std::string& out) {
        if (unreadable.count(name) || (!readDeniedPrefix.empty() && name.rfind(readDeniedPrefix, 0) == 0)) return false;
        const auto it = files.find(name);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
    bool Write(const std::string& name, const std::string& bytes) {
        ++writes;
        if (Exists(name) || (!writeDeniedPrefix.empty() && name.rfind(writeDeniedPrefix, 0) == 0)) return false;
        files.emplace(name, !corruptWritePrefix.empty() && name.rfind(corruptWritePrefix, 0) == 0 ? bytes.substr(0, 20) : bytes);
        if (!concurrentMain.empty()) {
            files["memory.ini"] = concurrentMain;
            concurrentMain.clear();
        }
        if (!concurrentBackupOnWrite.empty()) {
            files["memory.bak"] = concurrentBackupOnWrite;
            concurrentBackupOnWrite.clear();
        }
        if (deleteMainOnWrite) { files.erase("memory.ini"); deleteMainOnWrite = false; }
        if (unreadableMainOnWrite) { unreadable.insert("memory.ini"); unreadableMainOnWrite = false; }
        return true;
    }
    bool Replace(const std::string& temporary, const std::string& destination) {
        ++replacements;
        if (replaceDenied.count(destination) || !Exists(temporary)) return false;
        files[destination] = files.at(temporary);
        files.erase(temporary);
        if (destination == "memory.bak" && !concurrentMainAfterBackupReplace.empty()) {
            files["memory.ini"] = concurrentMainAfterBackupReplace;
            concurrentMainAfterBackupReplace.clear();
        }
        return true;
    }
    bool Preserve(const std::string& name) {
        if (preserveDenied.count(name) || !Exists(name)) return false;
        preserved.emplace(name + ".preserved-" + std::to_string(preserved.size()), files.at(name));
        if (!concurrentMainAfterPreserve.empty()) {
            files["memory.ini"] = concurrentMainAfterPreserve;
            concurrentMainAfterPreserve.clear();
        }
        return true;
    }
    bool Remove(const std::string& name) {
        if (removeDenied) return false;
        return files.erase(name) > 0;
    }
};

constexpr std::int64_t born = 1700000000;
constexpr std::int64_t now = born + 86400;
cat::PetState Legacy() { return cat::NewPet(born); }
cat::memory::State Memory(std::int64_t updated = now, std::int64_t birthday = born) {
    auto legacy = cat::NewPet(birthday);
    return cat::memory::FromLegacy(legacy, updated);
}
std::string Bytes(std::int64_t updated = now, std::int64_t birthday = born) {
    const auto bytes = cat::memory::Serialize(Memory(updated, birthday));
    CHECK(!bytes.empty());
    return bytes;
}
void NoTemps(const FakeIO& io) {
    for (const auto& entry : io.files) CHECK(entry.first.find(".tmp-") == std::string::npos);
}
void Preserved(const FakeIO& io, const std::string& bytes) {
    bool found = false;
    for (const auto& entry : io.preserved) found = found || entry.second == bytes;
    CHECK(found);
}
void Advance(Store& store) { cat::memory::Advance(store.state, store.state.updatedUtc + 60); }

void MigrationAndRestart() {
    FakeIO io;
    io.files["preferences.ini"] = "unrelated settings";
    auto legacy = Legacy();
    const auto legacyBefore = cat::Serialize(legacy);
    Store store;
    CHECK(!store.enabled());
    CHECK(!store.Save(io));
    store.Load(io, legacy, now);
    CHECK(store.outcome == Outcome::Migrated && store.enabled());
    CHECK(store.state.bornUtc == born);
    CHECK(cat::Serialize(legacy) == legacyBefore);
    CHECK(io.writes == 0);
    CHECK(store.Save(io));
    const auto initial = io.files.at("memory.ini");
    CHECK(!io.Exists("memory.bak"));
    Store restart;
    restart.Load(io, legacy, now + 60);
    CHECK(restart.outcome == Outcome::Loaded);
    CHECK(cat::memory::Serialize(restart.state) == initial);
    Advance(restart);
    const auto newer = cat::memory::Serialize(restart.state);
    CHECK(restart.Save(io));
    CHECK(io.files.at("memory.bak") == initial);
    CHECK(io.files.at("memory.ini") == newer);
    CHECK(io.files.at("preferences.ini") == "unrelated settings");
    NoTemps(io);
}

void MainIsAuthoritativeAndBackupTracksLastHealthyMain() {
    FakeIO io;
    const auto main = Bytes(now), backup = Bytes(now - 60);
    io.files = {{"memory.ini", main}, {"memory.bak", backup}};
    Store store;
    store.Load(io, Legacy(), now);
    CHECK(store.outcome == Outcome::Loaded);
    CHECK(store.state.updatedUtc == now);
    Advance(store);
    CHECK(store.Save(io));
    CHECK(io.files.at("memory.bak") == main);
    const auto saved = io.files.at("memory.ini");
    Store restart;
    restart.Load(io, Legacy(), now + 120);
    CHECK(restart.state.updatedUtc == now + 60);
    Advance(restart);
    CHECK(restart.Save(io));
    CHECK(io.files.at("memory.bak") == saved);
    CHECK(io.preserved.empty());
}

void BackupRecoveryPreservesCorruption() {
    for (const std::string& damaged : {std::string("truncated"), Bytes(now, born - 86400)}) {
        FakeIO io;
        const auto backup = Bytes(now - 60);
        io.files = {{"memory.ini", damaged}, {"memory.bak", backup}};
        Store store;
        store.Load(io, Legacy(), now);
        CHECK(store.outcome == Outcome::Recovered);
        CHECK(store.state.updatedUtc == now - 60);
        Advance(store);
        CHECK(store.Save(io));
        CHECK(io.files.at("memory.bak") == backup);
        Preserved(io, damaged);
        const auto saved = io.files.at("memory.ini");
        Store restart;
        restart.Load(io, Legacy(), now);
        CHECK(restart.outcome == Outcome::Loaded);
        CHECK(cat::memory::Serialize(restart.state) == saved);
        NoTemps(io);
    }
    FakeIO missing;
    missing.files["memory.bak"] = Bytes();
    Store store;
    store.Load(missing, Legacy(), now);
    CHECK(store.outcome == Outcome::Recovered);
    CHECK(store.Save(missing));
    CHECK(missing.files.at("memory.ini") == missing.files.at("memory.bak"));
}

void InvalidHistoryNeverSilentlyMigrates() {
    for (const auto& files : {
        std::map<std::string,std::string>{{"memory.ini", "truncated"}},
        std::map<std::string,std::string>{{"memory.bak", "broken"}},
        std::map<std::string,std::string>{{"memory.ini", "truncated"}, {"memory.bak", "broken"}},
        std::map<std::string,std::string>{{"memory.ini", Bytes(now, born - 86400)}}}) {
        FakeIO io; io.files = files;
        Store store;
        store.Load(io, Legacy(), now);
        CHECK(store.outcome == Outcome::Blocked && !store.enabled());
        CHECK(!store.error.empty());
        CHECK(!store.Save(io));
        CHECK(io.files == files);
        CHECK(io.writes == 0 && io.replacements == 0);
    }
    FakeIO io;
    io.files["memory.ini"] = Bytes();
    io.unreadable.insert("memory.ini");
    Store unreadable;
    unreadable.Load(io, Legacy(), now);
    CHECK(unreadable.outcome == Outcome::Blocked);
    CHECK(!unreadable.Save(io));
}

void CorruptBackupAndPreservationFailure() {
    FakeIO io;
    const auto main = Bytes();
    io.files = {{"memory.ini", main}, {"memory.bak", "broken backup"}};
    Store store;
    store.Load(io, Legacy(), now);
    Advance(store);
    io.preserveDenied.insert("memory.bak");
    CHECK(!store.Save(io));
    CHECK(io.files.at("memory.ini") == main && io.files.at("memory.bak") == "broken backup");
    NoTemps(io);
    io.preserveDenied.clear();
    CHECK(store.Save(io));
    CHECK(store.error.empty());
    Preserved(io, "broken backup");
    CHECK(io.files.at("memory.bak") == main);

    io.files["memory.ini"] = "broken main";
    io.preserveDenied.insert("memory.ini");
    Store recovered;
    recovered.Load(io, Legacy(), now);
    CHECK(recovered.outcome == Outcome::Recovered);
    const auto before = io.files;
    CHECK(!recovered.Save(io));
    CHECK(io.files == before);
    NoTemps(io);
}

void FailedWritesAndReadbackLeaveOriginalFiles() {
    for (int failure = 0; failure < 5; ++failure) {
        FakeIO io;
        io.files = {{"memory.ini", Bytes()}, {"memory.bak", Bytes(now - 60)}};
        const auto before = io.files;
        Store store;
        store.Load(io, Legacy(), now);
        Advance(store);
        if (failure == 0) io.writeDeniedPrefix = "memory.ini.tmp-";
        if (failure == 1) io.writeDeniedPrefix = "memory.bak.tmp-";
        if (failure == 2) io.corruptWritePrefix = "memory.ini.tmp-";
        if (failure == 3) io.corruptWritePrefix = "memory.bak.tmp-";
        if (failure == 4) io.readDeniedPrefix = "memory.ini.tmp-";
        CHECK(!store.Save(io));
        CHECK(!store.error.empty());
        CHECK(io.files == before);
        CHECK(io.replacements == 0);
        NoTemps(io);
    }
}

void AtomicReplaceFailuresRemainRecoverable() {
    for (const std::string& destination : {std::string("memory.ini"), std::string("memory.bak")}) {
        FakeIO io;
        const auto main = Bytes(), oldBackup = Bytes(now - 60);
        io.files = {{"memory.ini", main}, {"memory.bak", oldBackup}};
        Store store;
        store.Load(io, Legacy(), now);
        Advance(store);
        io.replaceDenied.insert(destination);
        CHECK(!store.Save(io));
        CHECK(io.files.at("memory.ini") == main);
        CHECK(io.files.at("memory.bak") == (destination == "memory.ini" ? main : oldBackup));
        Store restart;
        restart.Load(io, Legacy(), now + 60);
        CHECK(restart.outcome == Outcome::Loaded);
        CHECK(cat::memory::Serialize(restart.state) == main);
        NoTemps(io);
        io.replaceDenied.clear();
        CHECK(store.Save(io));
    }
}

void ConcurrentHistoryIsNotOverwritten() {
    for (const std::string& name : {std::string("memory.ini"), std::string("memory.bak")}) {
        FakeIO io;
        io.files = {{"memory.ini", Bytes()}, {"memory.bak", Bytes(now - 60)}};
        Store stale;
        stale.Load(io, Legacy(), now);
        Advance(stale);
        io.files[name] = Bytes(now + 300);
        const auto changed = io.files;
        CHECK(!stale.Save(io));
        CHECK(io.files == changed);
        CHECK(io.writes == 0);
        CHECK(stale.error.find("changed on disk") != std::string::npos);
    }
    FakeIO io;
    io.files["memory.ini"] = Bytes();
    Store stale;
    stale.Load(io, Legacy(), now);
    Advance(stale);
    io.concurrentMain = Bytes(now + 300);
    CHECK(!stale.Save(io));
    CHECK(io.files.at("memory.ini") == Bytes(now + 300));
    CHECK(io.replacements == 0);
    NoTemps(io);

    FakeIO empty;
    Store migrating;
    migrating.Load(empty, Legacy(), now);
    empty.files["memory.ini"] = Bytes(now + 60);
    CHECK(!migrating.Save(empty));
    CHECK(empty.files.at("memory.ini") == Bytes(now + 60));
}

void InvalidChangesDuringSaveAreNeverOverwritten() {
    for (int mutation = 0; mutation < 5; ++mutation) {
        FakeIO io;
        const auto main = Bytes(), backup = Bytes(now - 60);
        io.files = {{"memory.ini", main}, {"memory.bak", backup}};
        Store store;
        store.Load(io, Legacy(), now);
        Advance(store);
        if (mutation == 0) io.concurrentMain = "new damaged history";
        if (mutation == 1) io.concurrentMain = Bytes(now, born - 86400);
        if (mutation == 2) io.concurrentBackupOnWrite = "new damaged backup";
        if (mutation == 3) io.deleteMainOnWrite = true;
        if (mutation == 4) io.unreadableMainOnWrite = true;
        CHECK(!store.Save(io));
        CHECK(io.replacements == 0);
        CHECK(store.error.find("changed on disk") != std::string::npos);
        if (mutation == 0) CHECK(io.files.at("memory.ini") == "new damaged history");
        if (mutation == 1) CHECK(io.files.at("memory.ini") == Bytes(now, born - 86400));
        if (mutation == 2) CHECK(io.files.at("memory.bak") == "new damaged backup");
        if (mutation == 3) CHECK(!io.Exists("memory.ini"));
        if (mutation == 4) CHECK(io.files.at("memory.ini") == main);
        NoTemps(io);
    }
    for (bool corruptMain : {false, true}) {
        FakeIO io;
        io.files = {{"memory.ini", corruptMain ? "old damage" : Bytes()},
                    {"memory.bak", corruptMain ? Bytes() : "old backup damage"}};
        Store store;
        store.Load(io, Legacy(), now);
        Advance(store);
        io.concurrentMainAfterPreserve = "new damage after preservation";
        CHECK(!store.Save(io));
        CHECK(io.files.at("memory.ini") == "new damage after preservation");
        CHECK(io.replacements == 0);
        Preserved(io, corruptMain ? "old damage" : "old backup damage");
        NoTemps(io);
    }
    FakeIO io;
    const auto main = Bytes();
    io.files = {{"memory.ini", main}, {"memory.bak", Bytes(now - 60)}};
    Store store;
    store.Load(io, Legacy(), now);
    Advance(store);
    io.concurrentMainAfterBackupReplace = "new damage during backup rotation";
    CHECK(!store.Save(io));
    CHECK(io.files.at("memory.ini") == "new damage during backup rotation");
    CHECK(io.files.at("memory.bak") == main);
    CHECK(io.replacements == 1);
    NoTemps(io);
}

void ExistingTempsAndInvalidInMemoryState() {
    FakeIO io;
    io.files["memory.ini.tmp-0"] = "stale or unrelated file must not be truncated";
    Store store;
    store.Load(io, Legacy(), now);
    CHECK(store.Save(io));
    CHECK(io.files.at("memory.ini.tmp-0") == "stale or unrelated file must not be truncated");
    const auto before = io.files;
    store.state.bornUtc -= 86400;
    CHECK(!store.Save(io));
    CHECK(io.files == before);
    store.Load(io, Legacy(), now);
    store.state.bond = std::numeric_limits<double>::quiet_NaN();
    CHECK(!store.Save(io));
    CHECK(io.files == before);
    io.files["memory.ini"] = "bad";
    store.Load(io, Legacy(), now);
    CHECK(!store.enabled());
    CHECK(!store.Save(io));
}

void ReadFailureRecoveryAndDamageAfterLoad() {
    FakeIO io;
    const auto backup = Bytes(now - 60), main = Bytes();
    io.files = {{"memory.ini", main}, {"memory.bak", backup}};
    io.unreadable.insert("memory.ini");
    Store store;
    store.Load(io, Legacy(), now);
    CHECK(store.outcome == Outcome::Recovered);
    CHECK(!store.Save(io));
    CHECK(io.files.at("memory.ini") == main && io.files.at("memory.bak") == backup);
    CHECK(io.writes == 0);
    io.unreadable.clear();
    store.Load(io, Legacy(), now);
    CHECK(store.outcome == Outcome::Loaded);
    io.files["memory.ini"] = "damage after load";
    Advance(store);
    CHECK(store.Save(io));
    CHECK(io.files.at("memory.bak") == backup);
    Preserved(io, "damage after load");
}
}

int main() {
    try {
        MigrationAndRestart();
        MainIsAuthoritativeAndBackupTracksLastHealthyMain();
        BackupRecoveryPreservesCorruption();
        InvalidHistoryNeverSilentlyMigrates();
        CorruptBackupAndPreservationFailure();
        FailedWritesAndReadbackLeaveOriginalFiles();
        AtomicReplaceFailuresRemainRecoverable();
        ConcurrentHistoryIsNotOverwritten();
        InvalidChangesDuringSaveAreNeverOverwritten();
        ExistingTempsAndInvalidInMemoryState();
        ReadFailureRecoveryAndDamageAfterLoad();
        std::cout << "Memory storage: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
