/*
 * simd.c -- optimized vector operations
 *
 * Copyright (c) 2020, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#include "simd.h"

extern inline uint8_t
nsd_v16_findeq_u8(uint8_t chr, const uint8_t vec[16], uint8_t max);

extern inline uint8_t
nsd_v16_findgt_u8(uint8_t chr, const uint8_t vec[16], uint8_t max);

extern inline uint8_t
nsd_v32_findeq_u8(uint8_t chr, const uint8_t vec[32], uint8_t max);

extern inline uint8_t
nsd_v32_findgt_u8(uint8_t chr, const uint8_t vec[32], uint8_t max);
