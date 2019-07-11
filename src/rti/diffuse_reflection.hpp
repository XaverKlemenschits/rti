#pragma once

#include <cmath>

#include "rti/cos_hemi.hpp"
#include "rti/cstdlib_rng.hpp"
#include "rti/i_reflection_model.hpp"
#include "rti/i_rng.hpp"

namespace rti {
  class diffuse_reflection : public i_reflection_model {
  public:
    diffuse_reflection(double pStickingC) :
      mStickingC(pStickingC) {
      RLOG_ERROR << "WARNING: This code inverts the surface normals (gmsh input)" << std::endl;
    }

    bool use(
             RTCRayHit& pRayhit,
             rti::i_rng& pRng,
             rti::i_rng::i_state& pRngState,
             const i_geometry& pGeometry,
             i_hit_counter& pHitcounter) const override final {

      float epsilon = 1e-6;
      // THIS CODE IS HAND-CRAFTED FOR THE CYLINDER WITH SOURCE PLANE AT X == 0.
      if (pRayhit.ray.org_x + pRayhit.ray.dir_x * pRayhit.ray.tfar <= epsilon) {
        // Don't reflect and don't count
        return false;
      }

      /* Get random number and decide whether or not to reflect. */
      uint64_t rndm = pRng.get(pRngState);
      if (rndm <= (pRng.max() * mStickingC)) {
        // Do not reflect
        pHitcounter.use(pRayhit);
        return false;
      }

      /* Reflect */
      //
      /* Get surface normal at intersection */
      //rti::triple<float> pGeometry.get_normal(pRayhit.hit.geomID);
      rti::triple<float> normalO {pRayhit.hit.Ng_x, pRayhit.hit.Ng_y, pRayhit.hit.Ng_z};

      float xxahit = pRayhit.ray.org_x + pRayhit.ray.dir_x * pRayhit.ray.tfar;
      float yyahit = pRayhit.ray.org_y + pRayhit.ray.dir_y * pRayhit.ray.tfar;
      float zzahit = pRayhit.ray.org_z + pRayhit.ray.dir_z * pRayhit.ray.tfar;
      { // DEBUG
        RLOG_DEBUG << std::endl;
        RLOG_DEBUG << "hit location: " << xxahit << " " << yyahit << " " << zzahit << ")" << std::endl;
        RLOG_DEBUG << "normal from rayhit.hit: " << normalO << std::endl;
        auto geoNormal = pGeometry.get_normal(pRayhit.hit.primID);
        RLOG_DEBUG << "normal from geometry object: " << geoNormal << std::endl;
      } // DEBUG
      // Sanity check
      auto diff = rti::diff(normalO, pGeometry.get_normal(pRayhit.hit.primID));
      assert (diff.frst < 1e-5 && diff.scnd < 1e-5 && diff.thrd < 1e-5 && "Caluclation of surface normal");
      // Is that the normal at the hit location? Yes.

      // CAUTION: For now we use inverted geometries from GMSH, that is, we have
      // to invert the normal.
      auto normal = rti::inv(normalO);

      /* Compute lambertian reflection with respect to surface normal */
      auto orthonormalBasis = get_orthonormal_basis<float>(normal);

      // Set new origin and direction in pRayhit
      // We add a small epsilon to the origin to make sure that we do not intersect the same surface again.
      // Without that the simulation does not work.
      double epsilonOrg = 1e-6;
      auto obFirst = orthonormalBasis.frst; // normalized surface normal
      pRayhit.ray.org_x = xxahit + obFirst.frst * epsilonOrg;
      pRayhit.ray.org_y = yyahit + obFirst.scnd * epsilonOrg;
      pRayhit.ray.org_z = zzahit + obFirst.thrd * epsilonOrg;
      auto direction = rti::cos_hemi::get(orthonormalBasis, pRng, pRngState);
      pRayhit.ray.dir_x = direction.frst;
      pRayhit.ray.dir_y = direction.scnd;
      pRayhit.ray.dir_z = direction.thrd;

      return true;
    }

  private:
    // The sticking coefficient
    double mStickingC;

    // Returns some orthonormal basis containing a the input vector pVector
    // (possibly scaled) as the first element of the return value.
    // This function is deterministic, i.e., for one input it will return always
    // the same result.
    template<typename T>
    rti::triple<rti::triple<T> > get_orthonormal_basis(const rti::triple<T> pVector) const {
      rti::triple<rti::triple<T> > rr;
      rr.frst = pVector;
      // rr.frst.frst = pVector.frst;
      // rr.frst.scnd = pVector.scnd;
      // rr.frst.thrd = pVector.thrd;

      // Calculate a vector (rr.scnd) which is perpendicular to rr.frst
      // https://math.stackexchange.com/questions/137362/how-to-find-perpendicular-vector-to-another-vector#answer-211195
      rti::triple<T> candidate0 {rr.frst.thrd, rr.frst.thrd, -(rr.frst.frst + rr.frst.scnd)};
      rti::triple<T> candidate1 {rr.frst.scnd, -(rr.frst.frst + rr.frst.thrd), rr.frst.scnd};
      rti::triple<T> candidate2 {-(rr.frst.scnd + rr.frst.thrd), rr.frst.frst, rr.frst.frst};
      // We choose the candidate which maximizes the sum of its components, because we want to avoid
      // numeric errors and that the result is (0, 0, 0).
      std::array<rti::triple<T>, 3> cc = {candidate0, candidate1, candidate2};
      auto sumFun = [](rti::triple<T> oo){return oo.frst + oo.scnd + oo.thrd;};
      int maxIdx = 0;
      for (size_t idx = 1; idx < cc.size(); ++idx) {
        if (sumFun(cc[idx]) > sumFun(cc[maxIdx])) {
          maxIdx = idx;
        }
      }
      assert (maxIdx < 3 && "Error in computation of perpenticular vector");
      rr.scnd = cc[maxIdx];

      // Calculat cross product of rr.frst and rr.scnd to form orthogonal basis
      // rr.thrd.frst = rr.frst.scnd * rr.scnd.thrd - rr.frst.thrd * rr.scnd.scnd;
      // rr.thrd.scnd = rr.frst.thrd * rr.scnd.frst - rr.frst.frst * rr.scnd.thrd;
      // rr.thrd.thrd = rr.frst.frst * rr.scnd.scnd - rr.frst.scnd * rr.scnd.frst;
      rr.thrd = rti::cross_product(rr.frst, rr.scnd);

      // Normalize the length of these vectors.
      rti::normalize(rr.frst);
      rti::normalize(rr.scnd);
      rti::normalize(rr.thrd);

      // Sanity check
      assert(std::abs(rti::dot_product(rr.frst, rr.scnd)) < 1e-6 && "Error in orthonormal basis computation");
      assert(std::abs(rti::dot_product(rr.scnd, rr.thrd)) < 1e-6 && "Error in orthonormal basis computation");
      assert(std::abs(rti::dot_product(rr.scnd, rr.thrd)) < 1e-6 && "Error in orthonormal basis computation");

      return rr;
    }

  };
} // namespace rti