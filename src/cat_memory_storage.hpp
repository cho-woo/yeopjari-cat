#pragma once

#include "cat_memory.hpp"
#include <cstdint>
#include <string>

namespace cat::memory::storage {

// IO resolves these names inside the existing per-user YeopjariCat directory.
// Exists must return true if presence cannot be determined. Write creates a
// NEW file and cleans up its own partial file on failure; it must never truncate
// an existing path. Replace atomically consumes a successfully written temp.
// Preserve makes an immutable, uniquely named copy and must not overwrite an
// older preservation. Remove is called only for temps this Store created.
inline constexpr const char* kMain = "memory.ini";
inline constexpr const char* kBackup = "memory.bak";
enum class Outcome { Loaded, Recovered, Migrated, Blocked };

struct Store {
    State state{};
    Outcome outcome = Outcome::Blocked;
    std::string error;

    bool enabled() const { return outcome != Outcome::Blocked; }

    template<class IO>
    void Load(IO& io, const cat::PetState& legacy, std::int64_t now) {
        outcome = Outcome::Blocked;
        error.clear();
        expectedBornUtc_ = legacy.bornUtc;
        const auto main = Inspect(io, kMain);
        const auto backup = Inspect(io, kBackup);
        knownMain_ = main; knownBackup_ = backup;
        if (main.valid) {
            state = main.state;
            outcome = Outcome::Loaded;
        } else if (backup.valid) {
            state = backup.state;
            outcome = Outcome::Recovered;
        } else if (!main.exists && !backup.exists) {
            State migrated = FromLegacy(legacy, now), checked;
            const std::string bytes = Serialize(migrated);
            if (bytes.empty() || !Validate(bytes, checked)) {
                error = "Cannot initialize memory from the legacy birthday";
                return;
            }
            state = migrated;
            outcome = Outcome::Migrated;
        } else {
            error = "No valid memory record for this birthday; existing history was preserved";
        }
    }

    template<class IO>
    bool Save(IO& io) {
        if (!enabled()) {
            if (error.empty()) error = "Memory saving is blocked until a valid record is loaded";
            return false;
        }
        error.clear();
        State checked;
        const std::string bytes = Serialize(state);
        if (bytes.empty() || !Validate(bytes, checked)) {
            error = "Refusing to save invalid memory or a changed birthday";
            return false;
        }
        const auto main = Inspect(io, kMain);
        const auto backup = Inspect(io, kBackup);
        if (!Unchanged(main, backup)) return false;
        if ((main.exists && !main.readable) || (backup.exists && !backup.readable)) {
            error = "Cannot safely replace unreadable memory history; restore read access and reload";
            return false;
        }
        std::string mainTemp, backupTemp;
        if (!WriteCheckedTemp(io, kMain, bytes, mainTemp)) return false;
        const auto cleanup = [&] {
            if (!mainTemp.empty()) io.Remove(mainTemp);
            if (!backupTemp.empty()) io.Remove(backupTemp);
        };
        // A healthy main is the only source eligible for backup rotation. A
        // recovered save must retain its healthy backup, never copy bad main
        // bytes over the last readable history.
        if (main.valid && !WriteCheckedTemp(io, kBackup, main.bytes, backupTemp)) {
            cleanup();
            return false;
        }
        if (main.exists && !main.valid && !io.Preserve(kMain)) {
            error = "Cannot preserve the damaged main memory record";
            cleanup();
            return false;
        }
        if (main.valid && backup.exists && !backup.valid && !io.Preserve(kBackup)) {
            error = "Cannot preserve the damaged backup memory record";
            cleanup();
            return false;
        }
        // Detect another writer/restored history again after potentially slow
        // temp writes and preservation. The IO caller must hold a transaction
        // lock across Load/Save to close the final check/replace race.
        if (!SameFiles(Inspect(io, kMain), Inspect(io, kBackup), main, backup)) {
            cleanup();
            return false;
        }
        if (!backupTemp.empty()) {
            if (!io.Replace(backupTemp, kBackup)) {
                error = "Cannot atomically replace the memory backup";
                cleanup();
                return false;
            }
            backupTemp.clear();
            knownBackup_ = main;
        }
        // Backup replacement can itself be slow. An external editor may not
        // respect the application mutex, so recheck main immediately before
        // its replacement, including corruption, disappearance and read errors.
        if (!SameFiles(Inspect(io, kMain), Inspect(io, kBackup), main,
                       main.valid ? main : backup)) {
            cleanup();
            return false;
        }
        if (!io.Replace(mainTemp, kMain)) {
            error = "Cannot atomically replace the main memory record";
            cleanup();
            return false;
        }
        mainTemp.clear();
        knownMain_ = {true, true, checked, bytes, true};
        return true;
    }

private:
    std::int64_t expectedBornUtc_ = 0;
    struct Record {
        bool exists = false, valid = false;
        State state{};
        std::string bytes;
        bool readable = false;
    };

    Record knownMain_, knownBackup_;

    bool SameFiles(const Record& main, const Record& backup,
                   const Record& expectedMain, const Record& expectedBackup) {
        const auto same = [](const Record& a, const Record& b) {
            return a.exists == b.exists && a.readable == b.readable &&
                a.valid == b.valid && a.bytes == b.bytes;
        };
        if (same(main, expectedMain) && same(backup, expectedBackup)) return true;
        error = "Memory files changed on disk during save; reload before retrying";
        return false;
    }

    bool Unchanged(const Record& main, const Record& backup) {
        const auto changed = [](const Record& current, const Record& known) {
            return current.valid && (!known.valid || current.bytes != known.bytes);
        };
        if (!changed(main, knownMain_) && !changed(backup, knownBackup_)) return true;
        error = "Memory history changed on disk; reload before saving to avoid overwriting another session";
        return false;
    }

    bool Validate(const std::string& bytes, State& parsed) const {
        std::string parseError;
        return Parse(bytes, parsed, parseError) && parsed.bornUtc == expectedBornUtc_;
    }

    template<class IO>
    Record Inspect(IO& io, const std::string& name) const {
        Record record;
        record.exists = io.Exists(name);
        if (record.exists) {
            record.readable = io.Read(name, record.bytes);
            if (record.readable) record.valid = Validate(record.bytes, record.state);
        }
        return record;
    }

    template<class IO>
    bool WriteCheckedTemp(IO& io, const std::string& destination,
                          const std::string& bytes, std::string& written) {
        std::string candidate;
        for (unsigned i = 0; i < 256; ++i) {
            const auto name = destination + ".tmp-" + std::to_string(i);
            if (!io.Exists(name)) { candidate = name; break; }
        }
        if (candidate.empty()) {
            error = "No unused memory temporary filename is available";
            return false;
        }
        if (!io.Write(candidate, bytes)) {
            error = "Cannot create the memory temporary file";
            return false;
        }
        written = candidate;
        std::string readback;
        State parsed;
        if (!io.Read(candidate, readback) || readback != bytes || !Validate(readback, parsed)) {
            error = "Memory temporary file failed read-back validation";
            io.Remove(candidate);
            written.clear();
            return false;
        }
        return true;
    }
};

} // namespace cat::memory::storage
