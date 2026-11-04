#pragma once
#include "cat_life.hpp"
#include "cat_memory_storage.hpp"

namespace cat::life::storage {
struct Model {
    using State = cat::life::State;
    static constexpr const char* Main = "life.ini";
    static constexpr const char* Backup = "life.bak";
    static State Initialize(const cat::PetState& legacy,std::int64_t now) { return FromLegacy(legacy,now); }
    static std::string Encode(const State& state) { return Serialize(state); }
    static bool Decode(const std::string& bytes,State& state,std::string& error) { return Parse(bytes,state,error); }
};
using Outcome = cat::memory::storage::Outcome;
struct Store : cat::memory::storage::BasicStore<Model> {
    template<class IO>
    void Load(IO& io,const cat::PetState& pet,std::int64_t now,bool newlyCreatedPet=false,std::uint64_t entropy=0) {
        const auto initial = newlyCreatedPet ? NewPet(pet,now,entropy) : FromLegacy(pet,now);
        cat::memory::storage::BasicStore<Model>::Load(io,pet,now,&initial);
    }
};
} // namespace cat::life::storage
