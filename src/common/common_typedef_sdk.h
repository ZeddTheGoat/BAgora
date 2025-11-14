#ifndef AGORA_COMMON_TYPEDEF_SDK_H_
#define AGORA_COMMON_TYPEDEF_SDK_H_

#include <complex>
#include <cstdint>

struct alignas(8) complex_float {
  float re;
  float im;

  constexpr complex_float() : re(0.0f), im(0.0f) {}
  constexpr complex_float(float real, float imag) : re(real), im(imag) {}
  constexpr explicit complex_float(const std::complex<float>& value)
      : re(value.real()), im(value.imag()) {}

  constexpr std::complex<float> ToStdComplex() const {
    return std::complex<float>(re, im);
  }
};

inline constexpr complex_float MakeComplexFloat(float real, float imag) {
  return complex_float(real, imag);
}

inline constexpr complex_float MakeComplexFloat(
    const std::complex<float>& value) {
  return complex_float(value);
}

inline constexpr std::complex<float> ToStdComplex(
    const complex_float& value) {
  return value.ToStdComplex();
}

#endif  // AGORA_COMMON_TYPEDEF_SDK_H_
