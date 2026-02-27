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
#ifndef DYLAN_STR_H
#define DYLAN_STR_H

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "util.h"

struct str;
typedef usize (str_err)(struct str *, void *, usize l);

typedef struct str {
  char *m;
  usize l;
  usize c;

  str_err *f;
  void *ctx;
} str;

#define STR_ERR ((usize) -1)
#define STR_PUSH(s, p)          str_push((s), (p), sizeof(p) - 1)
#define STR_COPY(s, p)          str_copy((s), (p), sizeof(p) - 1)

#define STR_INIT(s, m, f, ctx) do {                                            \
  static char b[(m)];                                                          \
  str_init((s), (b), sizeof(b), (f), (ctx));                                   \
} while (0)

static inline void
str_init(str *s, char *b, usize c, str_err *f, void *ctx)
{
  s->m = b;
  s->l = -(!b || !c);
  s->c = c;
  s->f = f;
  s->ctx = ctx;
}

static inline usize
str_cb(str *s, usize l)
{
  return s->f ? s->f(s, s->ctx, l) : STR_ERR;
}

static inline int
str_fit(str *s, usize l)
{
  if (s->l + l < s->c) return 1;
  return str_cb(s, l) != STR_ERR;
}

static inline void
str_copy(str *s, const char *p, usize l)
{
  memcpy(&s->m[s->l], p, l);
  s->l += l;
}

static inline void
str_copy_c(str *s, char c)
{
  s->m[s->l++] = c;
}

static inline cut
str_push(str *s, const char *p, usize l)
{
  if (!str_fit(s, l)) return (cut) { 0, 0 };
  str_copy(s, p, l);
  return (cut){ &s->m[s->l - l], l };
}

static inline void
str_push_c(str *s, char c)
{
  if (!str_fit(s, 1)) return;
  str_copy_c(s, c);
}

static inline void
str_push_s(str *s, const char *p)
{
  str_push(s, p, strlen(p));
}

static inline void
str_memset(str *s, int c, usize n)
{
  if (!str_fit(s, n)) return;
  memset(&s->m[s->l], c, n);
  s->l += n;
}

static inline void
str_push_u32_b(str *s, u32 v, u32 b, int c, usize l)
{
  static const char d[] = "0123456789abcdef";
  char o[33];
  char *p = &o[sizeof(o)];
  assert(b >= 2 && b <= 16);
  do {
    *--p = d[v % b];
    v /= b;
  } while (v);
  usize n = (usize)(&o[sizeof(o)] - p);
  if (n < l) str_memset(s, c, l - n);
  str_push(s, p, n);
}

static inline void
str_push_u32_p(str *s, u32 v, int c, usize l)
{
  str_push_u32_b(s, v, 10, c, l);
}

static inline void
str_push_u32(str *s, u32 v)
{
  str_push_u32_p(s, v, 0, 0);
}

static inline void
str_push_u64(str *s, u64 v)
{
  char b[21];
  char *p = &b[sizeof(b)];
  do {
    *--p = (char)('0' + (v % 10));
    v /= 10;
  } while (v);
  str_push(s, p, (usize)(&b[sizeof(b)] - p));
}

static inline void
str_push_sanitize(str *s, const char *p, usize l)
{
  if (!str_fit(s, l)) return;
  char *d = s->m + s->l;
  const unsigned char *b = (const unsigned char *) p;
  for (usize i = 0; i < l; i++) {
    unsigned char c = b[i];
    d[i] = (char)(c >= 0x20 && c != 0x7F ? c : '?');
  }
  s->l += l;
}

static inline int
str_cmp(str *a, str *b)
{
  return a->l == b->l && *a->m == *b->m && !memcmp(a->m, b->m, b->l);
}

static inline void
str_terminate(str *s)
{
  if (str_fit(s, 1)) s->m[s->l] = 0;
}

#endif // DYLAN_STR_H

