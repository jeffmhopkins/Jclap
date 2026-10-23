#pragma once

// Turns off denormal (subnormal) floats for a scope, the way audio code usually runs: decaying
// filters and envelopes otherwise sink into numbers so small the CPU handles them slowly, and hosts
// (and clap-validator) flag them in the output. Put one at the top of process():
//
//    jclap::dsp::ScopedNoDenormals noDenormals;
//
// On x86 it sets flush-to-zero and denormals-are-zero; on 64-bit ARM, flush-to-zero. Elsewhere it
// does nothing.

#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#define JCLAP_DSP_DENORMALS_SSE 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define JCLAP_DSP_DENORMALS_ARM64 1
#endif

namespace jclap::dsp {

class ScopedNoDenormals {
public:
   ScopedNoDenormals() {
#if defined(JCLAP_DSP_DENORMALS_SSE)
      old_ = _mm_getcsr();
      _mm_setcsr(static_cast<unsigned>(old_) | 0x8040u); // flush to zero, denormals are zero
#elif defined(JCLAP_DSP_DENORMALS_ARM64)
      uint64_t fpcr;
      asm volatile("mrs %0, fpcr" : "=r"(fpcr));
      old_ = fpcr;
      asm volatile("msr fpcr, %0" : : "r"(fpcr | (uint64_t{1} << 24))); // FZ
#endif
   }
   ~ScopedNoDenormals() {
#if defined(JCLAP_DSP_DENORMALS_SSE)
      _mm_setcsr(static_cast<unsigned>(old_));
#elif defined(JCLAP_DSP_DENORMALS_ARM64)
      asm volatile("msr fpcr, %0" : : "r"(old_));
#endif
   }
   ScopedNoDenormals(const ScopedNoDenormals &) = delete;
   ScopedNoDenormals &operator=(const ScopedNoDenormals &) = delete;

private:
   [[maybe_unused]] uint64_t old_ = 0;
};

} // namespace jclap::dsp
