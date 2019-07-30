#pragma once

#include "rti/trace/i_hit_counter.hpp"

namespace rti { namespace reflection {
  template<typename Ty>
  class i_reflection_model {
  public:
    // Pure Virtual Class
    virtual ~i_reflection_model() {}
    // Decides whether or not to reflect. If a reflection should happen, it sets
    // the origin and direction in the RTCRayHit object and returns true. If no
    // reflection should happen, then it does not change pRayhit and returns
    // false.
    virtual bool use(RTCRayHit&, rti::rng::i_rng&, rti::rng::i_rng::i_state&, rti::geo::i_abs_geometry<Ty> const&, rti::trace::i_hit_counter&) const = 0;
  };
}} // namespace