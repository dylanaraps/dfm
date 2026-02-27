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
#ifndef DYLAN_UTF8_H
#define DYLAN_UTF8_H

#include "util.h"

static inline usize
utf8_expected(u8 b)
{
  static const u8 L[] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,2,2,2,2,3,3,4,0
  };
  return L[b >> 3];
}

static inline int
utf8_width(u32 c)
{
  if (c == 0) return 0;

  // Control.
  if (c < 0x20) return 0;
  if (c >= 0x7f && c < 0xa0) return 0;

  // Zero width joiner.
  if (c == 0x200d) return 0;

  // Combining.
  if ((c >= 0x0300 && c <= 0x036f) ||
      (c >= 0x1ab0 && c <= 0x1aff) ||
      (c >= 0x1dc0 && c <= 0x1dff) ||
      (c >= 0x20d0 && c <= 0x20ff) ||
      (c >= 0xfe20 && c <= 0xfe2f) ||
      (c >= 0xe0100 && c <= 0xe01ef))
    return 0;

  // Variation selectors.
  if ((c >= 0xfe00 && c <= 0xfe0f))
    return 0;

  // Emoji modifiers.
  if (c >= 0x1f3fb && c <= 0x1f3ff)
    return 0;

  // East asian wide.
  if ((c >= 0x1100 && c <= 0x115f) ||
      c == 0x2329 || c == 0x232a ||
      (c >= 0x2e80 && c <= 0xa4cf && c != 0x303f) ||
      (c >= 0xac00 && c <= 0xd7a3) ||
      (c >= 0xf900 && c <= 0xfaff) ||
      (c >= 0xfe10 && c <= 0xfe19) ||
      (c >= 0xfe30 && c <= 0xfe6f) ||
      (c >= 0xff00 && c <= 0xff60) ||
      (c >= 0xffe0 && c <= 0xffe6) ||
      (c >= 0x20000 && c <= 0x2fffd) ||
      (c >= 0x30000 && c <= 0x3fffd))
    return 2;

  // Emoji block.
  if ((c >= 0x1f300 && c <= 0x1faff) ||
      (c >= 0x2600 && c <= 0x27bf) ||
      (c >= 0x2b50 && c <= 0x2b55))
    return 2;

  return 1;
}

//
// Branchless UTF8 decoder by Skeeto.
// Source: https://nullprogram.com/blog/2017/10/06/
//
static inline void *
utf8_decode(void *b, u32 *c)
{
  unsigned char *s = (unsigned char *)b;
  usize l = utf8_expected(s[0]);
  static const int m[] = {0x00, 0x7f, 0x1f, 0x0f, 0x07};
  static const int shc[] = {0, 18, 12, 6, 0};
  *c  = (u32)(s[0] & m[l]) << 18;
  *c |= (u32)(s[1] & 0x3f) << 12;
  *c |= (u32)(s[2] & 0x3f) <<  6;
  *c |= (u32)(s[3] & 0x3f);
  *c >>= shc[l];
  return s + l + !l;
}

static void *
utf8_decode_untrusted(void *b, u32 *c, int *e)
{
  static const u32 mi[] = {4194304, 0, 128, 2048, 65536};
  static const int she[] = {0, 6, 4, 2, 0};
  unsigned char *s = (unsigned char *)b;
  unsigned char *n = utf8_decode(b, c);
  usize l = utf8_expected(s[0]);
  *e  = (*c < mi[l]) << 6;          // Non-canonical encoding.
  *e |= ((*c >> 11) == 0x1b) << 7;  // Surrogate half?
  *e |= (*c > 0x10FFFF) << 8;       // Out of range?
  *e |= (s[1] & 0xc0) >> 2;
  *e |= (s[2] & 0xc0) >> 4;
  *e |= (s[3])        >> 6;
  *e ^= 0x2a;                       // Top two bits of each tail byte correct?
  *e >>= she[l];
  return n;
}

static inline usize
utf8_decode_rev(const unsigned char *s, usize x, u32 *c)
{
  usize i = x;
  while (i > 0 && (s[i - 1] & 0xc0) == 0x80) i--;
  if (i > 0) i--;
  usize l = x - i;
  utf8_decode((void *)(s + i), c);
  return l;
}

static inline usize
utf8_cols(const void *s, usize l, usize *lw)
{
  usize w = 0;
  const unsigned char *p = (const unsigned char *)s;
  const unsigned char *e = p + l;
  *lw = 0;
  while (p < e) {
    u32 cp;
    p = utf8_decode((void *)p, &cp);
    *lw = utf8_width(cp);
    w += *lw;
  }
  return w;
}

static inline usize
utf8_trunc_narrow(const char *s, usize l, usize c)
{
  const unsigned char *p = (const unsigned char *)s;
  const unsigned char *e = p + l;
  for (usize i = 0; p < e && i < c; i++) {
    unsigned char b = *p++;
    if (!(b & 0x80)) continue;
    for (; p < e && ((*p & 0xC0) == 0x80); p++);
  }
  return (usize)(p - (const unsigned char *)s);
}

static inline usize
utf8_trunc_wide(const char *s, usize l, usize c)
{
  const unsigned char *p = (const unsigned char *)s;
  const unsigned char *e = p + l;
  for (usize i = 0; p < e && i < c; ) {
    u32 cp;
    const unsigned char *n = (const unsigned char *)utf8_decode((void *)p, &cp);
    usize a = (usize)(n - p);
    if (!a) a = 1;
    int w = utf8_width(cp);
    if (i + w > c) break;
    i += w;
    p += a;
  }
  return (usize)(p - (const unsigned char *)s);
}

#endif // DYLAN_UTF8_H

