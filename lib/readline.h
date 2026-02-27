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
#ifndef DYLAN_READLINE
#define DYLAN_READLINE

#ifndef RL_MAX
#error "RL_MAX not set"
#endif

#include <assert.h>
#include <string.h>

#include "str.h"
#include "utf8.h"
#include "util.h"

enum {
  RL_NONE,
  RL_PARTIAL,
  RL_FULL,
  RL_CAP = (RL_MAX >> 1) - 3,
};

struct readline {
  str cl;
  str cr;
  cut pr;

  usize vx;
  usize vw;

  usize prw;
  usize clw;
  usize crw;
};

static inline int
rl_is_ifs(int c)
{
  return c == ' ' || c == '\t';
}

static inline usize
rl_prompt(const struct readline *r)
{
  return r->prw + !!r->prw;
}

static inline usize
rl_cursor(const struct readline *r)
{
  return rl_prompt(r) + r->clw;
}

static inline usize
rl_total(const struct readline *r)
{
  return rl_cursor(r) + r->crw;
}

static inline void
rl_vw_set(struct readline *r, usize vw)
{
  assert(vw);
  r->vw = vw;
  if (r->vx >= vw) r->vx = vw - 1;
}

static inline void
rl_pr_set(struct readline *r, cut pr)
{
  r->pr = pr;
  usize lw;
  r->prw = utf8_cols(pr.d, pr.l, &lw);
}

static inline void
rl_cr_set(struct readline *r, cut c)
{
  assert(c.l <= RL_CAP);
  memcpy(r->cr.m + (r->cr.c - c.l), c.d, c.l);
  r->cr.l = c.l;
  usize lw;
  r->crw = utf8_cols(c.d, c.l, &lw);
}

static inline void
rl_cl_sync(struct readline *r)
{
  usize lw;
  r->clw = utf8_cols(r->cl.m, r->cl.l, &lw);
  usize c = rl_cursor(r);
  if (c < r->vw) r->vx = c;
  else r->vx = r->vw - lw;
}

static inline void
rl_init(struct readline *r, usize vw, cut pr)
{
  STR_INIT(&r->cl, RL_MAX,      0, 0);
  STR_INIT(&r->cr, RL_MAX >> 1, 0, 0);
  rl_vw_set(r, vw);
  rl_pr_set(r, pr);
  r->vx = rl_prompt(r);
  r->clw = 0;
  r->crw = 0;
}

static inline const char *
rl_cr_ptr(const struct readline *r)
{
  return r->cr.m + (r->cr.c - r->cr.l);
}

static inline cut
rl_cr_get(const struct readline *r)
{
  return (cut) { rl_cr_ptr(r), r->cr.l };
}

static inline cut
rl_cl_get(const struct readline *r)
{
  return (cut) { r->cl.m, r->cl.l };
}

static inline usize
rl_cl_last(const struct readline *r, u32 *cp, int *w)
{
  usize l = utf8_decode_rev((const unsigned char *)r->cl.m, r->cl.l, cp);
  *w = utf8_width(*cp);
  return l;
}

static inline usize
rl_cr_first(const struct readline *r, u32 *cp, int *w)
{
  const char *p = rl_cr_ptr(r);
  char *n = utf8_decode((void *)p, cp);
  *w = utf8_width(*cp);
  return (usize)(n - p);
}

static inline usize
rl_offset(const struct readline *r)
{
  usize c = rl_cursor(r);
  return c > r->vx ? c - r->vx : 0;
}

static inline int
rl_empty(const struct readline *r)
{
  return !r->cl.l && !r->cr.l;
}

static inline void
rl_clear(struct readline *r)
{
  rl_pr_set(r, CUT_NULL);
  r->cl.l = 0;
  r->cr.l = 0;
  r->clw = 0;
  r->crw = 0;
  r->vx = 0;
}

static inline usize
rl_consume_cl(struct readline *r)
{
  usize w = 0;
  do {
    u32 cp;
    int cw;
    usize l = rl_cl_last(r, &cp, &cw);
    r->cl.l -= l;
    r->clw -= cw;
    w += (usize)cw;
    if (cw != 0) break;
  } while (r->cl.l);
  return w;
}

static inline usize
rl_consume_cr(struct readline *r)
{
  usize w = 0;
  do {
    u32 cp;
    int cw;
    usize l = rl_cr_first(r, &cp, &cw);
    r->cr.l -= l;
    r->crw -= cw;
    w += (usize)cw;
    if (cw != 0 && !r->cr.l) break;
    if (cw != 0) {
      u32 ncp;
      int nw;
      rl_cr_first(r, &ncp, &nw);
      if (nw != 0) break;
    }
  } while (r->cr.l);
  return w;
}

static inline int
rl_take_left_to_right(struct readline *r, usize *wo)
{
  if (!r->cl.l) return 0;
  usize tw = 0;
  for (;;) {
    u32 cp;
    int w;
    usize l = rl_cl_last(r, &cp, &w);
    if (r->cr.l + l > RL_CAP)
      return -1;
    r->cl.l -= l;
    r->clw -= w;
    usize o = r->cr.c - r->cr.l - l;
    memcpy(r->cr.m + o, r->cl.m + r->cl.l, l);
    r->cr.l += l;
    r->crw += w;
    tw += (usize)w;
    if (!r->cl.l || w != 0) break;
  }
  if (wo) *wo = tw;
  return 1;
}

static inline int
rl_take_right_to_left(struct readline *r, usize *wo)
{
  if (!r->cr.l) return 0;
  usize tw = 0;
  for (;;) {
    u32 cp;
    int w;
    usize l = rl_cr_first(r, &cp, &w);
    if (r->cl.l + l > RL_CAP)
      return -1;
    str_copy(&r->cl, rl_cr_ptr(r), l);
    r->cr.l -= l;
    r->clw += w;
    r->crw -= w;
    tw += (usize)w;
    if (!r->cr.l || w != 0) break;
  }
  if (wo) *wo = tw;
  return 1;
}

static inline int
rl_insert(struct readline *r, u32 c, const u8 *b, usize l, usize *n)
{
  if (r->cl.l + l >= RL_CAP) return RL_NONE;
  u8 w = utf8_width(c);
  str_copy(&r->cl, (const char *)b, l);
  r->clw += w;
  if (n) *n = w;
  if (r->vx + w < r->vw) {
    r->vx += w;
    return RL_PARTIAL;
  } else {
    r->vx = r->vw - w;
    return RL_FULL;
  }
}

static inline int
rl_backspace(struct readline *r, usize *n)
{
  if (!r->cl.l) return RL_NONE;
  usize w = rl_consume_cl(r);
  if (n) *n = w;
  usize c = rl_cursor(r);
  if (rl_total(r) < r->vw && r->vx == c + w) {
    r->vx = c;
    return RL_PARTIAL;
  }
  if (r->vx > c) r->vx = c;
  return RL_FULL;
}

static inline int
rl_delete(struct readline *r, usize *n)
{
  if (n) *n = 0;
  if (!r->cr.l) return RL_NONE;
  usize w = rl_consume_cr(r);
  if (n) *n = w;
  return r->vx + r->crw + w < r->vw ? RL_PARTIAL : RL_FULL;
}

static inline int
rl_delete_left(struct readline *r)
{
  if (!r->cl.l) return RL_NONE;
  r->cl.l = 0;
  r->clw = 0;
  r->vx = rl_prompt(r);
  return RL_FULL;
}

static inline int
rl_delete_right(struct readline *r)
{
  if (!r->cr.l) return RL_NONE;
  r->cr.l = 0;
  r->crw = 0;
  return RL_PARTIAL;
}

static inline int
rl_left(struct readline *r, usize *n)
{
  if (n) *n = 0;
  if (!r->cl.l) {
    if (!rl_offset(r)) return RL_NONE;
    r->vx = rl_cursor(r);
    return RL_FULL;
  }
  usize w;
  if (rl_take_left_to_right(r, &w) < 0) return RL_NONE;
  if (n) *n = w;
  if (r->vx >= w && r->vx - w > 0) {
    r->vx -= w;
    return RL_PARTIAL;
  }
  return RL_FULL;
}

static inline int
rl_right(struct readline *r, usize *n)
{
  if (!r->cr.l) return RL_NONE;
  usize w;
  if (rl_take_right_to_left(r, &w) < 0) return RL_NONE;
  if (n) *n = w;
  if (r->vx + w + w <= r->vw) {
    r->vx += w;
    return RL_PARTIAL;
  }
  return RL_FULL;
}

static inline void
rl_join(struct readline *r)
{
  str_copy(&r->cl, rl_cr_ptr(r), r->cr.l);
  str_terminate(&r->cl);
  r->clw += r->crw;
  r->cr.l = 0;
  r->crw = 0;
}

static inline int
rl_home(struct readline *r)
{
  if (!r->cl.l) return RL_NONE;
  if (r->cr.l + r->cl.l > RL_CAP) return RL_NONE;
  usize s = rl_offset(r);
  usize o = r->cr.c - r->cr.l - r->cl.l;
  memcpy(r->cr.m + o, r->cl.m, r->cl.l);
  r->cr.l += r->cl.l;
  r->crw += r->clw;
  r->cl.l = 0;
  r->clw = 0;
  r->vx = rl_prompt(r);
  return s ? RL_FULL : RL_PARTIAL;
}

static inline int
rl_end(struct readline *r)
{
  if (!r->cr.l) return RL_NONE;
  if (r->cl.l + r->cr.l > RL_CAP) return RL_NONE;
  str_copy(&r->cl, rl_cr_ptr(r), r->cr.l);
  r->clw += r->crw;
  r->cr.l = 0;
  r->crw = 0;
  usize c = rl_cursor(r);
  if (c < r->vw) {
    r->vx = c;
    return RL_PARTIAL;
  }
  u32 cp;
  int w;
  rl_cl_last(r, &cp, &w);
  r->vx = r->vw - w;
  return RL_FULL;
}

static inline int
rl_word_left(struct readline *r)
{
  if (!r->cl.l) return RL_NONE;
  u32 cp;
  int w;
  while (r->cl.l) {
    rl_cl_last(r, &cp, &w);
    if (!rl_is_ifs(cp)) break;
    if (rl_left(r, NULL) == RL_NONE) break;
  }
  while (r->cl.l) {
    rl_cl_last(r, &cp, &w);
    if (rl_is_ifs(cp))  break;
    if (rl_left(r, NULL) == RL_NONE) break;
  }
  return RL_FULL;
}

static inline int
rl_word_right(struct readline *r)
{
  if (!r->cr.l) return RL_NONE;
  u32 cp;
  int w;
  while (r->cr.l) {
    rl_cr_first(r, &cp, &w);
    if (!rl_is_ifs(cp))  break;
    if (rl_right(r, NULL) == RL_NONE) break;
  }
  while (r->cr.l) {
    rl_cr_first(r, &cp, &w);
    if (rl_is_ifs(cp))   break;
    if (rl_right(r, NULL) == RL_NONE) break;
  }
  return RL_FULL;
}

static inline int
rl_delete_word_prev(struct readline *r)
{
  if (!r->cl.l) return -1;
  usize d = 0;
  u32 cp;
  int w;
  for (; r->cl.l; d++) {
    rl_cl_last(r, &cp, &w);
    if (!rl_is_ifs(cp))      break;
    if (rl_backspace(r, NULL) == RL_NONE) break;
  }
  for (; r->cl.l; d++) {
    rl_cl_last(r, &cp, &w);
    if (rl_is_ifs(cp))       break;
    if (rl_backspace(r, NULL) == RL_NONE) break;
  }
  return d ? RL_FULL : RL_NONE;
}

static inline int
rl_delete_word_right(struct readline *r)
{
  if (!r->cr.l) return RL_NONE;
  usize d = 0;
  u32 cp;
  int w;
  for (; r->cr.l; d++) {
    rl_cr_first(r, &cp, &w);
    if (!rl_is_ifs(cp))   break;
    if (rl_delete(r, NULL) == RL_NONE) break;
  }
  for (; r->cr.l; d++) {
    rl_cr_first(r, &cp, &w);
    if (rl_is_ifs(cp))    break;
    if (rl_delete(r, NULL) == RL_NONE) break;
  }
  return d ? RL_FULL : RL_NONE;
}

static inline usize
rl_write_seg(str *s, const unsigned char *p, usize l, usize c, usize x, usize e)
{
  const unsigned char *pe = p + l;
  u32 cp;
  while (p < pe) {
    const unsigned char *nc = utf8_decode((void *)p, &cp);
    int w = utf8_width(cp);
    if (c < x && c + w > x)
      str_push_c(s, ' ');
    else if (c + w > x && c + w <= e)
      str_push(s, (const char *)p, (usize)(nc - p));
    c += w;
    if (c >= e) return c;
    p = nc;
  }
  return c;
}

static inline void
rl_write_range(const struct readline *r, str *s, usize x, usize n)
{
  usize c = 0;
  usize e = x + n;
  c = rl_write_seg(s, (const unsigned char *)r->pr.d, r->pr.l, c, x, e);
  if (c >= e) return;
  c = rl_write_seg(s, (const unsigned char *)r->cl.m, r->cl.l, c, x, e);
  if (c >= e) return;
  c = rl_write_seg(s, (const unsigned char *)rl_cr_ptr(r), r->cr.l, c, x, e);
  if (c < e) str_memset(s, ' ', e - c);
}

static inline void
rl_write_visible(const struct readline *r, str *s)
{
  rl_write_range(r, s, rl_offset(r), r->vw);
}

#endif // DYLAN_READLINE

