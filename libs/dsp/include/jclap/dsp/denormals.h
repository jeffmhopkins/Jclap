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
#elif defined(_M_ARM64) && defined(_MSC_VER)
// MSVC on ARM64 has no inline assembly and no __aarch64__; it reaches FPCR through intrinsics.
#include <intrin.h>
#ifndef ARM64_FPCR
#define ARM64_FPCR 0x5A20 // ARM64_SYSREG(3, 3, 4, 4, 0), as in arm64intr.h
#endif
#define JCLAP_DSP_DENORMALS_ARM64 1
#define JCLAP_DSP_DENORMALS_ARM64_MSVC 1
#endif

namespace jclap::dsp {

class ScopedNoDenormals {
public:
   ScopedNoDenormals() {
#if defined(JCLAP_DSP_DENORMALS_SSE)
      old_ = _mm_getcsr();
      _mm_setcsr(static_cast<unsigned>(old_) | 0x8040u); // flush to zero, denormals are zero
#elif defined(JCLAP_DSP_DENORMALS_ARM64_MSVC)
      old_ = static_cast<uint64_t>(_ReadStatusReg(ARM64_FPCR));
      _WriteStatusReg(ARM64_FPCR, static_cast<__int64>(old_ | (uint64_t{1} << 24))); // FZ
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
#elif defined(JCLAP_DSP_DENORMALS_ARM64_MSVC)
      _WriteStatusReg(ARM64_FPCR, static_cast<__int64>(old_));
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
