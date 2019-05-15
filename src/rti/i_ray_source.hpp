#pragma once

//#include<embree3/rtcore.h>

namespace rti {
  class i_ray_source {
  public:
    // Interface
    virtual~i_ray_source() {}
    // TODO: all functions added must be pure virtual
    virtual RTCRay get_ray() = 0;
  };
} // namespace rti
