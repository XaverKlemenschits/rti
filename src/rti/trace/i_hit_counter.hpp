#pragma once

namespace rti { namespace trace {
  class i_hit_counter {
    // Pure Virtual Class
  public:
    virtual ~i_hit_counter() {}
    virtual void use(const RTCRayHit& pRayhit) = 0;
  };
}} // namespace