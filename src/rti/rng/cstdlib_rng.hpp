#pragma once

#include "i_rng.hpp"

namespace rti { namespace rng {
  class cstdlib_rng : public rti::rng::i_rng {
  public:
    // Define the state for this RNG
    struct state : public rti::rng::i_rng::i_state {

      state() : mSeed(1) {}

      state(unsigned int pSeed) :
         mSeed(pSeed) {}

      std::unique_ptr<rti::rng::i_rng::i_state> clone() const override final {
        return std::make_unique<state>(mSeed);
      }

      unsigned int mSeed;
    };

    // This function simply maps to  rand_r() in stdlib.h. For information on
    // the behaviour see, e.g., the man page.
    uint64_t get(rti::rng::i_rng::i_state& pState) const override final {
      // Precondition:
      // The parameter pState needs to be of type rti::rng::cstdlib_rng::state.
      // This sentence is verified in the following assertion.
      auto stateObjectForAssertion = state {};
      //std::cout << "*pState: " << typeid(*pState).name() << std::endl << std::endl;
      //std::cout
      // << "stateObjectForAssertion: "
      // << typeid(stateObjectForAssertion).name()
      // << std::endl << std::endl;
      assert(typeid(pState) == typeid(stateObjectForAssertion) && "Error: precondition violated");
      return (uint64_t) rand_r(&(reinterpret_cast<state*>(&pState)->mSeed));
    }

    uint64_t min() const override final {
      return 0;
    }

    uint64_t max() const override final {
      return RAND_MAX;
    }
  };
}} // namespace
