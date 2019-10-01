#pragma once

#include <vector>
#include <set>

#include <embree3/rtcore.h>

#include "rti/reflection/i_reflection_model.hpp"
#include "rti/reflection/specular.hpp"
#include "rti/trace/dummy_counter.hpp"
#include "rti/trace/i_hit_accumulator.hpp"

// This class needs to be used according to the following protocol. If one does not
// follow the protocol, then the behaviour is undefined:
// See rti::trace::point_cloud_context

namespace rti { namespace trace {
  template<typename Ty> // intended to be a numeric type
  class triangle_context {
  public:
    // a wrapper aroung struct RTCIntersectContext which attaches additional,
    // rti-project specific data to the context.
    // See https://www.embree.org/api.html#rtcinitintersectcontext
    //
    // Data layout:
    // The first member HAS TO BE the RTC context, that is, context from the Embree library.
    RTCIntersectContext mRtcContext; // not a reference or pointer but full data stored here
    //
    static constexpr float INITIAL_RAY_WEIGHT = 1.0f;
    // =================================================================
    // CHOOSING A GOOD VALUE FOR THE WEIGHT LOWER THRESHOLD IS IMPORTANT
    // =================================================================
    static constexpr float RAY_WEIGHT_LOWER_THRESHOLD = 0.1f;
    static constexpr float RAY_RENEW_WEIGHT = 3 * RAY_WEIGHT_LOWER_THRESHOLD;
    //
    // additional data
    // Here we violate the naming convention (to name a member variable with a string
    // starting with the character 'm') on purpose. Rationale: The context provides
    // (contextual) local data to the outside.
    // TODO: change the naming?
    //
    // geometry related data
  private:
    bool geoNotIntersected = true;
    Ty geoFirstHitTFar = 0; // the type will probably be float since Embree uses float for its RTCRay.tfar
    Ty geoTFarMax = 0;
    rti::util::pair<rti::util::triple<Ty> > geoRayout;
    // boundary related data
    bool boundNotIntersected = true;
    Ty boundFirstHitTFar = 0;
    rti::util::pair<rti::util::triple<Ty> > boundRayout;
    // other data
  public:
    float rayWeight = INITIAL_RAY_WEIGHT;
    bool reflect = false;
    rti::util::pair<rti::util::triple<Ty> >& rayout = geoRayout; // initialize to some value
    Ty tfar = 0; // initialize to some value
    //
    // Class members which will be used in a conventional way.
  private:
    unsigned int mGeometryID;
    rti::geo::triangle_geometry<Ty>& mGeometry;
    rti::reflection::i_reflection_model<Ty>& mReflectionModel;
    rti::trace::i_hit_accumulator<Ty>& mHitAccumulator;
    unsigned int mBoundaryID;
    rti::geo::i_boundary<Ty>& mBoundary;
    rti::reflection::i_reflection_model<Ty>& mBoundaryReflectionModel;

    rti::rng::i_rng& mRng;
    rti::rng::i_rng::i_state& mRngState;

    // struct hit_t {
    //   unsigned int geomID;
    //   unsigned int primID;
    // };
    // A vector of primitive IDs collected through the filter function filter_fun_geometry() which we
    // will then post process in the post_process_intersection() function.
    // Initialize to some reasonable size. The vector may grow, if needed.
    // Weird initialization necessary
    std::vector<unsigned int> mGeoHitPrimIDs;


    // Constructor
  public:
    triangle_context(unsigned int pGeometryID,
            rti::geo::i_geometry<Ty>& pGeometry,
            rti::reflection::i_reflection_model<Ty>& pReflectionModel,
            rti::trace::i_hit_accumulator<Ty>& pHitAccumulator,
            unsigned int pBoundaryID,
            rti::geo::i_boundary<Ty>& pBoundary,
            rti::reflection::i_reflection_model<Ty>& pBoundaryReflectionModel,
            rti::rng::i_rng& pRng,
            rti::rng::i_rng::i_state& pRngState) :
      mGeometryID(pGeometryID),
      // TODO: FIX the cast
      mGeometry(*dynamic_cast<rti::geo::triangle_geometry<Ty>*>(&pGeometry)),
      mReflectionModel(pReflectionModel),
      mHitAccumulator(pHitAccumulator),
      mBoundaryID(pBoundaryID),
      mBoundary(pBoundary),
      mBoundaryReflectionModel(pBoundaryReflectionModel),
      mRng(pRng),
      mRngState(pRngState) {
      //
      std::cerr << "rti::trace::triangle_context::triangle_context()" << std::endl;
      std::cerr << "Warning: This class uses a dummy epsilon value" << std::endl;
      //
      mGeoHitPrimIDs.reserve(32); // magic number // Reserve some reasonable number of hit elements for one ray
      mGeoHitPrimIDs.clear();
    }

  public:
    static
    void register_intersect_filter_funs(rti::geo::i_geometry<Ty>& pGeometry,
                                        rti::geo::i_boundary<Ty>& pBoundary) {
      // The following cast characterizes a precondition to this function
      auto pPCGeoPointer = dynamic_cast<rti::geo::triangle_geometry<Ty>*> (&pGeometry);
      auto& pPCGeo = *pPCGeoPointer;
      RLOG_DEBUG << "register_intersect_filter_funs()" << std::endl;
      rtcSetGeometryIntersectFilterFunction(pPCGeo.get_rtc_geometry(), &filter_fun_geometry);
      rtcSetGeometryIntersectFilterFunction(pBoundary.get_rtc_geometry(), &filter_fun_boundary);
      rtcCommitGeometry(pPCGeo.get_rtc_geometry()); // TODO: needed?
      rtcCommitGeometry(pBoundary.get_rtc_geometry());
    }

  private:
    // Callback filters
    static // static cause C++ allows to pass a function (take the adress of a function)
    // only on static functions. The context object will be passed through the
    // argument.
    void filter_fun_geometry(RTCFilterFunctionNArguments const* args) {
      RLOG_DEBUG << "filter_fun_geometry()" << std::endl;
      assert(args->N == 1 && "Precondition");
      // This function gets a pointer to a context object in args.context
      auto cc = args->context;
      // The following cast also characterizes a precondition to this function
      auto  rticonstcontextptr = reinterpret_cast<rti::trace::triangle_context<Ty> const*> (cc);
      auto  rticontextptr = const_cast<rti::trace::triangle_context<Ty>*> (rticonstcontextptr);
      // The rticontextptr now serves an equal function as the this pointer in a conventional
      // (non-static) member function.
      assert(args->N == 1 && "Precondition: for the cast");
      auto  rayptr = reinterpret_cast<RTCRay*> (args->ray); // ATTENTION: this cast works only if N == 1 holds
      auto  hitptr = reinterpret_cast<RTCHit*> (args->hit); // ATTENTION: this cast works only if N == 1 holds
      auto  validptr = args->valid;
      // Reference all the data in the context with local variables
      // auto& reflect =    rticontextptr->reflect;
      auto& geometryID = rticontextptr->mGeometryID;
      auto& rng =        rticontextptr->mRng;
      auto& rngState =   rticontextptr->mRngState;
      auto& geometry =   rticontextptr->mGeometry;
      auto& geoRayout =  rticontextptr->geoRayout;

      assert(hitptr->geomID == geometryID && "Assumption");

      // Debug
      if(rticontextptr->geoNotIntersected) {
        RLOG_DEBUG << "filter_fun_geometry(): FIRST-HIT IS SET ";
      } else {
        RLOG_DEBUG << "filter_fun_geometry(): FIRST-HIT IS *NOT* SET ";
      }
      RLOG_DEBUG << "hitptr->geomID == " << hitptr->geomID << " primID == " << hitptr->primID << std::endl;
      // Non-Debug
      geoRayout = rticontextptr->mReflectionModel.use(*rayptr, *hitptr, geometry, rng, rngState);
      rticontextptr->geoFirstHitTFar = rayptr->tfar;
      rticontextptr->geoNotIntersected = false;
      // In a triangle context, always accept hits.
      return;
    }

    static
    void filter_fun_boundary(RTCFilterFunctionNArguments const* args) {
      RLOG_DEBUG << "filter_fun_boundary()" << std::endl;
      assert(args->N == 1 && "Precondition");
      // This function gets a pointer to a context object in args.context
      auto cc = args->context;
      // The following cast also characterizes a precondition to this function
      auto  rticonstcontextptr = reinterpret_cast<rti::trace::triangle_context<Ty> const*> (cc);
      auto  rticontextptr = const_cast<rti::trace::triangle_context<Ty>*> (rticonstcontextptr);
      // The rticontextptr now serves an equal function as the this pointer in a conventional
      // (non-static) member function.
      assert(args->N == 1 && "Precondition: for the cast");
      auto  rayptr = reinterpret_cast<RTCRay*> (args->ray); // ATTENTION: this cast works only if N == 1 holds
      auto  hitptr = reinterpret_cast<RTCHit*> (args->hit); // ATTENTION: this cast works only if N == 1 holds
      auto  validptr = args->valid;
      // Reference all the data in the context with local variables
      auto& boundaryID = rticontextptr->mBoundaryID;
      auto& rng =        rticontextptr->mRng;
      auto& rngState =   rticontextptr->mRngState;
      auto& boundary =   rticontextptr->mBoundary;
      auto& boundRayout = rticontextptr->boundRayout;

      assert(hitptr->geomID == boundaryID && "Assumption");
      // Debug
      if(rticontextptr->boundNotIntersected) {
        RLOG_DEBUG << "filter_fun_boundary(): FIRST-HIT IS SET ";
      } else {
        RLOG_DEBUG << "filter_fun_boundary(): FIRST-HIT IS *NOT* SET ";
      }
      RLOG_DEBUG << "hitptr->geomID == " << hitptr->geomID << " primID == " << hitptr->primID << std::endl;
      // Non-Debug
      boundRayout = rticontextptr->mBoundaryReflectionModel.use(*rayptr, *hitptr, boundary, rng, rngState);
      rticontextptr->boundFirstHitTFar = rayptr->tfar;
      rticontextptr->boundNotIntersected = false;
      return;
    }

    private:
    void post_process_intersect(RTCRayHit& pRayHit) {
      RLOG_DEBUG
        << "post_process_intersect(); mGeoHitPrimIDs.size() == " << this->mGeoHitPrimIDs.size() << "; ";
      for(auto const& ee : this->mGeoHitPrimIDs)
        RLOG_DEBUG << ee << " ";
      RLOG_DEBUG
        << std::endl;
      RLOG_DEBUG << "this->rayWeight == " << this->rayWeight << std::endl;

      if (this->geoNotIntersected && this->boundNotIntersected) {
        // The ray did not hit anything
        this->reflect = false;
        this->tfar = pRayHit.ray.tfar;
        return;
      }

      this->reflect = true;

      if ( ( ! this->boundNotIntersected && this->geoNotIntersected) ||
           ( ! this->boundNotIntersected && this->boundFirstHitTFar < this->geoFirstHitTFar)) {
        // Geometry not hit but boundary hit
        this->rayout = this->boundRayout;
        this->tfar = this->boundFirstHitTFar;
        return;
      }
      // A hit on the boundary
      assert ( ! this->geoNotIntersected && "Assumption");
      this->rayout = this->geoRayout;
      this->tfar = this->geoFirstHitTFar;
      auto& hitprimId = pRayHit.hit.primID;
      auto valuetodrop = this->rayWeight * this->mGeometry.get_sticking_coefficient();
      this->mHitAccumulator.use(hitprimId, valuetodrop);
      this->rayWeight -= valuetodrop;
      return;

      // We do what is sometimes called Roulette in MC literatur.
      // Jun Liu calls it "rejection controll" in his book.
      // If the weight of the ray is above a certain threshold, we always reflect.
      // If the weight of the ray is below the threshold, we randomly decide to either kill the
      // ray or increase its weight (in an unbiased way).
      if (this->rayWeight < RAY_WEIGHT_LOWER_THRESHOLD) {
        RLOG_DEBUG << "in post_process_intersect() (rayWeight < RAY_WEIGHT_LOWER_THRESHOLD) holds" << std::endl;
        // We want to set the weight of (the reflection of) the ray to RAY_NEW_WEIGHT.
        // In order to stay  unbiased we kill the reflection with a probability of
        // (1 - rticontextptr->rayWeight / RAY_RENEW_WEIGHT).
        auto rndm = this->mRng.get(this->mRngState);
        assert(this->rayWeight < RAY_RENEW_WEIGHT && "Assumption");
        auto killProbability = 1.0f - this->rayWeight / RAY_RENEW_WEIGHT;
        if (rndm < (killProbability * this->mRng.max())) {
          // kill the ray
          this->reflect = false;
        } else {
          // The ray survived the roulette: set the ray weight to the new weight
          //reflect = true;
          this->rayWeight = RAY_RENEW_WEIGHT;
        }
      }
      // If the ray has enough weight, then we reflect it in any case.
    }
  public:
    void intersect1(RTCScene& pScene, RTCRayHit& pRayHit) {
      // prepare
      pRayHit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
      pRayHit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
      this->geoNotIntersected = true;
      this->boundNotIntersected = true;
      // Embree intersect
      // This call uses our intersection functions which must have been registered with
      // register_intersect_filter_funs() .
      auto rtcContextPtr = reinterpret_cast<RTCIntersectContext*> (this);
      rtcIntersect1(pScene, rtcContextPtr, &pRayHit);
      // post process
      this->post_process_intersect(pRayHit);
    }

    void init() {
      // // RTC_INTERSECT_CONTEXT_FLAG_INCOHERENT flag uses an optimized traversal
      // // algorithm for incoherent rays (default)
      rtcInitIntersectContext(&this->mRtcContext);
    }
  };
}} // namespace
