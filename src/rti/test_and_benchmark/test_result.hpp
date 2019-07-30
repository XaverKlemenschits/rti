#pragma once

#include <chrono>

#include "rti/util/ostream_overload_template.hpp"

namespace rti { namespace test_and_benchmark {
  class test_result {
  public:
    uint64_t timeNanoseconds = 0;
    std::string geometryClassName;
    std::string inputFilePath;
    size_t numRays;
    size_t hitc;
    size_t nonhitc;

    void print(std::ostream& pOs) const {
      pOs
        << "(:class test_result " << inputFilePath << " "
        << geometryClassName << " "
        << hitc << "hits "
        << nonhitc << "non-hits "
        << numRays << "rays "
        << timeNanoseconds << "ns"
        << ")";
    }
  };
}} // namespace