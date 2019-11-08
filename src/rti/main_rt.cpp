#include <algorithm>
#include <iostream>
#include <omp.h>
#include <string>

#include <assert.h>

#include <embree3/rtcore.h>
#include <gmsh.h>
#include <immintrin.h> // AVX
#include <pmmintrin.h>
#include <xmmintrin.h>

#include "rti/geo/boundary_x_y.hpp"
#include "rti/geo/disc_geometry_from_gmsh.hpp"
#include "rti/geo/oriented_disc_geometry_from_gmsh.hpp"
#include "rti/geo/point_cloud_disc_geometry.hpp"
#include "rti/geo/point_cloud_sphere_geometry.hpp"
#include "rti/geo/sphere_geometry_from_gmsh.hpp"
#include "rti/geo/triangle_factory.hpp"
#include "rti/geo/triangle_geometry_from_gmsh.hpp"
#include "rti/geo/triangle_geometry.hpp"
#include "rti/io/vtp_point_cloud_reader.hpp"
#include "rti/io/christoph/vtu_point_cloud_reader.hpp"
#include "rti/io/christoph/vtu_triangle_reader.hpp"
#include "rti/io/vtp_writer.hpp"
#include "rti/ray/constant_origin.hpp"
#include "rti/ray/cosine_direction.hpp"
#include "rti/ray/disc_origin_x.hpp"
#include "rti/ray/disc_origin_z.hpp"
#include "rti/ray/dummy_direction.hpp"
#include "rti/ray/source.hpp"
#include "rti/ray/rectangle_origin_z.hpp"
#include "rti/test_and_benchmark/test_result.hpp"
#include "rti/trace/tracer.hpp"
#include "rti/trace/result.hpp"
#include "rti/util/clo.hpp"
#include "rti/util/logger.hpp"
#include "rti/util/ray_logger.hpp"

namespace rti {
  namespace main_rt {

    std::unique_ptr<rti::util::clo::manager> init(int argc, char* argv[]) {
      // Setup command line arguments parser
      auto optMan = std::make_unique<rti::util::clo::manager>();
      optMan->addCmlParam(rti::util::clo::string_option
        {"MAX_THREADS", {"--max-threads", "-m"}, "specifies the maximum number of threads used", false});
      optMan->addCmlParam(rti::util::clo::string_option
        {"INPUT_FILE", {"--infile", "-i"}, "specifies the path of the input file", true});
      // We might want the output file to be mandatory in the future
      optMan->addCmlParam(rti::util::clo::string_option
        {"OUTPUT_FILE", {"--outfile", "-o"}, "specifies the path of the output file", false});
      optMan->addCmlParam(rti::util::clo::string_option
        {"NUM_RAYS", {"--number-of-rays", "--n-rays", "-r"}, "specifies the number of rays to use", false});
      optMan->addCmlParam(rti::util::clo::bool_option
        {"TRIANGLES", {"--triangles"}, "sets triangles as surface primitives"});
      optMan->addCmlParam(rti::util::clo::bool_option
        {"DISCS", {"--discs"}, "sets discs as surface primitives"});
      bool succ = optMan->parse_args(argc, argv);
      if (!succ) {
        std::cout << optMan->get_usage_msg();
        exit(EXIT_FAILURE);
      }

      std::string maxThreadsStr = optMan->get_string_option_value("MAX_THREADS");
      if ( ! maxThreadsStr.empty() ) {
        int maxThreads = std::stoi(maxThreadsStr);
        if (maxThreads < omp_get_max_threads()) {
          omp_set_num_threads(maxThreads);
        }
      }
      std::cout << "Using " << omp_get_max_threads() << " threads" << std::endl;

      // Enable Flush-to-Zero and Denormals-are-Zero for the MXSCR status and
      // control registers for performance reasons.
      //
      // "If using a different tasking system, make sure each rendering thread has the proper mode set."
      // See: https://embree.github.io/api.html#mxcsr-control-and-status-register
      #pragma omp parallel
      {
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
      }

      return optMan;
    }

    void print_rtc_device_info(RTCDevice pDevice) {
      RLOG_INFO
        << "RTC_DEVICE_PROPERTY_TRIANGLE_GEOMETRY_SUPPORTED == "
        << rtcGetDeviceProperty(pDevice, RTC_DEVICE_PROPERTY_TRIANGLE_GEOMETRY_SUPPORTED) << std::endl;
      RLOG_INFO
        << "RTC_DEVICE_PROPERTY_POINT_GEOMETRY_SUPPORTED == "
        << rtcGetDeviceProperty(pDevice, RTC_DEVICE_PROPERTY_POINT_GEOMETRY_SUPPORTED) << std::endl;
      // RLOG_INFO
      //   << "RTC_DEVICE_PROPERTY_TASKING_SYSTEM == "
      //   << rtcGetDeviceProperty(pDevice,RTC_DEVICE_PROPERTY_TASKING_SYSTEM) << std::endl
      //   << "0 indicates internal tasking system" << std::endl
      //   << "1 indicates Intel Threading Building Blocks (TBB)" << std::endl
      //   << "2 indicates Parallel Patterns Library (PPL)" << std::endl;
      RLOG_INFO
        << "RTC_DEVICE_PROPERTY_BACKFACE_CULLING_ENABLED == "
        << rtcGetDeviceProperty(pDevice, RTC_DEVICE_PROPERTY_BACKFACE_CULLING_ENABLED) << std::endl;
      RLOG_INFO
        << "RTC_DEVICE_PROPERTY_RAY_STREAM_SUPPORTED == "
        << rtcGetDeviceProperty(pDevice, RTC_DEVICE_PROPERTY_RAY_STREAM_SUPPORTED) << std::endl;
    }
  }
}

int main(int argc, char* argv[]) {

  // We are using floats. There would actually not be any benefit of using
  // double because Embree can work only with floats internally.
  using numeric_type = float;

  auto optMan = rti::main_rt::init(argc, argv);
  //rti::io::gmsh_reader& gmshReader = rti::io::gmsh_reader::getInstance();
  auto infilename = optMan->get_string_option_value("INPUT_FILE");
  auto outfilename = optMan->get_string_option_value("OUTPUT_FILE");

  // Enable huge page support.
  auto device_config = "hugepages=1";
  auto device = rtcNewDevice(device_config);
  rti::main_rt::print_rtc_device_info(device);
  assert(rtcGetDeviceProperty(device, RTC_DEVICE_PROPERTY_BACKFACE_CULLING_ENABLED) != 0 &&
         "Error: backface culling is not enabled");

  // rti::ray::source<numeric_type> source(
  //   std::make_unique<rti::ray::disc_origin_x<numeric_type> >(0, 0, 0, 0.5),
  //   std::make_unique<rti::ray::cosine_direction<numeric_type> >(
  //     rti::util::triple<rti::util::triple<numeric_type> > {
  //       {1.f, 0.f, 0.f},
  //       {0.f, 1.f, 0.f},
  //       {0.f, 0.f, 1.f}}));

  //rti::geo::triangle_geometry_from_gmsh geometry(device, gmshReader);
  //rti::geo::oriented_disc_geometry_from_gmsh orntdDiscGeo(device, gmshReader);
  //rti::geo::disc_geometry_from_gmsh discGeo(device, gmshReader);
  //rti::geo::sphere_geometry_from_gmsh geometry(device, gmshReader);
  //rti::ray::disc_origin_x<numeric_type> origin(0, 0, 0, 0.5);


  //auto pntCldReader = rti::io::vtp_point_cloud_reader<numeric_type> {infilename};
  //auto pntCldReader = rti::io::christoph::vtu_point_cloud_reader<numeric_type> {infilename};
  //auto geometry = rti::geo::point_cloud_sphere_geometry<numeric_type> {device, pntCldReader, 0.01};
  //auto geometry = rti::geo::point_cloud_disc_geometry<numeric_type> {device, pntCldReader, 1e-2};
  auto triangleReader = rti::io::christoph::vtu_triangle_reader<numeric_type> {infilename};
  //auto geometry = rti::geo::triangle_geometry<numeric_type> {device, triangleReader, 0.8};

  std::unique_ptr<rti::geo::i_factory<numeric_type> > geoFactory;
  auto stickingC = 0.8f;
  geoFactory = std::make_unique<rti::geo::triangle_factory<numeric_type> > (device, triangleReader, stickingC);
  // if (optMan->get_bool_option_value("TRIANGLES")) {
  //   geoFactory = std::make_unique<rti::geo::triangle_factory<numeric_type> > (device, triangleReader, stickingC);
  // }


  // Compute bounding box
  auto bdBox = geoFactory->get_geometry().get_bounding_box();
  // Increase the size of the bounding box by an epsilon on the z achsis.
  auto epsilon = 0.1; //0.1; // -0.1;
  if (bdBox[0][2] > bdBox[1][2]) {
    bdBox[0][2] += epsilon;
  } else {
    bdBox[1][2] += epsilon;
  }
  std::cerr << "main bdBox: " << bdBox[0][0] << " " << bdBox[0][1] << " " << bdBox[0][2] << " " << bdBox[1][0] << " " << bdBox[1][1] << " " << bdBox[1][2] << " " << std::endl;
  // for (auto const& bb : bdBox)
  //   for (auto const& cc : bb)
  //     std::cout << cc << " ";
  // std::cout << std::endl;

  // Prepare boundary
  auto bdBoxAltered = bdBox;
  // bdBoxAltered[0][0] -= 0.1; // x
  // bdBoxAltered[0][1] -= 0.1; // y
  // bdBoxAltered[1][0] += 0.1; // x
  // bdBoxAltered[1][1] += 0.1; // y
  auto boundary = rti::geo::boundary_x_y<numeric_type> {device, bdBoxAltered};

  // Prepare source
  auto zmax = std::max(bdBox[0][2], bdBox[1][2]);
  auto originC1 = rti::util::pair<numeric_type> {bdBox[0][0], bdBox[0][1]};
  auto originC2 = rti::util::pair<numeric_type> {bdBox[1][0], bdBox[1][1]};

  // ASSUMPTION: the source is on a plain above (positive values) the structure
  // such that z == c for some constant c. (That is in accordance with the silvaco
  // verification instances.)
  //
  auto origin = rti::ray::rectangle_origin_z<numeric_type> {zmax, originC1, originC2};
  //
  // auto origin = rti::ray::disc_origin_z<numeric_type> {(originC1[0] + originC2[0])/2,
  //                                                      (originC1[1] + originC2[1])/2,
  //                                                      zmax,
  //                                                      (originC2[0] - originC1[0])/2};
  //
  // auto origin = rti::ray::disc_origin_z<numeric_type> {(originC1[0] + originC2[0])/2,
  //                                                      (originC1[1] + originC2[1])/2,
  //                                                      zmax,
  //                                                      0.5};

  auto defaultNumberRays = 1024 * 1024; // magic number

  // Cosine direction in the opposite direction of the z-axis
  auto direction = rti::ray::cosine_direction<numeric_type> {
    {rti::util::triple<numeric_type> {0.f, 0.f, -1.f},
     rti::util::triple<numeric_type> {0.f, 1.f,  0.f},
     rti::util::triple<numeric_type> {1.f, 0.f,  0.f}}};
  auto source = rti::ray::source<numeric_type> {origin, direction};

  auto numraysstr = optMan->get_string_option_value("NUM_RAYS");
  auto numrays = std::stoull(numraysstr);
  numrays = numrays < 0 ? 0 : numrays;

  auto tracer = rti::trace::tracer<numeric_type> {*geoFactory, boundary, source, numrays};
  auto result = tracer.run();
  std::cout << result; // << std::endl;
  //std::cout << *result.hitAccumulator << std::endl;

  if ( ! outfilename.empty()) {
    // Write output to file
    if (vtksys::SystemTools::GetFilenameLastExtension(outfilename) != ".vtp") {
      std::cout << "Appending .vtp to the given file name" << std::endl;
      outfilename.append(".vtp");
    }
    auto bbfilename = vtksys::SystemTools::
      GetFilenameWithoutExtension(outfilename).append(".bounding-box.vtp");

    std::cout << "Writing output to " << outfilename << std::endl;
    //rti::io::vtp_writer<numeric_type>::write(geoFactory->get_geometry(), *result.hitAccumulator, outfilename);
    geoFactory->write_to_file(*result.hitAccumulator, outfilename);
    std::cout << "Writing bounding box to " << bbfilename << std::endl;
    rti::io::vtp_writer<numeric_type>::write(boundary, bbfilename);

    auto raylog = RAYLOG_GET_PTR();
    if (raylog != nullptr) {
      auto raylogfilename = vtksys::SystemTools::
        GetFilenameWithoutExtension(outfilename).append(".ray-log.vtp");
      std::cout << "Writing ray log to " << raylogfilename << std::endl;
      rti::io::vtp_writer<numeric_type>::write(raylog, raylogfilename);
    }
    auto raysrclog = RAYSRCLOG_GET_PTR();
    if (raysrclog != nullptr) {
      auto raysrclogfilename = vtksys::SystemTools::
        GetFilenameWithoutExtension(outfilename).append(".ray-src-log.vtp");
      std::cout << "Writing ray src log to " << raysrclogfilename << std::endl;
      rti::io::vtp_writer<numeric_type>::write(raysrclog, raysrclogfilename);
    }
  }

  rtcReleaseDevice(device);

  return EXIT_SUCCESS;
}
