// -*- c++ -*-
//
// Copyright 1997-2008 Matt T. Yourst <yourst@yourst.com>
//
// This program is free software; it is licensed under the
// GNU General Public License, Version 2.
//

#ifndef _GLOBALS_H_
#define _GLOBALS_H_

#include <assert.h>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>
extern "C" {
#include <sys/ptrace.h>
}

#include "typedefs.h"

#ifdef __cplusplus

#include <format>
#include <math.h>
#include <float.h>

#define __stringify_1(x) #x
#define stringify(x) __stringify_1(x)

#define alignto(x) __attribute__((aligned(x)))
#define insection(x) __attribute__((section(x)))
#define packedstruct __attribute__((packed))
#define noinline __attribute__((noinline))

#define unlikely(x) (__builtin_expect(!!(x), 0))
#define likely(x) (__builtin_expect(!!(x), 1))
#define isconst(x) (__builtin_constant_p(x))
#define getcaller() (__builtin_return_address(0))
#define asmlinkage extern "C"


asmlinkage void assert_fail(const char* __assertion, const char* __file, unsigned int __line, const char* __function)
    __attribute__((__noreturn__));

// For embedded debugging use only:
static inline void assert_fail_trap(const char* __assertion, const char* __file, unsigned int __line,
                                    const char* __function) {
  asm("ud2a" : : "a"(__assertion), "b"(__file), "c"(__line), "d"(__function));
}


template<typename T>
struct isprimitive_t {
  static const bool primitive = 0;
};
#define MakePrimitive(T)                                                                                               \
  template<>                                                                                                           \
  struct isprimitive_t<T> {                                                                                            \
    static const bool primitive = 1;                                                                                   \
  }
MakePrimitive(signed char);
MakePrimitive(unsigned char);
MakePrimitive(signed short);
MakePrimitive(unsigned short);
MakePrimitive(signed int);
MakePrimitive(unsigned int);
MakePrimitive(signed long);
MakePrimitive(unsigned long);
MakePrimitive(signed long long);
MakePrimitive(unsigned long long);
MakePrimitive(float);
MakePrimitive(double);
MakePrimitive(bool);

template<typename T>
struct ispointer_t {
  static const bool pointer = 0;
};
template<typename T>
struct ispointer_t<T*> {
  static const bool pointer = 1;
};
#define ispointer(T) (ispointer_t<T>::pointer)
#define isprimitive(T) (isprimitive_t<T>::primitive)

// Null pointer to the specified object type, for computing field offsets
#define offsetof_(T, field) ((Waddr)(&(reinterpret_cast<T*>(0)->field)) - ((Waddr) reinterpret_cast<T*>(0)))
#define baseof(T, field, ptr) ((T*)(((byte*)(ptr)) - offsetof_(T, field)))
// Restricted (non-aliased) pointers:
#define noalias __restrict__

// Add raw data auto-casts to a structured or bitfield type
#define RawDataAccessors(structtype, rawtype)                                                                          \
  structtype() {}                                                                                                      \
  structtype(rawtype rawbits) {                                                                                        \
    *((rawtype*)this) = rawbits;                                                                                       \
  }                                                                                                                    \
  operator rawtype() const {                                                                                           \
    return *((rawtype*)this);                                                                                          \
  }

// Typecasts in bizarre ways required for binary form access
union W32orFloat {
  W32 w;
  float f;
};
union W64orDouble {
  W64 w;
  double d;
  struct {
    W32 lo;
    W32s hi;
  } hilo;
  struct {
    W64 mantissa : 52, exponent : 11, negative : 1;
  } ieee;
  // This format makes it easier to see if a NaN is a signalling NaN.
  struct {
    W64 mantissa : 51, qnan : 1, exponent : 11, negative : 1;
  } ieeenan;
};

static inline const float W32toFloat(W32 x) {
  union W32orFloat c;
  c.w = x;
  return c.f;
}
static inline const W32 FloatToW32(float x) {
  union W32orFloat c;
  c.f = x;
  return c.w;
}
static inline const double W64toDouble(W64 x) {
  union W64orDouble c;
  c.w = x;
  return c.d;
}
static inline const W64 DoubleToW64(double x) {
  union W64orDouble c;
  c.d = x;
  return c.w;
}

//
// Functional constructor
//

template<typename T>
static inline bool inrange(const T& v, const T& minv, const T& maxv) {
  std::decay_t<T> _v = v;
  return ((_v >= minv) & (_v <= maxv));
}

// Bit fitting
static inline bool fits_in_signed_nbit(W64s v, int b) {
  return inrange(v, W64s(-(1ULL << (b - 1))), W64s(+(1ULL << (b - 1)) - 1));
}


#define sqr(x) ((x) * (x))
#define cube(x) ((x) * (x) * (x))
#define bit(x, n) (((x) >> (n)) & 1)

#define bitmask(l) (((l) == 64) ? (W64)(-1LL) : ((1LL << (l)) - 1LL))
#define bits(x, i, l) (((x) >> (i)) & bitmask(l))
#define lowbits(x, l) bits(x, 0, l)
#define setbit(x, i) ((x) |= (1LL << (i)))
#define clearbit(x, i) ((x) &= (W64)(~(1LL << (i))))
#define assignbit(x, i, v) ((x) = (((x) &= (W64)(~(1LL << (i)))) | (((W64)((bool)(v))) << i)));

#define foreach(i, n) for (size_t i = 0; i < (n); i++)

static inline W64s signext64(W64s x, const int i) {
  return (x << (64 - i)) >> (64 - i);
}
static inline W32s signext32(W32s x, const int i) {
  return (x << (32 - i)) >> (32 - i);
}
static inline W16s signext16(W16s x, const int i) {
  return (x << (16 - i)) >> (16 - i);
}

static inline W64s bitsext64(W64s x, const int i, const int l) {
  return signext64(bits(x, i, l), l);
}
static inline W32s bitsext32(W32s x, const int i, const int l) {
  return signext32(bits(x, i, l), l);
}
static inline W16s bitsext16(W16s x, const int i, const int l) {
  return signext16(bits(x, i, l), l);
}

typedef byte v16qi __attribute__((vector_size(16)));
typedef v16qi vec16b;
typedef W16 v8hi __attribute__((vector_size(16)));
typedef v8hi vec8w;
typedef float v4sf __attribute__((vector_size(16)));
typedef v4sf vec4f;
typedef W32 v4si __attribute__((vector_size(16)));
typedef v4si vec4i;
typedef float v2df __attribute__((vector_size(16)));
typedef v2df vec2d;

// std::formatter specializations for vector types - must be declared early before any use
template<>
struct std::formatter<v16qi> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
  auto format(const v16qi& v, std::format_context& ctx) const {
    auto out = ctx.out();
    const unsigned char* b = (const unsigned char*)&v;
    for (int i = 15; i >= 0; i--) {
      if (i < 15)
        out = std::format_to(out, "{}", (i == 7) ? '.' : ' ');
      out = std::format_to(out, "{:02x}", static_cast<unsigned int>(b[i]));
    }
    return out;
  }
};

template<>
struct std::formatter<v8hi> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
  auto format(const v8hi& v, std::format_context& ctx) const {
    auto out = ctx.out();
    const W16* b = (const W16*)&v;
    for (int i = 0; i < 8; i++) {
      if (i)
        out = std::format_to(out, " ");
      out = std::format_to(out, "{:>5}", b[i]);
    }
    return out;
  }
};

inline vec16b x86_sse_pcmpeqb(vec16b a, vec16b b) {
  asm("pcmpeqb %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return a;
}
inline vec8w x86_sse_pcmpeqw(vec8w a, vec8w b) {
  asm("pcmpeqw %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return a;
}
inline vec4i x86_sse_pcmpeqd(vec4i a, vec4i b) {
  asm("pcmpeqd %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return a;
}
inline vec16b x86_sse_psubusb(vec16b a, vec16b b) {
  asm("psubusb %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return a;
}
inline vec16b x86_sse_paddusb(vec16b a, vec16b b) {
  asm("paddusb %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return a;
}
inline vec16b x86_sse_pandb(vec16b a, vec16b b) {
  asm("pand %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return a;
}
inline vec8w x86_sse_psubusw(vec8w a, vec8w b) {
  asm("psubusb %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return a;
}
inline vec8w x86_sse_paddusw(vec8w a, vec8w b) {
  asm("paddsub %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return a;
}
inline vec8w x86_sse_pandw(vec8w a, vec8w b) {
  asm("pand %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return a;
}
inline vec16b x86_sse_packsswb(vec8w a, vec8w b) {
  asm("packsswb %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return (vec16b)a;
}
inline W32 x86_sse_pmovmskb(vec16b vec) {
  W32 mask;
  asm("pmovmskb %[vec],%[mask]" : [mask] "=r"(mask) : [vec] "x"(vec));
  return mask;
}
inline W32 x86_sse_pmovmskw(vec8w vec) {
  return x86_sse_pmovmskb(x86_sse_packsswb(vec, vec)) & 0xff;
}
inline vec16b x86_sse_psadbw(vec16b a, vec16b b) {
  asm("psadbw %[b],%[a]" : [a] "+x"(a) : [b] "xg"(b));
  return a;
}
template<int i>
inline W16 x86_sse_pextrw(vec16b a) {
  W32 rd;
  asm("pextrw %[i],%[a],%[rd]" : [rd] "=r"(rd) : [a] "x"(a), [i] "N"(i));
  return rd;
}

inline vec16b x86_sse_ldvbu(const vec16b* m) {
  vec16b rd;
  asm("movdqu %[m],%[rd]" : [rd] "=x"(rd) : [m] "xm"(*m));
  return rd;
}
inline void x86_sse_stvbu(vec16b* m, const vec16b ra) {
  asm("movdqu %[ra],%[m]" : [m] "=m"(*m) : [ra] "x"(ra) : "memory");
}
inline vec8w x86_sse_ldvwu(const vec8w* m) {
  vec8w rd;
  asm("movdqu %[m],%[rd]" : [rd] "=x"(rd) : [m] "xm"(*m));
  return rd;
}
inline void x86_sse_stvwu(vec8w* m, const vec8w ra) {
  asm("movdqu %[ra],%[m]" : [m] "=m"(*m) : [ra] "x"(ra) : "memory");
}

inline vec16b x86_sse_zerob() {
  vec16b rd;
  asm("pxor %[rd],%[rd]" : [rd] "+x"(rd));
  return rd;
}
inline vec16b x86_sse_onesb() {
  vec16b rd;
  asm("pcmpeqb %[rd],%[rd]" : [rd] "+x"(rd));
  return rd;
}
inline vec8w x86_sse_zerow() {
  vec8w rd;
  asm("pxor %[rd],%[rd]" : [rd] "+x"(rd));
  return rd;
}
inline vec8w x86_sse_onesw() {
  vec8w rd;
  asm("pcmpeqw %[rd],%[rd]" : [rd] "+x"(rd));
  return rd;
}

// If lddqu is available (SSE3: Athlon 64 (some cores, like X2), Pentium 4 Prescott), use that instead. It may be faster.

extern const byte byte_to_vec16b[256][16];
extern const byte index_bytes_vec16b[16][16];
extern const byte index_bytes_plus1_vec16b[16][16];

inline vec16b x86_sse_dupb(const byte b) {
  return *((vec16b*)&byte_to_vec16b[b]);
}

inline vec8w x86_sse_dupw(const W16 b) {
  W32 w = (b << 16) | b;
  vec8w v;
  W32* wp = (W32*)&v;
  wp[0] = w;
  wp[1] = w;
  wp[2] = w;
  wp[3] = w;
  return v;
}

inline void x86_set_mxcsr(W32 value) {
  asm volatile("ldmxcsr %[value]" : : [value] "m"(value));
}
inline W32 x86_get_mxcsr() {
  W32 value;
  asm volatile("stmxcsr %[value]" : [value] "=m"(value));
  return value;
}
union MXCSR {
  struct {
    W32 ie : 1, de : 1, ze : 1, oe : 1, ue : 1, pe : 1, daz : 1, im : 1, dm : 1, zm : 1, om : 1, um : 1, pm : 1, rc : 2,
        fz : 1;
  } fields;
  W32 data;

  MXCSR() {}
  MXCSR(W32 v) { data = v; }
  operator W32() const { return data; }
};
enum { MXCSR_ROUND_NEAREST, MXCSR_ROUND_DOWN, MXCSR_ROUND_UP, MXCSR_ROUND_TOWARDS_ZERO };
#define MXCSR_EXCEPTION_DISABLE_MASK 0x1f80 // OR this into mxcsr to disable all exceptions
#define MXCSR_DEFAULT 0x1f80                // default settings (no exceptions, defaults for rounding and denormals)

template<typename T>
inline bool x86_bt(T r, T b) {
  using U = std::make_unsigned_t<T>;
  constexpr int digits = std::numeric_limits<U>::digits;
  return (static_cast<U>(r) >> (static_cast<U>(b) % digits)) & 1;
}
// Return the updated data; ignore the old value
template<typename T>
inline W64 x86_bts(T r, T b) {
  using U = std::make_unsigned_t<T>;
  constexpr int digits = std::numeric_limits<U>::digits;
  return static_cast<T>(static_cast<U>(r) | (U(1) << (static_cast<U>(b) % digits)));
}
template<typename T>
inline W64 x86_btr(T r, T b) {
  using U = std::make_unsigned_t<T>;
  constexpr int digits = std::numeric_limits<U>::digits;
  return static_cast<T>(static_cast<U>(r) & ~(U(1) << (static_cast<U>(b) % digits)));
}
template<typename T>
inline W64 x86_btc(T r, T b) {
  using U = std::make_unsigned_t<T>;
  constexpr int digits = std::numeric_limits<U>::digits;
  return static_cast<T>(static_cast<U>(r) ^ (U(1) << (static_cast<U>(b) % digits)));
}

// Return the old value of the bit, but still update the data
template<typename T>
inline bool x86_test_bts(T& r, T b) {
  bool c = x86_bt(r, b);
  r = static_cast<T>(x86_bts(r, b));
  return c;
}
template<typename T>
inline bool x86_test_btr(T& r, T b) {
  bool c = x86_bt(r, b);
  r = static_cast<T>(x86_btr(r, b));
  return c;
}
template<typename T>
inline bool x86_test_btc(T& r, T b) {
  bool c = x86_bt(r, b);
  r = static_cast<T>(x86_btc(r, b));
  return c;
}

static inline W16 x86_sse_maskeqb(const vec16b v, byte target) {
  return x86_sse_pmovmskb(x86_sse_pcmpeqb(v, x86_sse_dupb(target)));
}

// This is a barrier for the compiler only, NOT the processor!
#define barrier() std::atomic_signal_fence(std::memory_order_seq_cst)

// Denote parallel sections for the compiler
#define parallel

static inline void cpu_pause() {
  asm volatile("pause" : : : "memory");
}

static inline void prefetch(const void* x) {
  asm volatile("prefetcht0 (%0)" : : "r"(x));
}

static inline void cpuid(int op, W32& eax, W32& ebx, W32& ecx, W32& edx) {
  asm("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(op));
}

static inline W64 rdtsc() {
  W32 lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((W64)lo) | (((W64)hi) << 32);
}

template<typename T>
static inline T x86_ror(T r, int n) {
  using U = std::make_unsigned_t<T>;
  return static_cast<T>(std::rotr(static_cast<U>(r), n));
}

template<typename T>
static inline T x86_rol(T r, int n) {
  using U = std::make_unsigned_t<T>;
  return static_cast<T>(std::rotl(static_cast<U>(r), n));
}

template<typename T>
static inline T dupb(const byte b) {
  return T(b) * T(0x0101010101010101ULL);
}

template<int n>
struct lg {
  static const int value = 1 + lg<n / 2>::value;
};
template<>
struct lg<1> {
  static const int value = 0;
};
#define log2(v) (lg<(v)>::value)

template<int n>
struct lg10 {
  static const int value = 1 + lg10<n / 10>::value;
};
template<>
struct lg10<1> {
  static const int value = 0;
};
template<>
struct lg10<0> {
  static const int value = 0;
};
#define log10(v) (lg10<(v)>::value)

template<int N, typename T>
static inline T foldbits(T a) {
  if (N == 0)
    return 0;

  const int B = (sizeof(T) * 8);
  const int S = (B / N) + ((B % N) ? 1 : 0);

  T z = 0;
  foreach (i, S) {
    z ^= a;
    a >>= N;
  }

  return lowbits(z, N);
}


asmlinkage{
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/ptrace.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/user.h>
};

#include <stdarg.h>

#include "syscalls.h"

#ifdef PAGE_SIZE
#undef PAGE_SIZE
// We're on x86 or x86-64, so pages are always 4096 bytes:
#define PAGE_SIZE 4096
#endif

/*
 * Make these math functions available even inside of member functions with the same name:
 */
static inline float fsqrt(float v) {
  return (float)std::sqrt(v);
}

template<typename T>
static inline void setzero(T& x) {
  memset(&x, 0, sizeof(T));
}

#define HI32(x) (W32)((x) >> 32LL)
#define LO32(x) (W32)((x) & 0xffffffffLL)
#define CONCAT64(hi, lo) ((((W64)(hi)) << 32) + (((W64)(lo)) & 0xffffffffLL))

template<typename T, typename A>
static inline T floor(T x, A a) {
  return (T)(((T)x) & ~((T)(a - 1)));
}
template<typename T, typename A>
static inline T trunc(T x, A a) {
  return (T)(((T)x) & ~((T)(a - 1)));
}
template<typename T, typename A>
static inline T ceil(T x, A a) {
  return (T)((((T)x) + ((T)(a - 1))) & ~((T)(a - 1)));
}
template<typename T, typename A>
static inline T mask(T x, A a) {
  return (T)(((T)x) & ((T)(a - 1)));
}

template<typename T, typename A>
static inline T* floorptr(T* x, A a) {
  return (T*)floor((Waddr)x, a);
}
template<typename T, typename A>
static inline T* ceilptr(T* x, A a) {
  return (T*)ceil((Waddr)x, a);
}
template<typename T, typename A>
static inline T* maskptr(T* x, A a) {
  return (T*)mask((Waddr)x, a);
}
static inline W64 mux64(W64 sel, W64 v0, W64 v1) {
  return (sel & v1) | ((~sel) & v0);
}
template<typename T>
static inline T mux(T sel, T v1, T v0) {
  return (sel & v1) | ((~sel) & v0);
}

template<typename T>
void swap(T& a, T& b) {
  T t = a;
  a = b;
  b = t;
}

#define ptralign(ptr, bytes) ((decltype(ptr))((unsigned long)(ptr) & ~((bytes) - 1)))
#define ptrmask(ptr, bytes) ((decltype(ptr))((unsigned long)(ptr) & ((bytes) - 1)))

template<typename T>
inline void arraycopy(T* dest, const T* source, int count) {
  memcpy(dest, source, count * sizeof(T));
}

template<typename T, typename V>
inline void rawcopy(T& dest, const V& source) {
  memcpy(&dest, &source, sizeof(T));
}

// static inline float randfloat() { return ((float)rand() / RAND_MAX); }

static inline bool aligned(W64 address, int size) {
  return ((address & (W64)(size - 1)) == 0);
}

inline bool strequal(const char* a, const char* b) {
  return (strcmp(a, b) == 0);
}

template<typename T, size_t size>
size_t lengthof(T (&)[size]) {
  return size;
}

extern const byte popcountlut8bit[];
extern const byte lsbindexlut8bit[];

static inline int popcount8bit(byte x) {
  return popcountlut8bit[x];
}

static inline int lsbindex8bit(byte x) {
  return lsbindexlut8bit[x];
}

static inline int popcount(W32 x) {
  return (popcount8bit(x >> 0) + popcount8bit(x >> 8) + popcount8bit(x >> 16) + popcount8bit(x >> 24));
}

static inline int popcount64(W64 x) {
  return popcount(LO32(x)) + popcount(HI32(x));
}


extern const W64 expand_8bit_to_64bit_lut[256];

// LSB index:

// Operand must be non-zero or result is undefined:
inline unsigned int lsbindex32(W32 n) {
  return n ? std::countr_zero(n) : 0;
}

inline int lsbindexi32(W32 n) {
  int r = lsbindex32(n);
  return (n ? r : -1);
}

inline unsigned int lsbindex64(W64 n) {
  return n ? std::countr_zero(n) : 0;
}

inline unsigned int lsbindexi64(W64 n) {
  int r = lsbindex64(n);
  return (n ? r : -1);
}

// static inline unsigned int lsbindex(W32 n) { return lsbindex32(n); }
inline unsigned int lsbindex(W64 n) {
  return lsbindex64(n);
}

// MSB index:

// Operand must be non-zero or result is undefined:
inline unsigned int msbindex32(W32 n) {
  return n ? std::bit_width(n) - 1 : 0;
}

inline int msbindexi32(W32 n) {
  int r = msbindex32(n);
  return (n ? r : -1);
}

inline unsigned int msbindex64(W64 n) {
  return n ? std::bit_width(n) - 1 : 0;
}

inline unsigned int msbindexi64(W64 n) {
  int r = msbindex64(n);
  return (n ? r : -1);
}

// static inline unsigned int msbindex(W32 n) { return msbindex32(n); }
inline unsigned int msbindex(W64 n) {
  return msbindex64(n);
}

#define percent(x, total) (100.0 * ((float)(x)) / ((float)(total)))

inline int add_index_modulo(int index, int increment, int bufsize) {
  // Only if power of 2: return (index + increment) & (bufsize-1);
  index += increment;
  if (index < 0)
    index += bufsize;
  if (index >= bufsize)
    index -= bufsize;
  return index;
}

#include "superstl.h"

template<>
struct std::formatter<MXCSR> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  auto format(const MXCSR& mxcsr, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "0x{:08x}", mxcsr.data);
  }
};

#endif // __cplusplus

#endif // _GLOBALS_H_
