/*
 * Copyright (c) 2026 Dylan Araps
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef DYLAN_DATE_H
#define DYLAN_DATE_H

//
// Fast date algorithm, C implementation.
// Source: https://www.benjoffe.com/fast-date-64
//
#include <stddef.h>
#include <stdint.h>

#include "util.h"

#define C1 505054698555331ull   // floor(2^64 * 4 / 146097)
#define C2 50504432782230121ull // ceil(2^64 * 4 / 1461)
#define C3 8619973866219416ull  // floor(2^64 / 2140)

#define SCALE  32u
#define SHIFT0 (30556u * SCALE)
#define SHIFT1 (5980u * SCALE)

#if defined(__SIZEOF_INT128__)
#define ERAS    4726498270ull
#define D_SHIFT ((u64)(146097ull * ERAS - 719469ull))
#define Y_SHIFT ((u64)(400ull * ERAS - 1ull))

static inline u64
mulhi(u64 a, u64 b)
{
  __uint128_t p = (__uint128_t)a * (__uint128_t)b;
  return (u64)(p >> 64);
}

static inline void
mul128(u64 a, u64 b, u64 *hi, u64 *lo)
{
  __uint128_t p = (__uint128_t)a * (__uint128_t)b;
  *hi = (u64)(p >> 64);
  *lo = (u64)p;
}

#else

#define ERAS    14704u
#define D_SHIFT ((u64)(146097ull * (u64)ERAS - 719469ull))
#define Y_SHIFT ((u64)(400ull * (u64)ERAS - 1ull))

static inline u64
mulhi(u64 a, u64 b)
{
  u64 a0 = (u32)a;
  u64 a1 = a >> 32;
  u64 b0 = (u32)b;
  u64 b1 = b >> 32;
  u64 p00 = a0 * b0;
  u64 p01 = a0 * b1;
  u64 p10 = a1 * b0;
  u64 p11 = a1 * b1;
  u64 mid = (p00 >> 32) + (u32)p01 + (u32)p10;
  return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

static inline void
mul128(u64 a, u64 b, u64 *hi, u64 *lo)
{
  u64 a0 = (u32)a;
  u64 a1 = a >> 32;
  u64 b0 = (u32)b;
  u64 b1 = b >> 32;
  u64 p00 = a0 * b0;
  u64 p01 = a0 * b1;
  u64 p10 = a1 * b0;
  u64 p11 = a1 * b1;
  u64 mid = (p00 >> 32) + (u32)p01 + (u32)p10;
  *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
  *lo = (mid << 32) | (u32)p00;
}

#endif // defined(__SIZEOF_INT128__)

static inline void
ut_to_date(s32 day, s32 *Y, u32 *M, u32 *D)
{
  u64 rev = (u64)D_SHIFT - (u64)(s64)day;                  // Reverse day count.
  u64 cen = mulhi(C1, rev);                                // Divide 36524.25
  u64 jul = rev - cen / 4u + cen;                          // Julian map.
  u64 num_hi, num_lo;
  mul128(C2, jul, &num_hi, &num_lo);                       // Divide 365.25
  u64 yrs64 = (u64)Y_SHIFT - (u64)num_hi;                  // Forward year.
  u64 yrs = (u32)yrs64;
  u32 ypt = (u32)mulhi((u64)(24451u * SCALE), num_lo);     // Year (backwards).
  u32 bump  = (ypt < (3952u * SCALE));                     // Jan or Feb.
  u32 shift = bump ? SHIFT1 : SHIFT0;                      // Month offset.
  u32 N = (u32)((yrs % 4u) * (16u * SCALE) + shift - ypt); // Leap years.
  u32 m = N / (2048u * SCALE);
  u32 d = (u32)mulhi(C3, (u64)(N % (2048u * SCALE)));      // Divide 2140
  *Y = (s32)yrs + (s32)bump;
  *M = m;
  *D = d + 1u;
}

static inline void
ut_to_date_time(s64 tz, s32 day, s32 *Y, u32 *M, u32 *D, u32 *h, u32 *m, u32 *s)
{
  s64 us   = tz + day;
  s32 days = (s32)(us / 86400);
  s32 r    = (s32)(us % 86400);
  if (r < 0) { r += 86400; days -= 1; }
  ut_to_date(days, Y, M, D);
  u32 hr = r / 3600;
  r -= hr * 3600;
  *h = hr;
  u32 mn = r / 60;
  *m = mn;
  *s = r - mn * 60;
}

#endif // DYLAN_DATE_H

