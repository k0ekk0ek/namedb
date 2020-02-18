/*
 * simd.h -- optimized vector operations
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#ifndef NSD_SIMD_H
#define NSD_SIMD_H

#include <stdint.h>

#if defined(__i386__) || defined(__x86_64__)
# include <immintrin.h>
#elif defined(__arm)
/* https://developer.arm.com/architectures/instruction-sets/simd-isas/neon */
# include <arm_neon.h>
#endif

#if HAVE_SSE2
inline uint8_t
nsd_v16_findeq_u8(uint8_t chr, const uint8_t vec[16], uint8_t max)
{
  __m128i cmp;
  uint16_t bitmap;
  uint16_t mask = max < 16 ? (1 << max) - 1 : (uint16_t)-1;

  cmp = _mm_cmpeq_epi8(
    _mm_set1_epi8(chr), _mm_loadu_si128((__m128i*)vec));
  bitmap = _mm_movemask_epi8(cmp) & mask;
  return __builtin_ctz(bitmap);
}

inline uint8_t
nsd_v16_findgt_u8(uint8_t chr, const uint8_t vec[16], uint8_t max)
{
  __m128i cmp;
  uint16_t bitmap;
  uint16_t mask = max < 16 ? (1 << max) - 1 : (uint16_t)-1;

  cmp = _mm_cmpgt_epi8(
    _mm_set1_epi8(chr), _mm_loadu_si128((__m128i*)vec));
  bitmap = _mm_movemask_epi8(cmp);
  return __builtin_ctz(~bitmap & mask);
}
#else
inline uint8_t
nsd_v16_findeq_u8(uint8_t chr, const uint8_t vec[16], uint8_t max)
{
  for (uint8_t idx = 0; idx < 16; idx++) {
    if (vec[idx] == chr) {
      return idx + 1;
    }
  }
  return 0;
}

inline uint8_t
nsd_v16_findgt_u8(uint8_t chr, const uint8_t vec[16], uint8_t max)
{
  for (uint8_t idx = 0; idx < 16; idx++) {
    if (vec[idx] > chr) {
      return idx + 1;
    }
  }
  return 0;
}
#endif

#if HAVE_AVX2
inline uint8_t
nsd_v32_findeq_u8(uint8_t chr, const uint8_t vec[32], uint8_t max)
{
  __m256i cmp;
  uint32_t bitmap;
  uint32_t mask = max < 32 ? (1 << max) - 1 : (uint32_t)-1;

  cmp = _mm256_cmpeq_epi8(
    _mm256_set1_epi8(chr), _mm256_loadu_si256((__m256i*)vec));
  bitmap = _mm256_movemask_epi8(cmp) & mask;
  return __builtin_ctz(bitmap);
}

inline uint8_t
nsd_v32_findgt_u8(uint8_t chr, const uint8_t vec[32], uint8_t max)
{
  __m256i cmp;
  uint32_t bitmap;
  uint32_t mask = max < 32 ? (1 << max) - 1 : (uint32_t)-1;

  cmp = _mm256_cmpgt_epi8(
    _mm256_set1_epi8(chr), _mm256_loadu_si256((__m256i*)vec));
  bitmap = _mm256_movemask_epi8(cmp);
  return __builtin_ctz(~bitmap & mask);
}
#endif

#endif /* NSD_SIMD_H */
