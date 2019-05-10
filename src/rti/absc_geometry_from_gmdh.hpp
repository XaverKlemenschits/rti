#pragma once

#include "rti/i_geometry_from_gmsh.hpp"
#include "rti/utils.hpp"

namespace rti {
class absc_geometry_from_gmsh : public i_geometry_from_gmsh {
  public:
    virtual ~i_geometry_from_gmsh() {};
    virtual void invert_surface_normals() = 0;
    virtual std::string prim_to_string(unsigned int) = 0;
    RTCGeometry& get_rtc_geometry() {
      return mGeometry;
    }
  protected:
    RTCGeometry mGeometry;
};
} // namespace rti