#ifndef AGORA_MKL_DFTI_WRAPPER_H_
#define AGORA_MKL_DFTI_WRAPPER_H_

#ifdef AGORA_HAVE_MKL
#include <mkl_dfti.h>
#else

#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <fftw3.h>

#ifndef MKL_LONG
using MKL_LONG = long;
#endif

// Minimal subset of MKL's DFTI status codes needed by Agora
static constexpr MKL_LONG DFTI_NO_ERROR = 0;
static constexpr MKL_LONG DFTI_MEMORY_ERROR = 1;
static constexpr MKL_LONG DFTI_BAD_DESCRIPTOR = 2;
static constexpr MKL_LONG DFTI_UNCOMMITTED = 3;
static constexpr MKL_LONG DFTI_BAD_VALUE = 4;

// Supported configuration tokens
static constexpr MKL_LONG DFTI_SINGLE = 0;
static constexpr MKL_LONG DFTI_COMPLEX = 1;
static constexpr MKL_LONG DFTI_PLACEMENT = 2;
static constexpr MKL_LONG DFTI_INPLACE = 3;
static constexpr MKL_LONG DFTI_NOT_INPLACE = 4;

struct AgoraDftiDescriptor {
  MKL_LONG length;
  bool not_in_place;
  bool committed;
  fftwf_complex* scratch_in;
  fftwf_complex* scratch_out;
  fftwf_plan forward_plan;
  fftwf_plan backward_plan;
};

using DFTI_DESCRIPTOR_HANDLE = AgoraDftiDescriptor*;

inline const char* DftiErrorMessage(MKL_LONG status) {
  switch (status) {
    case DFTI_NO_ERROR:
      return "No error";
    case DFTI_MEMORY_ERROR:
      return "Memory allocation failed";
    case DFTI_BAD_DESCRIPTOR:
      return "Invalid DFTI descriptor";
    case DFTI_UNCOMMITTED:
      return "DFTI descriptor not committed";
    case DFTI_BAD_VALUE:
      return "Unsupported DFTI parameter";
    default:
      return "Unknown DFTI error";
  }
}

inline MKL_LONG DftiCreateDescriptor(DFTI_DESCRIPTOR_HANDLE* desc,
                                     MKL_LONG precision, MKL_LONG domain,
                                     MKL_LONG dimension, MKL_LONG length) {
  if (desc == nullptr || dimension != 1 || precision != DFTI_SINGLE ||
      domain != DFTI_COMPLEX || length <= 0) {
    return DFTI_BAD_VALUE;
  }
  auto* handle = new AgoraDftiDescriptor();
  handle->length = length;
  handle->not_in_place = false;
  handle->committed = false;
  handle->scratch_in = nullptr;
  handle->scratch_out = nullptr;
  handle->forward_plan = nullptr;
  handle->backward_plan = nullptr;
  *desc = handle;
  return DFTI_NO_ERROR;
}

inline MKL_LONG DftiSetValue(DFTI_DESCRIPTOR_HANDLE desc, MKL_LONG parameter,
                             MKL_LONG value) {
  if (desc == nullptr) {
    return DFTI_BAD_DESCRIPTOR;
  }
  if (parameter == DFTI_PLACEMENT) {
    if (value == DFTI_NOT_INPLACE) {
      desc->not_in_place = true;
      return DFTI_NO_ERROR;
    }
    if (value == DFTI_INPLACE) {
      desc->not_in_place = false;
      return DFTI_NO_ERROR;
    }
  }
  return DFTI_BAD_VALUE;
}

inline MKL_LONG AgoraCommitDescriptor(DFTI_DESCRIPTOR_HANDLE desc,
                                      int direction, fftwf_plan& plan) {
  fftwf_complex* in = reinterpret_cast<fftwf_complex*>(
      fftwf_malloc(sizeof(fftwf_complex) * desc->length));
  if (in == nullptr) {
    return DFTI_MEMORY_ERROR;
  }
  fftwf_complex* out = in;
  if (desc->not_in_place) {
    out = reinterpret_cast<fftwf_complex*>(
        fftwf_malloc(sizeof(fftwf_complex) * desc->length));
    if (out == nullptr) {
      fftwf_free(in);
      return DFTI_MEMORY_ERROR;
    }
  }
  auto tmp_plan = fftwf_plan_dft_1d(static_cast<int>(desc->length), in, out,
                                    direction, FFTW_MEASURE);
  if (tmp_plan == nullptr) {
    fftwf_free(in);
    if (desc->not_in_place) {
      fftwf_free(out);
    }
    return DFTI_BAD_VALUE;
  }
  plan = tmp_plan;
  desc->scratch_in = in;
  desc->scratch_out = desc->not_in_place ? out : in;
  return DFTI_NO_ERROR;
}

inline MKL_LONG DftiCommitDescriptor(DFTI_DESCRIPTOR_HANDLE desc) {
  if (desc == nullptr) {
    return DFTI_BAD_DESCRIPTOR;
  }
  auto status = AgoraCommitDescriptor(desc, FFTW_FORWARD, desc->forward_plan);
  if (status != DFTI_NO_ERROR) {
    return status;
  }
  status = AgoraCommitDescriptor(desc, FFTW_BACKWARD, desc->backward_plan);
  if (status != DFTI_NO_ERROR) {
    return status;
  }
  desc->committed = true;
  return DFTI_NO_ERROR;
}

inline MKL_LONG DftiExecutePlan(DFTI_DESCRIPTOR_HANDLE desc,
                                fftwf_plan plan, void* in, void* out) {
  if (desc == nullptr || !desc->committed || plan == nullptr) {
    return DFTI_BAD_DESCRIPTOR;
  }
  auto* in_cf = reinterpret_cast<fftwf_complex*>(in);
  auto* out_cf = reinterpret_cast<fftwf_complex*>(out);
  if (in_cf == nullptr || out_cf == nullptr) {
    return DFTI_BAD_VALUE;
  }
  fftwf_execute_dft(plan, in_cf, out_cf);
  return DFTI_NO_ERROR;
}

inline MKL_LONG AgoraDftiComputeForwardImpl(DFTI_DESCRIPTOR_HANDLE desc,
                                            void* in, void* out) {
  return DftiExecutePlan(desc, desc->forward_plan, in, out);
}

inline MKL_LONG AgoraDftiComputeBackwardImpl(DFTI_DESCRIPTOR_HANDLE desc,
                                             void* in, void* out) {
  return DftiExecutePlan(desc, desc->backward_plan, in, out);
}

inline MKL_LONG DftiComputeForward(DFTI_DESCRIPTOR_HANDLE desc, void* in,
                                   void* out) {
  return AgoraDftiComputeForwardImpl(desc, in, out);
}

inline MKL_LONG DftiComputeForward(DFTI_DESCRIPTOR_HANDLE desc, void* in_out) {
  return AgoraDftiComputeForwardImpl(desc, in_out, in_out);
}

inline MKL_LONG DftiComputeBackward(DFTI_DESCRIPTOR_HANDLE desc, void* in,
                                    void* out) {
  return AgoraDftiComputeBackwardImpl(desc, in, out);
}

inline MKL_LONG DftiComputeBackward(DFTI_DESCRIPTOR_HANDLE desc, void* in_out) {
  return AgoraDftiComputeBackwardImpl(desc, in_out, in_out);
}

inline MKL_LONG DftiFreeDescriptor(DFTI_DESCRIPTOR_HANDLE* desc) {
  if (desc == nullptr || *desc == nullptr) {
    return DFTI_BAD_DESCRIPTOR;
  }
  auto* handle = *desc;
  if (handle->forward_plan != nullptr) {
    fftwf_destroy_plan(handle->forward_plan);
  }
  if (handle->backward_plan != nullptr) {
    fftwf_destroy_plan(handle->backward_plan);
  }
  if (handle->scratch_in != nullptr) {
    fftwf_free(handle->scratch_in);
  }
  if (handle->not_in_place && handle->scratch_out != nullptr &&
      handle->scratch_out != handle->scratch_in) {
    fftwf_free(handle->scratch_out);
  }
  delete handle;
  *desc = nullptr;
  return DFTI_NO_ERROR;
}

#endif  // AGORA_HAVE_MKL

#endif  // AGORA_MKL_DFTI_WRAPPER_H_
