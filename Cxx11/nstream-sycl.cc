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
/// NAME:    nstream
///
/// PURPOSE: To compute memory bandwidth when adding a vector of a given
///          number of double precision values to the scalar multiple of
///          another vector of the same length, and storing the result in
///          a third vector.
///
/// USAGE:   The program takes as input the number
///          of iterations to loop over the triad vectors, the length of the
///          vectors, and the offset between vectors
///
///          <progname> <# iterations> <vector length> <offset>
///
///          The output consists of diagnostics to make sure the
///          algorithm worked, and of timing statistics.
///
/// NOTES:   Bandwidth is determined as the number of words read, plus the
///          number of words written, times the size of the words, divided
///          by the execution time. For a vector length of N, the total
///          number of words read and written is 4*N*sizeof(double).
///
///
/// HISTORY: This code is loosely based on the Stream benchmark by John
///          McCalpin, but does not follow all the Stream rules. Hence,
///          reported results should not be associated with Stream in
///          external publications
///
///          Converted to C++11 by Jeff Hammond, November 2017.
///
//////////////////////////////////////////////////////////////////////

#include "CL/sycl.hpp"
#include "prk_util.h"

#if 0
#include "prk_opencl.h"
#define USE_OPENCL 1
#endif

template <typename T> class nstream;

template <typename T>
void run(cl::sycl::queue & q, int iterations, size_t length)
{
  //////////////////////////////////////////////////////////////////////
  // Allocate space and perform the computation
  //////////////////////////////////////////////////////////////////////

  double nstream_time(0);

  const T scalar(3);

  std::vector<T> h_A(length,0);
  std::vector<T> h_B(length,2);
  std::vector<T> h_C(length,2);

  try {

#if PREBUILD_KERNEL
    cl::sycl::program kernel(q.get_context());
    kernel.build_with_kernel_type<nstream<T>>();
#endif

    cl::sycl::buffer<T,1> d_A { h_A.data(), cl::sycl::range<1>(h_A.size()) };
    cl::sycl::buffer<T,1> d_B { h_B.data(), cl::sycl::range<1>(h_B.size()) };
    cl::sycl::buffer<T,1> d_C { h_C.data(), cl::sycl::range<1>(h_C.size()) };

    for (int iter = 0; iter<=iterations; ++iter) {

      if (iter==1) nstream_time = prk::wtime();

      q.submit([&](cl::sycl::handler& h) {

        auto A = d_A.template get_access<cl::sycl::access::mode::read_write>(h);
        auto B = d_B.template get_access<cl::sycl::access::mode::read>(h);
        auto C = d_C.template get_access<cl::sycl::access::mode::read>(h);

        h.parallel_for<class nstream<T>>(
#if PREBUILD_KERNEL
                kernel.get_kernel<nstream<T>>(),
#endif
                cl::sycl::range<1>{length}, [=] (cl::sycl::item<1> i) {
            A[i] += B[i] + scalar * C[i];
        });
      });
      q.wait();
    }

    // Stop timer before buffer+accessor destructors fire,
    // since that will move data, and we do not time that
    // for other device-oriented programming models.
    nstream_time = prk::wtime() - nstream_time;
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

  T ar(0);
  T br(2);
  T cr(2);
  for (int i=0; i<=iterations; ++i) {
      ar += br + scalar * cr;
  }

  ar *= length;

  double asum(0);
  for (size_t i=0; i<length; ++i) {
      asum += std::fabs(h_A[i]);
  }

  const double epsilon(1.e-8);
  if (std::fabs(ar-asum)/asum > epsilon) {
      std::cout << "Failed Validation on output array\n"
                << "       Expected checksum: " << ar << "\n"
                << "       Observed checksum: " << asum << std::endl;
      std::cout << "ERROR: solution did not validate" << std::endl;
  } else {
      std::cout << "Solution validates" << std::endl;
      double avgtime = nstream_time/iterations;
      double nbytes = 4.0 * length * sizeof(T);
      std::cout << 8*sizeof(T) << "B "
                << "Rate (MB/s): " << 1.e-6*nbytes/avgtime
                << " Avg time (s): " << avgtime << std::endl;
  }
}

int main(int argc, char * argv[])
{
  std::cout << "Parallel Research Kernels version " << PRKVERSION << std::endl;
  std::cout << "C++11/SYCL STREAM triad: A = B + scalar * C" << std::endl;

  //////////////////////////////////////////////////////////////////////
  /// Read and test input parameters
  //////////////////////////////////////////////////////////////////////

  int iterations, offset;
  size_t length;
  try {
      if (argc < 3) {
        throw "Usage: <# iterations> <vector length>";
      }

      iterations  = std::atoi(argv[1]);
      if (iterations < 1) {
        throw "ERROR: iterations must be >= 1";
      }

      length = std::atol(argv[2]);
      if (length <= 0) {
        throw "ERROR: vector length must be positive";
      }

      offset = (argc>3) ? std::atoi(argv[3]) : 0;
      if (length <= 0) {
        throw "ERROR: offset must be nonnegative";
      }
  }
  catch (const char * e) {
    std::cout << e << std::endl;
    return 1;
  }

  std::cout << "Number of iterations = " << iterations << std::endl;
  std::cout << "Vector length        = " << length << std::endl;
  std::cout << "Offset               = " << offset << std::endl;

  //////////////////////////////////////////////////////////////////////
  /// Setup SYCL environment
  //////////////////////////////////////////////////////////////////////

#ifdef USE_OPENCL
  prk::opencl::listPlatforms();
#endif

  try {
#if SYCL_TRY_CPU_QUEUE
    if (length<100000) {
        cl::sycl::queue host(cl::sycl::host_selector{});
#ifndef TRISYCL
        auto device      = host.get_device();
        auto platform    = device.get_platform();
        std::cout << "SYCL Device:   " << device.get_info<cl::sycl::info::device::name>() << std::endl;
        std::cout << "SYCL Platform: " << platform.get_info<cl::sycl::info::platform::name>() << std::endl;
#endif
        run<float>(host, iterations, length);
        run<double>(host, iterations, length);
    } else {
        std::cout << "Skipping host device since it is too slow for large problems" << std::endl;
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
          run<float>(cpu, iterations, length);
          run<double>(cpu, iterations, length);
        }
    }
#endif
    // NVIDIA GPU requires ptx64 target and does not work very well
#if SYCL_TRY_GPU_QUEUE
    if (1) {
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
          run<float>(gpu, iterations, length);
          if (has_fp64) {
            run<double>(gpu, iterations, length);
          }
        } else {
          std::cout << "SYCL GPU device lacks SPIR-V support." << std::endl;
#ifdef __COMPUTECPP__
          std::cout << "You are using ComputeCpp so we will try it anyways..." << std::endl;
          run<float>(gpu, iterations, length);
          if (has_fp64) {
            run<double>(gpu, iterations, length);
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


