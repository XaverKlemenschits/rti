#pragma once

#include <embree3/rtcore.h>
#include <gmsh.h>

#include "rti/gmsh_reader.hpp"
#include "rti/logger.hpp"
#include "rti/absc_geometry_from_gmsh.hpp"

namespace rti {
  class triangle_geometry_from_gmsh : public absc_geometry_from_gmsh {
  public:
    // triangle_geometry_from_gmsh(RTCDevice& pDevice) {
    //   init_this(pDevice);
    // }
    triangle_geometry_from_gmsh(RTCDevice& pDevice, gmsh_reader& pGmshReader) :
      absc_geometry_from_gmsh(pDevice) {
      init_this(pDevice, pGmshReader);
    }
    void invert_surface_normals() override {
      for (size_t idx = 0; idx < mNumTriangles; ++idx) {
        std::swap<uint32_t>(mTTBuffer[idx].v1, mTTBuffer[idx].v2);
      }
    }
    std::string to_string() override {
      std::stringstream strstream;
      strstream << "(:class triangle_geometry_from_gmsh ";
      for (size_t idxA = 0; idxA < mNumTriangles; ++idxA) {
        strstream << this->prim_to_string(idxA);
        if (idxA < mNumTriangles-1) {
          strstream << " ";
        }
      }
      strstream << ")";
      return strstream.str();
    }
    std::string prim_to_string(unsigned int pPrimID) override {
      std::stringstream strstream;
      strstream << "(" << mVVBuffer[mTTBuffer[pPrimID].v0].xx
                << " " << mVVBuffer[mTTBuffer[pPrimID].v0].yy
                << " " << mVVBuffer[mTTBuffer[pPrimID].v0].zz << ")"
                << "(" << mVVBuffer[mTTBuffer[pPrimID].v1].xx
                << " " << mVVBuffer[mTTBuffer[pPrimID].v1].yy
                << " " << mVVBuffer[mTTBuffer[pPrimID].v1].zz << ")"
                << "(" << mVVBuffer[mTTBuffer[pPrimID].v2].xx
                << " " << mVVBuffer[mTTBuffer[pPrimID].v2].yy
                << " " << mVVBuffer[mTTBuffer[pPrimID].v2].zz << ")";
      return strstream.str();
    }
  private:
    ////////////////////////
    // Local algebraic types
    ////////////////////////
    struct vertex_f3_t {
      float xx, yy, zz; // No padding here!
      // "RTC_GEOMETRY_TYPE_TRIANGLE: The vertex buffer contains an array of
      // single precision x, y, z floating point coordinates
      // (RTC_FORMAT_FLOAT3 format), and the number of vertices are inferred
      // from the size of that buffer. "
      // Source: https://embree.github.io/api.html#rtc_geometry_type_triangle
    };
    // Does Embree need aligned memory?
    struct alignas(4) triangle_t {
      uint32_t v0, v1, v2;
      //int v0, v1, v2;
      // "RTC_GEOMETRY_TYPE_TRIANGLE: The index buffer contains an array of three
      // 32-bit indices per triangle (RTC_FORMAT_UINT format)"
      // Source: https://embree.github.io/api.html#rtc_geometry_type_triangle
    };
    ///////////////
    // Data members
    ///////////////
    triangle_t* mTTBuffer = nullptr;
    vertex_f3_t* mVVBuffer = nullptr;
    size_t mNumTriangles = 0;
    size_t mNumVertices = 0;
    ////////////
    // Functions
    ////////////
    void init_this(RTCDevice& pDevice, gmsh_reader& pGmshReader) {
      // "Points with per vertex radii are supported with sphere, ray-oriented
      // discs, and normal-oriented discs geometric represetntations. Such point
      // geometries are created by passing RTC_GEOMETRY_TYPE_SPHERE_POINT,
      // RTC_GEOMETRY_TYPE_DISC_POINT, or RTC_GEOMETRY_TYPE_ORIENTED_DISC_POINT to
      // the rtcNewGeometry function."
      //
      // "RTC_GEOMETRY_TYPE_SPHERE_POINT -
      //   point geometry spheres
      // RTC_GEOMETRY_TYPE_DISC_POINT -
      //   point geometry with ray-oriented discs
      // RTC_GEOMETRY_TYPE_ORIENTED_DISC_POINT -
      //   point geometry with normal-oriented discs"
      //
      // Source: https://embree.github.io/api.html#rtc_geometry_type_point

      // RTC_GEOMETRY_TYPE_TRIANGLE: https://embree.github.io/api.html#rtc_geometry_type_triangle
      // "The index buffer contains an array of three 32-bit indices per triangle (RTC_FORMAT_UINT
      // format) and the number of primitives is inferred from the size of that buffer. The vertex
      // buffer contains an array of single precision x, y, z floating point coordinates
      // (RTC_FORMAT_FLOAT3 format), and the number of vertices are inferred from the size of that
      // buffer."
      mGeometry = rtcNewGeometry(pDevice, RTC_GEOMETRY_TYPE_TRIANGLE);

      std::vector<rti::triple_t<double> > vertices = pGmshReader.get_vertices();

      // The vertex buffer contains an array of single precision x, y, z floating
      // point coordinates (RTC_FORMAT_FLOAT3 format).
      mNumVertices = vertices.size();
      // Acquire memory from Embree
      mVVBuffer = (vertex_f3_t*)
        rtcSetNewGeometryBuffer(
                                mGeometry,
                                RTC_BUFFER_TYPE_VERTEX,
                                0, /* the slot */
                                RTC_FORMAT_FLOAT3,
                                sizeof(vertex_f3_t),
                                mNumVertices);

      // Write vertices to Embree
      for (size_t idx = 0; idx < vertices.size(); ++idx) {
        auto& triple = vertices[idx];
        mVVBuffer[idx].xx = std::get<0>(triple);
        mVVBuffer[idx].yy = std::get<1>(triple);
        mVVBuffer[idx].zz = std::get<2>(triple);
      }

      std::vector<rti::triple_t<size_t> > triangles = pGmshReader.get_triangles();
      mNumTriangles = triangles.size();
      // Acquire memory from Embree
      mTTBuffer = (triangle_t*)
        rtcSetNewGeometryBuffer(
                                mGeometry,
                                RTC_BUFFER_TYPE_INDEX,
                                0, // slot
                                RTC_FORMAT_UINT3,
                                sizeof(triangle_t),
                                mNumTriangles);

      // Check Embree device error
      if (RTC_ERROR_NONE != rtcGetDeviceError(pDevice)) {
        BOOST_LOG_SEV(rti::mRLogger, blt::debug) << "Embree device error after rtcSetNewGeometryBuffer()";
      }

      // Write triangle to Embree
      for (size_t idx = 0; idx < triangles.size(); ++idx) {
        auto& triple = triangles[idx];
        mTTBuffer[idx].v0 = std::get<0>(triple);
        mTTBuffer[idx].v1 = std::get<1>(triple);
        mTTBuffer[idx].v2 = std::get<2>(triple);
        // assert(0 <= mTTBuffer[idx].v0); // not necessary; unsigned
        assert(mTTBuffer[idx].v0 < mNumVertices && "Invalid Vertex");
        assert(mTTBuffer[idx].v1 < mNumVertices && "Invalid Vertex");
        assert(mTTBuffer[idx].v2 < mNumVertices && "Invalid Vertex");
        //BOOST_LOG_SEV(rti::mRLogger, blt::debug) << "(" << mVVBuffer[mTTBuffer[idx].v0].xx
        //                                  << "," << mVVBuffer[mTTBuffer[idx].v0].yy
        //                                  << "," << mVVBuffer[mTTBuffer[idx].v0].zz << ")"
        //                                  << "(" << mVVBuffer[mTTBuffer[idx].v1].xx
        //                                  << "," << mVVBuffer[mTTBuffer[idx].v1].yy
        //                                  << "," << mVVBuffer[mTTBuffer[idx].v1].zz << ")"
        //                                  << "(" << mVVBuffer[mTTBuffer[idx].v2].xx
        //                                  << "," << mVVBuffer[mTTBuffer[idx].v2].yy
        //                                  << "," << mVVBuffer[mTTBuffer[idx].v2].zz << ")";
      }
    }
  };
}
