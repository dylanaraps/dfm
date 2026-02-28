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
#ifndef DYLAN_BITSET_H
#define DYLAN_BITSET_H

#include <stdint.h>
#include <string.h>

#include "util.h"

enum {
  BITSET_WORD_BITS  = 64,
  BITSET_WORD_SHIFT =  6,
  BITSET_WORD_MASK  = BITSET_WORD_BITS - 1,
};

#define BITSET_W(n) (((n) + BITSET_WORD_MASK) >> BITSET_WORD_SHIFT)

static inline u8
bitset_get(const u64 *b, usize i)
{
  return (b[i >> BITSET_WORD_SHIFT] >> (i & BITSET_WORD_MASK)) & 1ull;
}

static inline void
bitset_set(u64 *b, usize i)
{
  b[i >> BITSET_WORD_SHIFT] |= 1ull << (i & BITSET_WORD_MASK);
}

static inline void
bitset_clr(u64 *b, usize i)
{
  b[i >> BITSET_WORD_SHIFT] &= ~(1ull << (i & BITSET_WORD_MASK));
}

static inline void
bitset_tog(u64 *b, usize i)
{
  b[i >> BITSET_WORD_SHIFT] ^= 1ull << (i & BITSET_WORD_MASK);
}

static inline void
bitset_assign(u64 *b, usize i, int v)
{
  u64 *w = &b[i >> BITSET_WORD_SHIFT];
  u64  m = 1ull << (i & BITSET_WORD_MASK);
  *w = v ? (*w | m) : (*w & ~m);
}

static inline usize
bitset_count(const u64 *b, usize l)
{
  usize c = 0;
  for (usize i = 0, w = BITSET_W(l); i < w; i++)
    c += u64_popcount(b[i]);
  return c;
}

static inline void
bitset_set_all(u64 *v, usize n)
{
  memset(v, 0xff, BITSET_W(n) * sizeof *v);
}

static inline void
bitset_clr_all(u64 *v, usize n)
{
  memset(v, 0, BITSET_W(n) * sizeof *v);
}

static inline void
bitset_invert(u64 *v, usize n)
{
  usize w = BITSET_W(n);
  for (usize i = 0; i < w; i++) v[i] = ~v[i];
  usize r = n & BITSET_WORD_MASK;
  if (r) v[w - 1] &= (1ull << r) - 1;
}

static inline void
bitset_swap(u64 *b, usize i, usize j)
{
  u8 bi = bitset_get(b, i);
  u8 bj = bitset_get(b, j);
  bitset_assign(b, i, bj);
  bitset_assign(b, j, bi);
}

static inline usize
bitset_next_set(const u64 *b, usize i, usize n)
{
  if (i >= n) return SIZE_MAX;
  usize wi = i >> BITSET_WORD_SHIFT;
  usize wN = BITSET_W(n);
  u64 w = b[wi];
  w &= (~0ull << (i & BITSET_WORD_MASK));

  for (;;) {
    if (w) {
      usize j = (wi << BITSET_WORD_SHIFT) + u64_ctz(w);
      return j < n ? j : SIZE_MAX;
    }

    if (++wi >= wN) break;
    w = b[wi];
  }

  return SIZE_MAX;
}

static inline usize
bitset_prev_set(const u64 *b, usize i, usize n)
{
  if (i >= n) i = n - 1;
  usize wi = i >> BITSET_WORD_SHIFT;
  u64 w = b[wi];
  u64 r = (u64)(i & BITSET_WORD_MASK);
  w &= (r == BITSET_WORD_MASK) ? ~0ull : ((1ull << (r + 1)) - 1ull);

  for (;;) {
    if (w)
      return (wi << BITSET_WORD_SHIFT) + (BITSET_WORD_MASK - (usize)u64_clz(w));
    if (!wi) break;
    w = b[--wi];
  }

  return SIZE_MAX;
}

#endif // DYLAN_BITSET_H

