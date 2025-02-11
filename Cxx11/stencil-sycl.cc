
///
/// Copyright (c) 2017, Intel Corporation
///
/// Redistribution and use in source and binary forms, with or without
/// modification, are permitted provided that the following conditions
/// are met:
///
/// * Redistributions of source code must retain the above copyright
///       notice, this list of conditions and the following disclaimer.
/// * Redistributions in binary form must reproduce the above
///       copyright notice, this list of conditions and the following
///       disclaimer in the documentation and/or other materials provided
///       with the distribution.
/// * Neither the name of Intel Corporation nor the names of its
///       contributors may be used to endorse or promote products
///       derived from this software without specific prior written
///       permission.
///
/// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
/// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
/// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
/// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
/// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
/// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
/// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
/// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
/// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
/// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
/// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
/// POSSIBILITY OF SUCH DAMAGE.

//////////////////////////////////////////////////////////////////////
///
/// NAME:    Stencil
///
/// PURPOSE: This program tests the efficiency with which a space-invariant,
///          linear, symmetric filter (stencil) can be applied to a square
///          grid or image.
///
/// USAGE:   The program takes as input the linear
///          dimension of the grid, and the number of iterations on the grid
///
///                <progname> <iterations> <grid size>
///
///          The output consists of diagnostics to make sure the
///          algorithm worked, and of timing statistics.
///
/// FUNCTIONS CALLED:
///
///          Other than standard C functions, the following functions are used in
///          this program:
///          wtime()
///
/// HISTORY: - Written by Rob Van der Wijngaart, February 2009.
///          - RvdW: Removed unrolling pragmas for clarity;
///            added constant to array "in" at end of each iteration to force
///            refreshing of neighbor data in parallel versions; August 2013
///            C++11-ification by Jeff Hammond, May 2017.
///
//////////////////////////////////////////////////////////////////////

#include "CL/sycl.hpp"
#include "prk_util.h"
#include "stencil_sycl.hpp"


#if 0
#include "prk_opencl.h"
#define USE_OPENCL 1
#endif

template <typename T> class init;
template <typename T> class add;

#if USE_2D_INDEXING
template <typename T>
void nothing(cl::sycl::queue & q, const size_t n, cl::sycl::buffer<T, 2> & d_in, cl::sycl::buffer<T, 2> & d_out)
#else
template <typename T>
void nothing(cl::sycl::queue & q, const size_t n, cl::sycl::buffer<T> & d_in, cl::sycl::buffer<T> & d_out)
#endif
{
    std::cout << "You are trying to use a stencil that does not exist.\n";
    std::cout << "Please generate the new stencil using the code generator\n";
    std::cout << "and add it to the case-switch in the driver." << std::endl;
    // There seems to be an issue with the clang CUDA/HIP toolchains not having
    // std::abort() available
#if defined(HIPSYCL_PLATFORM_CUDA) || defined(HIPSYCL_PLATFORM_HCC)
    abort();
#else
    std::abort();
#endif
}

template <typename T>
void run(cl::sycl::queue & q, int iterations, size_t n, size_t tile_size, bool star, size_t radius)
{
  auto stencil = nothing<T>;
  if (star) {
      switch (radius) {
          case 1: stencil = star1; break;
          case 2: stencil = star2; break;
          case 3: stencil = star3; break;
          case 4: stencil = star4; break;
          case 5: stencil = star5; break;
      }
  }
#if 0
  else {
      switch (radius) {
          case 1: stencil = grid1; break;
          case 2: stencil = grid2; break;
          case 3: stencil = grid3; break;
          case 4: stencil = grid4; break;
          case 5: stencil = grid5; break;
      }
  }
#endif

  //////////////////////////////////////////////////////////////////////
  // Allocate space and perform the computation
  //////////////////////////////////////////////////////////////////////

  double stencil_time(0);

  std::vector<T> h_in(n*n,0);
  std::vector<T> h_out(n*n,0);

  try {

    // initialize device buffers from host buffers
#if USE_2D_INDEXING
    cl::sycl::buffer<T, 2> d_in  { cl::sycl::range<2> {n, n} };
    cl::sycl::buffer<T, 2> d_out { h_out.data(), cl::sycl::range<2> {n, n} };
#else
    // FIXME: if I don't initialize this buffer from host, the results are wrong.  Why?
    //cl::sycl::buffer<T> d_in  { cl::sycl::range<1> {n*n} };
    cl::sycl::buffer<T> d_in  { h_in.data(),  h_in.size() };
    cl::sycl::buffer<T> d_out { h_out.data(), h_out.size() };
#endif

    q.submit([&](cl::sycl::handler& h) {

      // accessor methods
      auto in  = d_in.template get_access<cl::sycl::access::mode::read_write>(h);

      h.parallel_for<class init<T>>(cl::sycl::range<2> {n, n}, [=] (cl::sycl::item<2> it) {
#if USE_2D_INDEXING
          cl::sycl::id<2> xy = it.get_id();
          auto i = it[0];
          auto j = it[1];
          in[xy] = static_cast<T>(i+j);
#else
          auto i = it[0];
          auto j = it[1];
          in[i*n+j] = static_cast<T>(i+j);
#endif
      });
    });
    q.wait();

    for (auto iter = 0; iter<=iterations; iter++) {

      if (iter==1) stencil_time = prk::wtime();

      stencil(q, n, d_in, d_out);
#ifdef TRISYCL
      q.wait();
#endif

      q.submit([&](cl::sycl::handler& h) {

        // accessor methods
        auto in  = d_in.template get_access<cl::sycl::access::mode::read_write>(h);

        // Add constant to solution to force refresh of neighbor data, if any
        h.parallel_for<class add<T>>(cl::sycl::range<2> {n, n}, cl::sycl::id<2> {0, 0},
                                  [=] (cl::sycl::item<2> it) {
#if USE_2D_INDEXING
            cl::sycl::id<2> xy = it.get_id();
            in[xy] += static_cast<T>(1);
#else
#if 0 // This is noticeably slower :-(
            auto i = it[0];
            auto j = it[1];
            in[i*n+j] += 1.0;
#else
            in[it[0]*n+it[1]] += static_cast<T>(1);
#endif
#endif
        });
      });
      q.wait();
    }
    stencil_time = prk::wtime() - stencil_time;
  }
  catch (cl::sycl::exception & e) {
    std::cout << e.what() << std::endl;
#ifdef __COMPUTECPP__
    std::cout << e.get_file_name() << std::endl;
    std::cout << e.get_line_number() << std::endl;
    std::cout << e.get_description() << std::endl;
    std::cout << e.get_cl_error_message() << std::endl;
    std::cout << e.get_cl_code() << std::endl;
#endif
    return;
  }
  catch (std::exception & e) {
    std::cout << e.what() << std::endl;
    return;
  }
  catch (const char * e) {
    std::cout << e << std::endl;
    return;
  }

  //////////////////////////////////////////////////////////////////////
  /// Analyze and output results
  //////////////////////////////////////////////////////////////////////

  // interior of grid with respect to stencil
  auto active_points = (n-2L*radius)*(n-2L*radius);

  // compute L1 norm in parallel
  double norm(0);
  for (int i=radius; i<n-radius; i++) {
    for (int j=radius; j<n-radius; j++) {
      norm += std::fabs(h_out[i*n+j]);
    }
  }
  norm /= active_points;

  // verify correctness
  const double epsilon = 1.0e-8;
  const double reference_norm = 2*(iterations+1);
  if (std::fabs(norm-reference_norm) > epsilon) {
    std::cout << "ERROR: L1 norm = " << norm
              << " Reference L1 norm = " << reference_norm << std::endl;
  } else {
    std::cout << "Solution validates" << std::endl;
#ifdef VERBOSE
    std::cout << "L1 norm = " << norm
              << " Reference L1 norm = " << reference_norm << std::endl;
#endif
    const size_t stencil_size = star ? 4*radius+1 : (2*radius+1)*(2*radius+1);
    size_t flops = (2L*stencil_size+1L) * active_points;
    double avgtime = stencil_time/iterations;
    std::cout << 8*sizeof(T) << "B "
              << "Rate (MFlops/s): " << 1.0e-6 * static_cast<double>(flops)/avgtime
              << " Avg time (s): " << avgtime << std::endl;
  }
}

int main(int argc, char * argv[])
{
  std::cout << "Parallel Research Kernels version " << PRKVERSION << std::endl;
  std::cout << "C++11/SYCL Stencil execution on 2D grid" << std::endl;

  //////////////////////////////////////////////////////////////////////
  // Process and test input parameters
  //////////////////////////////////////////////////////////////////////

  int iterations;
  size_t n, tile_size;
  bool star = true;
  size_t radius = 2;
  try {
      if (argc < 3) {
        throw "Usage: <# iterations> <array dimension> [<tile size> <star/grid> <stencil radius>]";
      }

      // number of times to run the algorithm
      iterations  = std::atoi(argv[1]);
      if (iterations < 1) {
        throw "ERROR: iterations must be >= 1";
      }

      // linear grid dimension
      n  = std::atoi(argv[2]);
      if (n < 1) {
        throw "ERROR: grid dimension must be positive";
      } else if (n > std::floor(std::sqrt(INT_MAX))) {
        throw "ERROR: grid dimension too large - overflow risk";
      }

      // default tile size for tiling of local transpose
      tile_size = 32;
      if (argc > 3) {
          tile_size = std::atoi(argv[3]);
          if (tile_size <= 0) tile_size = n;
          if (tile_size > n) tile_size = n;
      }

      // stencil pattern
      if (argc > 4) {
          auto stencil = std::string(argv[4]);
          auto grid = std::string("grid");
          star = (stencil == grid) ? false : true;
      }

      // stencil radius
      radius = 2;
      if (argc > 5) {
          radius = std::atoi(argv[5]);
      }

      if ( (radius < 1) || (2*radius+1 > n) ) {
        throw "ERROR: Stencil radius negative or too large";
      }
  }
  catch (const char * e) {
    std::cout << e << std::endl;
    return 1;
  }

  std::cout << "Number of iterations = " << iterations << std::endl;
  std::cout << "Grid size            = " << n << std::endl;
  std::cout << "Type of stencil      = " << (star ? "star" : "grid") << std::endl;
  std::cout << "Radius of stencil    = " << radius << std::endl;

  //////////////////////////////////////////////////////////////////////
  /// Setup SYCL environment
  //////////////////////////////////////////////////////////////////////

#ifdef USE_OPENCL
  prk::opencl::listPlatforms();
#endif

  try {

#if SYCL_TRY_CPU_QUEUE
    if (1) {
        cl::sycl::queue host(cl::sycl::host_selector{});
#if !defined(TRISYCL) && !defined(__HIPSYCL__)
        auto device      = host.get_device();
        auto platform    = device.get_platform();
        std::cout << "SYCL Device:   " << device.get_info<cl::sycl::info::device::name>() << std::endl;
        std::cout << "SYCL Platform: " << platform.get_info<cl::sycl::info::platform::name>() << std::endl;
#endif

        run<float>(host, iterations, n, tile_size, star, radius);
        run<double>(host, iterations, n, tile_size, star, radius);
    }
#endif

    // CPU requires spir64 target
#if SYCL_TRY_CPU_QUEUE
    if (1) {
        cl::sycl::queue cpu(cl::sycl::cpu_selector{});
#if !defined(TRISYCL) && !defined(__HIPSYCL__)
        auto device      = cpu.get_device();
        auto platform    = device.get_platform();
        std::cout << "SYCL Device:   " << device.get_info<cl::sycl::info::device::name>() << std::endl;
        std::cout << "SYCL Platform: " << platform.get_info<cl::sycl::info::platform::name>() << std::endl;
        bool has_spir = device.has_extension(cl::sycl::string_class("cl_khr_spir"));
#else
        bool has_spir = true; // ?
#endif
        if (has_spir) {
          run<float>(cpu, iterations, n, tile_size, star, radius);
          run<double>(cpu, iterations, n, tile_size, star, radius);
        }
    }
#endif

    // NVIDIA GPU requires ptx64 target and does not work very well
#if SYCL_TRY_GPU_QUEUE
    if (0) {
        cl::sycl::queue gpu(cl::sycl::gpu_selector{});
#if !defined(TRISYCL) && !defined(__HIPSYCL__)
        auto device      = gpu.get_device();
        auto platform    = device.get_platform();
        std::cout << "SYCL Device:   " << device.get_info<cl::sycl::info::device::name>() << std::endl;
        std::cout << "SYCL Platform: " << platform.get_info<cl::sycl::info::platform::name>() << std::endl;
        bool has_spir = device.has_extension(cl::sycl::string_class("cl_khr_spir"));
        bool has_fp64 = device.has_extension(cl::sycl::string_class("cl_khr_fp64"));
#else
        bool has_spir = true; // ?
        bool has_fp64 = true;
#endif
        if (!has_fp64) {
          std::cout << "SYCL GPU device lacks FP64 support." << std::endl;
        }
        if (has_spir) {
          run<float>(gpu, iterations, n, tile_size, star, radius);
          if (has_fp64) {
            run<double>(gpu, iterations, n, tile_size, star, radius);
          }
        } else {
          std::cout << "SYCL GPU device lacks SPIR-V support." << std::endl;
#ifdef __COMPUTECPP__
          std::cout << "You are using ComputeCpp so we will try it anyways..." << std::endl;
          run<float>(gpu, iterations, n, tile_size, star, radius);
          if (has_fp64) {
            run<double>(gpu, iterations, n, tile_size, star, radius);
          }
#endif
        }
    }
#endif
  }
  catch (cl::sycl::exception & e) {
    std::cout << e.what() << std::endl;
#ifdef __COMPUTECPP__
    std::cout << e.get_file_name() << std::endl;
    std::cout << e.get_line_number() << std::endl;
    std::cout << e.get_description() << std::endl;
    std::cout << e.get_cl_error_message() << std::endl;
    std::cout << e.get_cl_code() << std::endl;
#endif
    return 1;
  }
  catch (std::exception & e) {
    std::cout << e.what() << std::endl;
    return 1;
  }
  catch (const char * e) {
    std::cout << e << std::endl;
    return 1;
  }

  return 0;
}


