#pragma once

#include <memory>

#include "rti/geo/i_boundary.hpp"
#include "rti/geo/i_factory.hpp"
#include "rti/geo/triangle_geometry.hpp"
#include "rti/io/i_triangle_reader.hpp"
#include "rti/io/vtp_writer.hpp"
#include "rti/trace/triangle_context.hpp"
#include "rti/trace/triangle_context_simplified.hpp"

namespace rti { namespace geo {
  template<typename Ty, typename ContextType>
  class triangle_factory : public rti::geo::i_factory<Ty> {
  public:
    triangle_factory(RTCDevice& pDevice,
                     rti::io::i_triangle_reader<Ty>& pReader,
                     Ty pStickingC) :
      mGeometry(pDevice, pReader, pStickingC) {}

    rti::geo::i_geometry<Ty>& get_geometry() override final {
      return mGeometry;
    }

    void register_intersect_filter_funs(rti::geo::i_boundary<Ty>& pBoundary) {
      ContextType::register_intersect_filter_funs(mGeometry, pBoundary);
    }

    std::unique_ptr<rti::trace::absc_context<Ty> > get_new_context(
      unsigned int pGeometryID,
      rti::geo::i_geometry<Ty>& pGeometry,
      rti::reflection::i_reflection_model<Ty>& pReflectionModel,
      rti::trace::i_hit_accumulator<Ty>& pHitAccumulator,
      unsigned int pBoundaryID,
      rti::geo::i_boundary<Ty>& pBoundary,
      rti::reflection::i_reflection_model<Ty>& pBoundaryReflectionModel,
      rti::rng::i_rng& pRng,
      rti::rng::i_rng::i_state& pRngState) override final {
      auto cntxt = std::make_unique<ContextType>
        (pGeometryID,
         // the cast characterizes a precondition to this function
         *dynamic_cast<rti::geo::triangle_geometry<Ty>*>(&pGeometry),
         pReflectionModel,
         pHitAccumulator, pBoundaryID, pBoundary,
         pBoundaryReflectionModel, pRng, pRngState);
      // We have to move the unique_ptr out
      return cntxt;
    }

    void write_to_file(rti::trace::i_hit_accumulator<Ty>& pHA,
                       std::string pOutfilename,
                       std::vector<rti::util::pair<std::string> > pMetadata) override final {
      rti::io::vtp_writer<Ty>::write(this->mGeometry, pHA, pOutfilename, pMetadata);
    }

  private:
    rti::geo::triangle_geometry<Ty> mGeometry;
  };
}} // namespace