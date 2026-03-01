/*
 * vim: foldmethod=marker
 *
 *
 *               oooooooooo.   oooooooooooo ooo        ooooo
 *               `888'   `Y8b  `888'     `8 `88.       .888'
 *                888      888  888          888b     d'888
 *                888      888  888oooo8     8 Y88. .P  888
 *                888      888  888    "     8  `888'   888
 *                888     d88'  888          8    Y     888
 *               o888bood8P'   o888o        o8o        o888o
 *
 *                           Dylan's File Manager
 *
 *
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

#include "config.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "lib/arg.h"
#include "lib/bitset.h"
#include "lib/date.h"
#include "lib/readline.h"
#include "lib/str.h"
#include "lib/term.h"
#include "lib/term_key.h"
#include "lib/utf8.h"
#include "lib/util.h"
#include "lib/vt.h"

#if defined(__linux__)
#include "platform/linux.h"
#elif defined(__APPLE__)
#include "platform/apple.h"
#else
#include "platform/posix.h"
#endif

static const char DFM_HELP[] =
  "usage: " CFG_NAME " [options] [path]\n\n"
  "options:\n"
  "-H | +H        toggle hidden files (-H off, +H on)\n"
  "-p             picker mode (print selected path to stdout and exit)\n"
  "-o <opener>    program to use when opening files (default: xdg-open)\n"
  "-s <mode>      change default sort\n"
  "  n  name\n"
  "  N  name reverse\n"
  "  e  extension\n"
  "  s  size\n"
  "  S  size reverse\n"
  "  d  date\n"
  "  D  date reverse\n"
  "-v <mode>      change default view\n"
  "  n  name only\n"
  "  s  size\n"
  "  p  permissions\n"
  "  t  time\n"
  "  a  all\n\n"
  "--help         show this help\n"
  "--version      show version\n\n"
  "path:\n"
  "directory to open (default: \".\")\n\n"
  "environment:\n"
  "DFM_OPENER         program used to open files (overridden by -o)\n"
  "DFM_BOOKMARK_[0-9] bookmark directories\n"
  "DFM_COPYER         program used to copy PWD and file contents.\n"
  "DFM_TRASH          program used to trash files.\n"
  "DFM_TRASH_DIR      path to trash directory.\n"
;

enum fm_opt {
  FM_ERROR        = 1 << 0,
  FM_ROOT         = 1 << 1,

  FM_REDRAW_DIR   = 1 << 2,
  FM_REDRAW_NAV   = 1 << 3,
  FM_REDRAW_CMD   = 1 << 4,
  FM_REDRAW_FLUSH = 1 << 5,
  FM_REDRAW       = FM_REDRAW_DIR|FM_REDRAW_NAV|FM_REDRAW_CMD|FM_REDRAW_FLUSH,

  FM_DIRTY        = 1 << 6,
  FM_DIRTY_WITHIN = 1 << 7,
  FM_HIDDEN       = 1 << 8,
  FM_TRUNC        = 1 << 9,
  FM_MARK_PWD     = 1 << 10,
  FM_MSG          = 1 << 11,
  FM_MSG_ERR      = 1 << 12,
  FM_PICKER       = 1 << 13,
  FM_PRINT_PWD    = 1 << 14,
  FM_SEARCH       = 1 << 15,
};

struct fm;
typedef void (*fm_key_press)(struct fm *, int k, cut, cut);
typedef  int (*fm_key_enter)(struct fm *, str *);
typedef  int (*fm_filter)(struct fm *, usize, cut, cut);

struct fm {
  struct term t;
  struct term_key k;
  struct platform p;
  struct readline r;

  int dfd;
  str pwd;
  str ppwd;
  str mpwd;

  str io;

  usize ml;
  usize mp;

  char de[DFM_ENT_MAX];
  usize del;
  usize dec;

  union {
    align_max _a;
    unsigned char d[DFM_DIR_MAX * sizeof(u32)];
  } d;

  usize dl;
  u8 dv;
  u8 ds;
  u32 du;

  u64 v[BITSET_W(DFM_DIR_MAX)];
  u16 vp[BITSET_W(DFM_DIR_MAX)];
  usize vl;
  char vq[DFM_NAME_MAX];
  usize vql;

  u64 vm[BITSET_W(DFM_DIR_MAX)];
  usize vml;

  u32 ht[DFM_DIR_HT_CAP];

  usize y;
  usize o;
  usize c;
  u32 st;

  u16 row;
  u16 col;

  u32 f;
  u32 cf;

  cut opener;

  fm_key_press kp;
  fm_key_enter kd;
  fm_filter sf;

  s64 tz;
};

// Entry Virtual {{{

#define ENT_V_OFF   0, 20
#define ENT_V_CHAR 20,  8
#define ENT_V_TOMB 28,  1
#define ENT_V_MARK 29,  1
#define ENT_V_VIS  30,  1
#define ENT_V_DOT  31,  1

#define ent_v_get(e, o)     bitfield_get32((e),      ENT_V_##o)
#define ent_v_set(e, o, v)  bitfield_set32((e), (v), ENT_V_##o)
#define ent_v_geto(p, i, o) ent_v_get(ent_v_load((p), (i)), o)

static inline unsigned char *
ent_v_ptr(struct fm *p, usize i)
{
  return p->d.d + (i * sizeof(u32));
}

static inline const unsigned char *
ent_v_ptr_const(const struct fm *p, usize i)
{
  return p->d.d + (i * sizeof(u32));
}

static inline u32
ent_v_load(const struct fm *p, usize i)
{
  u32 v;
  memcpy(&v, ent_v_ptr_const(p, i), sizeof(v));
  return v;
}

static inline void
ent_v_store(struct fm *p, usize i, u32 v)
{
  memcpy(ent_v_ptr(p, i), &v, sizeof(v));
}

// }}}

// Entry Physical {{{

enum {
  ENT_DIR       = 0,
  ENT_LNK_DIR   = 1,
  ENT_LNK       = 3,
  ENT_LNK_BRK   = 5,
  ENT_UNKNOWN   = 4,
  ENT_FIFO      = 6,
  ENT_SOCK      = 8,
  ENT_SPEC      = 10,
  ENT_REG       = 12,
  ENT_REG_EXEC  = 14,
  ENT_TYPE_MAX  = 16,
};

#define ENT_IS_LNK(t) ((t) & 1)
#define ENT_IS_DIR(t) ((t) <= ENT_LNK_DIR)

#define ENT_UTF8  0,  1
#define ENT_WIDE  1,  1
#define ENT_LOC   2, 16
#define ENT_LEN  18,  8
#define ENT_SIZE 26, 12
#define ENT_TYPE 38,  4
#define ENT_PERM 42, 12
#define ENT_TIME 54,  5
#define ENT_HASH 59,  5

#define ent_get(e, o)    bitfield_get64((u64)(e), ENT_##o)
#define ent_set(e, o, v) bitfield_set64((e), (v), ENT_##o)
#define lnk_set(l, o, v) bitfield_set8((l),  (v), ENT_##o)

static inline u64
ent_load(const struct fm *p, usize i)
{
  u64 m;
  memcpy(&m, p->de + ent_v_geto(p, i, OFF) - sizeof(m), sizeof(m));
  return m;
}

static inline void
ent_store(struct fm *p, usize i, u64 m)
{
  memcpy(p->de + ent_v_geto(p, i, OFF) - sizeof(m), &m, 8);
}

static inline u64
ent_load_off(const struct fm *p, u32 o)
{
  u64 m;
  memcpy(&m, p->de + o - sizeof(m), sizeof(m));
  return m;
}

static inline void
ent_perm_decode(str *s, mode_t m, u8 t)
{
  char b[11];
  int d = t ? t == ENT_DIR : S_ISDIR(m);
  b[0]  = d ? 'd' : '-';
  b[1]  = (m & S_IRUSR) ? 'r' : '-';
  b[2]  = (m & S_IWUSR) ? 'w' : '-';
  b[3]  = (m & S_ISUID) ? (m & S_IXUSR) ? 's' : 'S' : (m & S_IXUSR) ? 'x' : '-';
  b[4]  = (m & S_IRGRP) ? 'r' : '-';
  b[5]  = (m & S_IWGRP) ? 'w' : '-';
  b[6]  = (m & S_ISGID) ? (m & S_IXGRP) ? 's' : 'S' : (m & S_IXGRP) ? 'x' : '-';
  b[7]  = (m & S_IROTH) ? 'r' : '-';
  b[8]  = (m & S_IWOTH) ? 'w' : '-';
  b[9]  = (m & S_ISVTX) ? (m & S_IXOTH) ? 't' : 'T' : (m & S_IXOTH) ? 'x' : '-';
  b[10] = ' ';
  str_push(s, b, sizeof(b));
}

static inline u32
ent_size_encode(off_t s)
{
  if (s <= 0) return 0;
  u64 v = (u64)s;
  u32 e = 63 - u64_clz(v);
  u64 b = 1ULL << e;
  u32 f = 0;
  if (e) {
    u64 d = v - b;
    f = (u32)((d << 6) >> e);
    if (f > 63) f = 63;
  }
  return (e << 6) | f;
}

static inline u64
ent_size_bytes(u32 v, u8 t)
{
  if (ENT_IS_LNK(t)) return v;
  if (!v) return 0;
  u32 e = v >> 6;
  u32 f = v & 63;
  u64 b = 1ULL << e;
  return b + ((b * f) >> 6);
}

static inline u32
ent_size_add(u32 e, u64 a)
{
  if (!e) return ent_size_encode(a);
  if (!a) return e;
  u64 c = ent_size_bytes(e, ENT_TYPE_MAX);
  return ent_size_encode(c + a);
}

static inline u32
ent_size_sub(u32 e, u64 s)
{
  if (!e) return 0;
  u64 c = ent_size_bytes(e, ENT_TYPE_MAX);
  if (s >= c) return 0;
  return ent_size_encode(c - s);
}

static inline void
ent_size_decode(str *s, u32 v, usize p, u8 t)
{
  if (ENT_IS_LNK(t) || !v) {
    str_push_u32_p(s, v, ' ', p ? p - 1 : 0);
    str_push_c(s, 'B');
    if (p) str_push_c(s, ' ');
    return;
  }
  u32 e = v >> 6;
  u32 f = v & 63;
  u32 u = e / 10;
  if (u > 6) u = 6;
  u64 b = 1ULL << (e - u * 10);
  u64 ip = b + ((b * f) >> 6);
  u32 d = ((f * 10) + 32) >> 6;
  if (d == 10) { ip++; d = 0; }
  int sd = (u && ip < 10);
  usize su = 1 + (sd ? 2 : 0);
  usize pa = p > su ? p - su : 0;
  str_push_u32_p(s, (u32)ip, ' ', pa);
  if (sd) {
    str_push_c(s, '.');
    str_push_u32(s, d);
  }
  str_push_c(s, "BKMGTPE"[u]);
  if (p) str_push_c(s, ' ');
}

static inline u32
ent_time_encode(time_t t)
{
  time_t d = time(NULL) - t;
  return d <= 0 ? 0 : (u32)(63 - u64_clz(d));
}

static inline void
ent_time_decode(str *s, u32 v)
{
  static const char *u[] = {
    "s ", "s ", "s ", "s ", "s ", "s ",
    "m ", "m ", "m ", "m ", "m ", "m ",
    "h ", "h ", "h ", "h ", "h ",
    "d ", "d ", "d ", "d ", "d ",
    "w ", "w ", "w ", "w ",
    "mo", "mo", "mo", "mo", "mo"
  };
  if (v > 31) v = 31;
  str_push(s, v == 31 ? ">= " : "   ", 3);
  str_push_u32_p(s, v == 31 ? 1u << 5 : 1u << (v % 6), ' ', 2);
  str_push(s, u[v], 2);
  str_push_c(s, ' ');
}

static inline void
ent_map_stat(u64 *e, const struct stat *s, u8 t)
{
  if (t != ENT_TYPE_MAX) goto e;
  if (S_ISDIR(s->st_mode))       t = ENT_DIR;
  else if (S_ISLNK(s->st_mode))  t = ENT_LNK;
  else if (S_ISREG(s->st_mode) && (s->st_mode & (S_IXUSR|S_IXGRP|S_IXOTH)))
    t = ENT_REG_EXEC;
  else if (S_ISREG(s->st_mode))  t = ENT_REG;
  else if (S_ISFIFO(s->st_mode)) t = ENT_FIFO;
  else if (S_ISSOCK(s->st_mode)) t = ENT_SOCK;
  else if (S_ISCHR(s->st_mode) || S_ISBLK(s->st_mode))
    t = ENT_SPEC;
  else                           t = ENT_UNKNOWN;
e:
  ent_set(e, TYPE, t);
  ent_set(e, PERM, s->st_mode & 07777);
  ent_set(e, TIME, ent_time_encode(s->ST_MTIM));
}

static inline void
ent_map_stat_size(u64 *e, const struct stat *s)
{
  ent_set(e, SIZE, S_ISLNK(s->st_mode)
    ? s->st_size : ent_size_encode(s->st_size));
}

static inline usize
ent_name_len(const char *s, u8 *utf8, u8 *wide)
{
  const unsigned char *m = (const unsigned char *)s;
  const unsigned char *p = m;
  *utf8 = 0;
  *wide = 0;
#ifdef __GNUC__
  typedef size_t __attribute__((__may_alias__)) W;
  #define ONES ((size_t)-1 / UCHAR_MAX)
  #define HIGHS (ONES * (UCHAR_MAX / 2 + 1))
  for (; (uintptr_t)p % sizeof(W); p++) {
    if (!*p) return (size)(p - m);
    if (*p & 0x80) { *utf8 = 1; goto check_wide; }
  }
  for (const W *w = (const void *)p;; w++) {
    W v = *w;
    if ((v & HIGHS) | ((v - ONES) & ~v & HIGHS)) {
      p = (const unsigned char *)w;
      for (;; p++) {
        if (!*p) return (size)(p - m);
        if (*p & 0x80) { *utf8 = 1; goto check_wide; }
      }
    }
  }
#endif
  for (;;) {
    unsigned char b = *p;
    if (!b) return (size)(p - m);
    if (!(b & 0x80)) { p++; continue; }
    *utf8 = 1;
check_wide:;
    unsigned char b2 = *p;
    if ((b2 & 0xF8) == 0xF0) { *wide = 1; break; }
    if ((b2 & 0xF0) == 0xE0) {
      u32 cp;
      utf8_decode((void *)p, &cp);
      if (utf8_width(cp) > 1) { *wide = 1; break; }
    }
    usize n = utf8_expected(b2);
    p += n ? n : 1;
  }
  for (p++;;) {
#ifdef __GNUC__
    for (; (uintptr_t)p % sizeof(W); p++) if (!*p) return (size)(p - m);
    for (const W *w = (const void *)p;; w++) {
      if ((*w - ONES) & ~*w & HIGHS) {
        p = (const unsigned char *)w;
        for (; *p; p++);
        return (size)(p - m);
      }
    }
#endif
    if (!*p) return (size)(p - m);
    p++;
  }
}

static inline usize
ent_next(struct fm *p, usize i)
{
  return bitset_next_set(p->v, i, p->dl);
}

static inline usize
ent_prev(struct fm *p, usize i)
{
  return bitset_prev_set(p->v, i, p->dl);
}

// }}}

// Sorting {{{

typedef int (*ent_sort_cb)(struct fm *, u32, u32);

static inline int
fm_ent_cmp_name(struct fm *p, u32 a, u32 b)
{
  static const unsigned char t[256] = {
    ['0']=1,['1']=1,['2']=1,['3']=1,['4']=1,
    ['5']=1,['6']=1,['7']=1,['8']=1,['9']=1
  };

  u32 oa = ent_v_get(a, OFF);
  u32 ob = ent_v_get(b, OFF);
  u64 ma = ent_load_off(p, oa);
  u64 mb = ent_load_off(p, ob);

  int r = ENT_IS_DIR(ent_get(mb, TYPE)) - ENT_IS_DIR(ent_get(ma, TYPE));
  if (unlikely(r)) return r;

  u8 fa = ent_v_get(a, CHAR);
  u8 fb = ent_v_get(b, CHAR);

  int da = (unsigned)(fa - '0') < 10;
  int db = (unsigned)(fb - '0') < 10;
  if (da ^ db)
    return da ? -1 : 1;

  if (fa != fb && !(t[fa] & t[fb]))
    return fa < fb ? -1 : 1;

  const char *pa = p->de + oa;
  const char *pb = p->de + ob;
  usize la = ent_get(ma, LEN);
  usize lb = ent_get(mb, LEN);
  usize i = 0;
  usize j = 0;

  while (i < la && j < lb) {
    unsigned char ca = (unsigned char)pa[i];
    unsigned char cb = (unsigned char)pb[j];

    if (unlikely(t[ca] & t[cb])) {
      usize ia = i;
      usize ja = j;
      for (; ia < la && pa[ia] == '0'; ia++);
      for (; ja < lb && pb[ja] == '0'; ja++);
      usize ea = ia;
      usize eb = ja;
      for (; ea < la && t[(unsigned char)pa[ea]]; ea++);
      for (; eb < lb && t[(unsigned char)pb[eb]]; eb++);
      usize na = ea - ia;
      usize nb = eb - ja;
      if (na != nb) return na < nb ? -1 : 1;
      int cmp = memcmp(pa + ia, pb + ja, na);
      if (cmp) return cmp;
      usize za = ia - i;
      usize zb = ja - j;
      if (za != zb) return za < zb ? -1 : 1;
      i = ea;
      j = eb;
      continue;
    }

    if (ca != cb) return ca < cb ? -1 : 1;
    i++;
    j++;
  }

  return (i < la) - (j < lb);
}

static inline int
fm_ent_cmp_name_rev(struct fm *p, u32 a, u32 b)
{
  return fm_ent_cmp_name(p, a, b) * -1;
}

static inline int
fm_ent_cmp_size(struct fm *p, u32 a, u32 b)
{
  u64 ma = ent_load_off(p, ent_v_get(a, OFF));
  u64 mb = ent_load_off(p, ent_v_get(b, OFF));
  u32 sa = ent_size_bytes(ent_get(ma, SIZE), ent_get(ma, TYPE));
  u32 sb = ent_size_bytes(ent_get(mb, SIZE), ent_get(mb, TYPE));
  return (size)sa - (size)sb;
}

static inline int
fm_ent_cmp_date(struct fm *p, u32 a, u32 b)
{
  u64 ma = ent_load_off(p, ent_v_get(a, OFF));
  u64 mb = ent_load_off(p, ent_v_get(b, OFF));
  return (size)ent_get(ma, TIME) - (size)ent_get(mb, TIME);
}

static inline int
fm_ent_cmp_size_rev(struct fm *p, u32 a, u32 b)
{
  return fm_ent_cmp_size(p, b, a);
}

static inline int
fm_ent_cmp_date_rev(struct fm *p, u32 a, u32 b)
{
  return fm_ent_cmp_date(p, b, a);
}

static inline int
fm_ent_cmp_fext(struct fm *p, u32 a, u32 b)
{
  u32 oa = ent_v_get(a, OFF);
  u32 ob = ent_v_get(b, OFF);
  u64 ma = ent_load_off(p, oa);
  u64 mb = ent_load_off(p, ob);
  cut ca = { p->de + oa, ent_get(ma, LEN) };
  cut cb = { p->de + ob, ent_get(mb, LEN) };
  const char *pa = ca.d + ca.l;
  const char *pb = cb.d + cb.l;
  for (; pa > ca.d && pa[-1] != '.'; pa--);
  for (; pb > cb.d && pb[-1] != '.'; pb--);
  if (pa == ca.d && pb != cb.d) return  1;
  if (pb == cb.d && pa != ca.d) return -1;
  if (pa == ca.d && pb == cb.d) return  0;
  usize la = (usize)(ca.d + ca.l - pa);
  usize lb = (usize)(cb.d + cb.l - pb);
  int r = memcmp(pa, pb, la < lb ? la : lb);
  return r ? r : (int)(la < lb) - (int)(la > lb);
}

static inline void
fm_ent_isort(struct fm *p, ent_sort_cb f, usize lo, usize hi)
{
  for (usize i = lo + 1; i < hi; i++) {
    u32 x = ent_v_load(p, i);
    usize j = i;
    for (; j > lo && f(p, ent_v_load(p, j - 1), x) > 0; j--)
      ent_v_store(p, j, ent_v_load(p, j - 1));
    ent_v_store(p, j, x);
  }
}

static inline void
fm_ent_qsort(struct fm *p, ent_sort_cb f, usize lo, usize hi, int d)
{
  while (hi - lo > 16) {
    if (!d--) break;
    usize mid = lo + ((hi - lo) >> 1);

    u32 a = ent_v_load(p, lo);
    u32 b = ent_v_load(p, mid);
    u32 c = ent_v_load(p, hi - 1);
    u32 pivot = (f(p, a, b) < 0
    ? (f(p, b, c) < 0 ? b : (f(p, a, c) < 0 ? c : a))
    : (f(p, a, c) < 0 ? a : (f(p, b, c) < 0 ? c : b)));

    usize i = lo;
    usize j = hi - 1;

    for (;; i++, j--) {
      for (; f(p, ent_v_load(p, i), pivot) < 0; i++);
      for (; f(p, pivot, ent_v_load(p, j)) < 0; j--);
      if (i >= j) break;
      u32 t = ent_v_load(p, i);
      ent_v_store(p, i, ent_v_load(p, j));
      ent_v_store(p, j, t);
    }

    if (j - lo < hi - (j + 1)) {
      fm_ent_qsort(p, f, lo, j + 1, d);
      lo = j + 1;
    } else {
      fm_ent_qsort(p, f, j + 1, hi, d);
      hi = j + 1;
    }
  }

  fm_ent_isort(p, f, lo, hi);
}

static inline ent_sort_cb
fm_sort_fn(u8 s)
{
  switch (s) {
    case 'n': return fm_ent_cmp_name;
    case 'N': return fm_ent_cmp_name_rev;
    case 'e': return fm_ent_cmp_fext;
    case 's': return fm_ent_cmp_size;
    case 'S': return fm_ent_cmp_size_rev;
    case 'd': return fm_ent_cmp_date;
    case 'D': return fm_ent_cmp_date_rev;
    default:  return 0;
  }
}

// }}}

// Util {{{

static inline cut
fm_ent(const struct fm *p, usize i)
{
  u32 o = ent_v_geto(p, i, OFF);
  u64 m = ent_load_off(p, o);
  return (cut) {p->de + o, ent_get(m, LEN) };
}

static inline cut
fm_file_type(mode_t m)
{
  if (S_ISREG(m) && m & (S_IXUSR|S_IXGRP|S_IXOTH))
    return (cut){ S("executable file") };
  if (S_ISREG(m))  return (cut){ S("regular file") };
  if (S_ISDIR(m))  return (cut){ S("directory") };
  if (S_ISLNK(m))  return (cut){ S("symlink") };
  if (S_ISCHR(m))  return (cut){ S("char device") };
  if (S_ISBLK(m))  return (cut){ S("block device") };
  if (S_ISFIFO(m)) return (cut){ S("fifo") };
  if (S_ISSOCK(m)) return (cut){ S("socket") };
  return (cut){ S("unknown") };
}

static inline void
str_push_time(str *s, s64 tz, time_t ts)
{
  s32 y; u32 m; u32 d;
  u32 H; u32 M; u32 S;
  ut_to_date_time(tz, ts, &y, &m, &d, &H, &M, &S);
  str_push_u32_p(s, y, '0', 2);
  str_push_c(s, '-');
  str_push_u32_p(s, m, '0', 2);
  str_push_c(s, '-');
  str_push_u32_p(s, d, '0', 2);
  str_push_c(s, ' ');
  str_push_u32_p(s, H, '0', 2);
  str_push_c(s, ':');
  str_push_u32_p(s, M, '0', 2);
  str_push_c(s, ':');
  str_push_u32_p(s, S, '0', 2);
}

static inline int
next_tok(const char *s, usize l, usize *c, cut *o)
{
  usize p = *c;
  for (; p < l && (s[p] == ' ' || !s[p]); p++);
  if (p >= l) {
    *c = p;
    *o = (cut){ s, 0 };
    return 0;
  }
  usize t = p;
  for (; p < l && s[p] != ' ' && s[p]; p++);
  *c = p;
  *o = (cut){ &s[t], p - t };
  return 1;
}

// }}}

// Visibility {{{

static inline void
fm_v_clr(struct fm *p, usize i)
{
  if (!ent_v_geto(p, i, VIS)) return;
  u32 e = ent_v_load(p, i);
  ent_v_set(&e, VIS, 0);
  ent_v_store(p, i, e);
}

static inline void
fm_v_assign(struct fm *p, usize i, u8 v)
{
  if (ent_v_geto(p, i, VIS) == v)
    return;
  u32 e = ent_v_load(p, i);
  ent_v_set(&e, VIS, v);
  ent_v_store(p, i, e);
}

static inline void
fm_v_rebuild(struct fm *p)
{
  p->vl = 0;
  u16 s = 0;
  for (usize b = 0, c = BITSET_W(p->dl); b < c; b++) {
    u64 w = 0;
    for (usize j = 0; j < 64; j++) {
      usize i = (b << 6) + j;
      if (i >= p->dl) break;
      if (ent_v_geto(p, i, VIS))
        w |= 1ULL << j;
    }
    p->v[b] = w;
    p->vp[b] = s;
    s += u64_popcount(w);
  }
  p->vl = s;
}

// }}}

// Filtering {{{

static inline usize
fm_filter_pct_rank(struct fm *p, usize idx)
{
  usize b = idx >> 6;
  usize o = idx & 63;
  u64 m = o ? ((1ULL << o) - 1) : 0ULL;
  return (usize)p->vp[b] + u64_popcount(p->v[b] & m);
}

static inline void
fm_filter_apply(struct fm *p, fm_filter f, cut cl, cut cr)
{
  for (usize i = 0; i < p->dl; i++)
    if (ent_v_geto(p, i, TOMB))
      fm_v_clr(p, i);
    else
      fm_v_assign(p, i, f(p, i, cl, cr));
  fm_v_rebuild(p);
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
}

static inline void
fm_filter_apply_inc(struct fm *p, fm_filter f, cut cl, cut cr)
{
  for (usize i = ent_next(p, 0); i != SIZE_MAX; i = ent_next(p, i + 1))
    if (ent_v_geto(p, i, TOMB) || !f(p, i, cl, cr))
      fm_v_clr(p, i);
  fm_v_rebuild(p);
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
}

static int
fm_filter_hidden(struct fm *p, usize i, cut cl, cut cr)
{
  (void)cl;
  (void)cr;
  if (ent_v_geto(p, i, TOMB))
    return 0;
  if (p->f & FM_HIDDEN)
    return 1;
  return !ent_v_geto(p, i, DOT);
}

static inline int
fm_filter_startswith(struct fm *p, usize i, cut cl, cut cr)
{
  usize al = cl.l;
  usize bl = cr.l;
  const char *am = cl.d;
  const char *bm = cr.d;
  u64 m = ent_load(p, i);
  u32 o = ent_v_geto(p, i, OFF);
  cut n = { p->de + o, ent_get(m, LEN) };
  usize w = al + bl;
  if (w > n.l) return 0;
  if (al && (*n.d != *am || (al > 1 && memcmp(n.d + 1, am + 1, al - 1))))
    return 0;
  return !(bl && memcmp(n.d + al, bm, bl));
}

static int
fm_filter_substr(struct fm *p, usize i, cut cl, cut cr)
{
  usize al = cl.l;
  usize bl = cr.l;
  const char *am = cl.d;
  const char *bm = cr.d;
  usize w = al + bl;
  if (!w) return 1;
  u64 m = ent_load(p, i);
  u32 o = ent_v_geto(p, i, OFF);
  cut n = { p->de + o, ent_get(m, LEN) };
  if (w > n.l) return 0;
  for (usize j = 0, x = n.l - w; j <= x; j++) {
    if (al && memcmp(n.d + j, am, al)) continue;
    if (bl && memcmp(n.d + j + al, bm, bl)) continue;
    return 1;
  }
  return 0;
}

//
// TODO: Make this incremental.
//
static inline void
fm_filter_save(struct fm *p, cut cl, cut cr)
{
  usize c = sizeof(p->vq);
  usize i = 0;
  if (cl.l) {
    usize n = MIN(cl.l, c - 1);
    memcpy(p->vq, cl.d, n);
    i += n;
  }
  if (cr.l && i < c - 1) {
    usize n = MIN(cr.l, c - 1 - i);
    memcpy(p->vq + i, cr.d, n);
    i += n;
  }
  p->vq[i] = 0;
  p->vql = i;
}

static inline void
fm_filter_clear(struct fm *p)
{
  fm_filter_apply(p, fm_filter_hidden, CUT_NULL, CUT_NULL);
  p->vql = 0;
  p->f &= ~FM_SEARCH;
}

static inline usize
fm_visible_select(struct fm *p, usize k)
{
  if (k >= p->vl) return SIZE_MAX;
  usize lo = 0;
  usize hi = BITSET_W(p->dl);
  if (!hi) return SIZE_MAX;
  while (lo + 1 < hi) {
    usize mi = lo + ((hi - lo) >> 1);
    if (p->vp[mi] <= k) lo = mi;
    else hi = mi;
  }
  u64 w = p->v[lo];
  usize rank = k - p->vp[lo];
  for (; rank--; w &= w - 1);
  return (lo << 6) + u64_ctz(w);
}

// }}}

// UTF8 Truncation Cache {{{

#define DFM_HT_OCC      0x800u
#define DFM_HT_CACHE    0x40000000u
#define CACHE_HASH(x)   ((u32)((x) & 0x0003F7FFu))
#define CACHE_LEN(x)    ((u16)(((x) >> 18) & 0x0FFFu))
#define CACHE_IS(x)     (((x) & (DFM_HT_CACHE | DFM_HT_OCC)) == DFM_HT_CACHE)
#define CACHE_PACK(h,l) (DFM_HT_CACHE | \
  ((h) & 0x0003F7FFu) | (((u32)(l) & 0x0FFFu) << 18))

static inline u16
fm_cache_hash(const struct fm *p, const char *n, usize l)
{
  u32 h = hash_fnv1a32(n, l);
  u32 m = h;
  m ^= (u32)p->col * 0x9E3779B1u;
  m ^= (u32)p->dv  * 0x85EBCA6Bu;
  m ^= m >> 16;
  return (u16)m;
}

static inline usize
fm_cache_slot(u16 h)
{
  return (h & 0xF7FFu) & (DFM_DIR_HT_CAP - 1);
}

static inline void
fm_dir_ht_clear_cache(struct fm *p)
{
  for (usize i = 0; i < DFM_DIR_HT_CAP; i++)
    if (CACHE_IS(p->ht[i])) p->ht[i] = 0;
}

// }}}

// Directory Lookup {{{

#define DFM_HT_TOMB       0x7FFu
#define DFM_HT_IS_FREE(x) (!((x) & DFM_HT_OCC))

static inline void
fm_dir_ht_hash_split(u32 h, u16 *a, u8 *b)
{
  u32 m = h ^ (h >> 16);
  u16 x = m & 0x07FF;
  *a = x ? x : 1;
  *b = (m >> 11) & 0x1F;
}

static inline usize
fm_dir_ht_find(struct fm *p, cut c, u16 *o)
{
  u32 h = hash_fnv1a32(c.d, c.l);
  u16 a;
  u8 b;
  fm_dir_ht_hash_split(h, &a, &b);
  usize i = h & (DFM_DIR_HT_CAP - 1);
  for (;;) {
    u32 s = p->ht[i];
    if (!s) {
      *o = 0xFFFF;
      return i;
    }
    if ((s & DFM_HT_OCC) && !CACHE_IS(s) && (s & 0x07FF) == a) {
      u64 m = ent_load_off(p, s >> 12);
      if (ent_get(m, HASH) == b) {
        u16 j = (u16)ent_get(m, LOC);
        if (!ent_v_geto(p, j, TOMB) && cut_cmp(fm_ent(p, j), c)) {
          *o = j;
          return i;
        }
      }
    }
    i = (i + 1) & (DFM_DIR_HT_CAP - 1);
  }
}

static inline int
fm_dir_exists(struct fm *p, cut c)
{
  u16 i;
  fm_dir_ht_find(p, c, &i);
  return i != 0xFFFF;
}

static inline usize
fm_dir_ht_find_insert(struct fm *p, u32 h)
{
  usize i = h & (DFM_DIR_HT_CAP - 1);
  usize ft = SIZE_MAX;
  for (;;) {
    u32 s = p->ht[i];
    if (s == DFM_HT_TOMB) {
      if (ft == SIZE_MAX) ft = i;
    } else if (DFM_HT_IS_FREE(s) || CACHE_IS(s))
      return ft != SIZE_MAX ? ft : i;
    i = (i + 1) & (DFM_DIR_HT_CAP - 1);
  }
}

static inline void
fm_dir_ht_insert(struct fm *p, cut c, u16 o, u64 *m)
{
  u32 h = hash_fnv1a32(c.d, c.l);
  u16 a;
  u8 b;
  fm_dir_ht_hash_split(h, &a, &b);
  ent_set(m, HASH, b);
  usize i = fm_dir_ht_find_insert(p, h);
  p->ht[i] = (ent_v_geto(p, o, OFF) << 12) | DFM_HT_OCC | a;
}

static inline void
fm_dir_ht_remove(struct fm *p, usize i)
{
  p->ht[i] = DFM_HT_TOMB;
}

static inline void
fm_dir_ht_clear(struct fm *p)
{
  memset(p->ht, 0, sizeof(p->ht));
}

// }}}

// Draw {{{

static inline void
fm_draw_flush(struct fm *p)
{
  STR_PUSH(&p->io, VT_ESU);
  p->io.f(&p->io, p, 0);
  STR_PUSH(&p->io, VT_BSU);
}

static inline usize
fm_draw_trunc_name(struct fm *p, u64 m, const char *n, usize l, usize c)
{
  if (!c) return 0;
  int w = ent_get(m, WIDE);
  if (l < c) return l;
  int u = ent_get(m, UTF8);
  if (!u) return MIN(l, c);
  if (!w) return utf8_trunc_narrow(n, l, c);
  u16 h = fm_cache_hash(p, n, l);
  usize i = fm_cache_slot(h);
  for (usize j = 0; j < 4; j++) {
    usize s = (i + j) & (DFM_DIR_HT_CAP - 1);
    u32 v = p->ht[s];
    if (CACHE_IS(v) && CACHE_HASH(v) == (h & 0xF7FFu))
      return CACHE_LEN(v) < l ? CACHE_LEN(v) : l;
  }
  usize tl = utf8_trunc_wide(n, l, c);
  for (usize j = 0; j < 4; j++) {
    usize s = (i + j) & (DFM_DIR_HT_CAP - 1);
    u32 v = p->ht[s];
    if (CACHE_IS(v) || !(v & DFM_HT_OCC)) {
      p->ht[s] = CACHE_PACK(h, (u16)tl);
      break;
    }
  }
  return tl;
}

static inline void
fm_draw_ent(struct fm *p, usize n)
{
  u64 e = ent_load(p, n);
  u32 o = ent_v_geto(p, n, OFF);
  u32 t = ent_get(e, TYPE);
  s32 vw = p->col;

  switch (p->dv) {
  case 's': vw -=  7; ent_size_decode(&p->io, ent_get(e, SIZE), 6, t); break;
  case 'p': vw -= 11; ent_perm_decode(&p->io, ent_get(e, PERM), t); break;
  case 't': vw -=  8; ent_time_decode(&p->io, ent_get(e, TIME)); break;
  case 'a':
    vw -= 26;
    ent_perm_decode(&p->io, ent_get(e, PERM), t);
    ent_time_decode(&p->io, ent_get(e, TIME));
    ent_size_decode(&p->io, ent_get(e, SIZE), 6, t);
    break;
  }

  switch (t) {
  case ENT_DIR:      STR_PUSH(&p->io, DFM_COL_DIR);      vw--; break;
  case ENT_FIFO:     STR_PUSH(&p->io, DFM_COL_FIFO);     break;
  case ENT_LNK:      STR_PUSH(&p->io, DFM_COL_LNK);      break;
  case ENT_LNK_BRK:  STR_PUSH(&p->io, DFM_COL_LNK_BRK);  break;
  case ENT_LNK_DIR:  STR_PUSH(&p->io, DFM_COL_LNK_DIR);  break;
  case ENT_REG_EXEC: STR_PUSH(&p->io, DFM_COL_REG_EXEC); vw--; break;
  case ENT_SOCK:     STR_PUSH(&p->io, DFM_COL_SOCK);     break;
  case ENT_SPEC:     STR_PUSH(&p->io, DFM_COL_SPEC);     break;
  case ENT_UNKNOWN:  STR_PUSH(&p->io, DFM_COL_UNKNOWN);  break;
  }

  int m = p->f & FM_MARK_PWD && p->vml && ent_v_geto(p, n, MARK);
  if (m) {
    STR_PUSH(&p->io, DFM_COL_MARK " ");
    vw -= 2;
  }
  if (p->c == n) STR_PUSH(&p->io, DFM_COL_CURSOR);
  usize l = ent_get(e, LEN);
  const char *dn = &p->de[o];
  usize c = fm_draw_trunc_name(p, e, dn, l, vw < 0 ? 0 : vw);
  str_push_sanitize(&p->io, dn, c);

  switch (t) {
  case ENT_LNK_DIR:
  case ENT_DIR:      str_push_c(&p->io, '/'); break;
  case ENT_REG_EXEC: str_push_c(&p->io, '*'); break;
  }

  if (m) str_push_c(&p->io, '*');

  if (ENT_IS_LNK(t)) {
    u8 sl = ent_get(e, SIZE);
    vw -= c + 4;
    if (vw <= 0) goto e;
    STR_PUSH(&p->io, VT_SGR0 " -> ");
    if (sl) {
      dn = &p->de[o + l + 2];
      c = fm_draw_trunc_name(p, dn[-1], dn, sl, vw < 0 ? 0 : vw);
      str_push_sanitize(&p->io, dn, c);
    } else
      str_push_c(&p->io, '?');
  }

e:
  STR_PUSH(&p->io, VT_SGR0 VT_EL0 VT_CR);
}

static inline void
fm_draw_dir(struct fm *p)
{
  usize s = p->y >= p->o ? p->y - p->o : 0;
  usize m = p->vl - s;
  usize d = MIN(m, p->row);
  usize c = fm_visible_select(p, s);
  STR_PUSH(&p->io, VT_CUP1);

  for (usize i = 0; i < d && c != SIZE_MAX; i++) {
    fm_draw_ent(p, c);
    STR_PUSH(&p->io, VT_CUD1);
    c = ent_next(p, c + 1);
  }

  for (usize i = d; i < p->row; i++)
    STR_PUSH(&p->io, VT_EL2 VT_CUD1);
}

static inline void
fm_draw_nav_begin(struct fm *p, cut c)
{
  vt_cup(&p->io, 0, p->row + (DFM_MARGIN - 1));
  str_push(&p->io, c.d, c.l);
  str_memset(&p->io, ' ', p->col);
  STR_PUSH(&p->io, VT_CR);
}

static inline void
fm_draw_nav_end(struct fm *p)
{
  STR_PUSH(&p->io, VT_SGR0);
}

static inline void
fm_draw_inf(struct fm *p)
{
  cut c = p->f & (FM_TRUNC|FM_ERROR) ? CUT(DFM_COL_NAV_ERR) :
    p->f & FM_ROOT ? CUT(DFM_COL_NAV_ROOT) : CUT(DFM_COL_NAV);
  fm_draw_nav_begin(p, c);
  str_push_c(&p->io, ' ');
  str_push_u32(&p->io, p->y + !!p->vl);
  str_push_c(&p->io, '/');
  str_push_u32(&p->io, p->vl);
  STR_PUSH(&p->io, " ");

  str_push_c(&p->io, '[');
  if (unlikely(p->f & FM_ROOT))   str_push_c(&p->io, 'R');
  if (likely(!(p->f & FM_TRUNC))) str_push_c(&p->io, p->ds);
  else str_push_c(&p->io, 'T');
  if (unlikely(p->f & FM_ERROR))  str_push_c(&p->io, 'E');
  if (unlikely(p->f & FM_HIDDEN)) str_push_c(&p->io, 'H');
  STR_PUSH(&p->io, "] ");

  if (p->vml) {
    STR_PUSH(&p->io, DFM_COL_NAV_MARK " ");
    str_push_u32(&p->io, p->vml);
    STR_PUSH(&p->io, " marked " VT_SGR0);
    str_push(&p->io, c.d, c.l);
    str_push_c(&p->io, ' ');
  }

  if (likely(!(p->f & FM_TRUNC))) {
    STR_PUSH(&p->io, "~");
    ent_size_decode(&p->io, p->du, 0, ENT_TYPE_MAX);
    STR_PUSH(&p->io, " ");
  }

  str_push_sanitize(&p->io, p->pwd.m, MIN(p->pwd.l, p->col));

  if (p->f & FM_SEARCH) {
    STR_PUSH(&p->io, "/" VT_SGR(1));
    if (p->sf == fm_filter_substr) str_push_c(&p->io, '*');
    str_push(&p->io, p->vq, p->vql);
    STR_PUSH(&p->io, "*" VT_SGR0);
  }

  fm_draw_nav_end(p);
}

static inline void
fm_draw_msg(struct fm *p, const char *s, usize l)
{
  p->f |= FM_MSG|FM_REDRAW_NAV;
  rl_clear(&p->r);
  str_push(&p->r.cl, s, l);
}

static inline void
fm_draw_err(struct fm *p, const char *s, usize l, int e)
{
  p->f |= FM_MSG_ERR|FM_REDRAW_NAV;
  rl_clear(&p->r);
  STR_PUSH(&p->r.cl, " error: ");
  str_push(&p->r.cl, s, l);
  if (!e) return;
  STR_PUSH(&p->r.cl, ": ");
  str_push_s(&p->r.cl, strerror(e));
}

static inline void
fm_draw_cmd(struct fm *p)
{
  vt_cup(&p->io, 0, p->row + DFM_MARGIN);
  rl_write_visible(&p->r, &p->io);
  STR_PUSH(&p->io, VT_EL0);
}

static inline void
fm_draw_buf(struct fm *p, cut c)
{
  fm_draw_nav_begin(p, c);
  str_push(&p->io, p->r.cl.m, p->r.cl.l);
  fm_draw_nav_end(p);
}

static inline void
fm_draw_nav(struct fm *p)
{
  if (p->f & (FM_MSG|FM_MSG_ERR)) {
    fm_draw_buf(p, p->f & FM_MSG ? CUT(DFM_COL_NAV_MSG) : CUT(DFM_COL_NAV_ERR));
    rl_clear(&p->r);
    p->f &= ~(FM_MSG|FM_MSG_ERR);
  } else
    fm_draw_inf(p);
}

// }}}

// Cursor {{{

static inline void
fm_cursor_set(struct fm *p, usize y, usize o)
{
  if (!p->vl || !p->row) {
    p->y = 0;
    p->o = 0;
    p->c = ent_next(p, 0);
    return;
  }
  if (y >= p->vl) y = p->vl - 1;
  if (o >= p->row) o = p->row - 1;
  if (o > y) o = y;
  p->y = y;
  p->o = o;
  p->c = fm_visible_select(p, y);
}

static inline void
fm_scroll_to(struct fm *p, cut d)
{
  if (!p->vl) goto e;
  u16 i;
  fm_dir_ht_find(p, d, &i);
  if (i == 0xFFFF || !ent_v_geto(p, i, VIS))
    goto e;
  usize r = fm_filter_pct_rank(p, i);
  usize ms = p->vl > p->row ? p->vl - p->row : 0;
  usize h = p->row >> 1;
  usize s = r <= p->row - 2 ? 0 : r >= ms ? ms : r > h ? r - h : 0;
  if (s > ms) s = ms;
  fm_cursor_set(p, r, r - s);
  return;
e:
  fm_cursor_set(p, 0, 0);
}

static inline size
fm_scroll_to_rank(struct fm *p, usize r)
{
  size dy = (size)r - (size)p->y;
  if (!dy || !p->vl) return 0;
  if (dy > (size)p->row || dy < -(size)p->row) {
    size h = (size)p->row >> 1;
    size j = (size)r - (dy > 0 ? h : -h);
    if (j < 0) j = 0;
    if (j >= (size)p->vl) j = (size)p->vl - 1;
    fm_cursor_set(p, (usize)j, 0);
    p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
    dy = (size)r - (size)p->y;
  }
  return dy;
}

static inline void
fm_cursor_sync(struct fm *p)
{
  if (!p->vl || !p->row) {
    p->y = 0;
    p->o = 0;
    p->c = SIZE_MAX;
    return;
  }
  if (p->y >= p->vl) p->y = p->vl - 1;
  if (p->o >= p->row) p->o = p->row - 1;
  if (p->o > p->y) p->o = p->y;
  p->c = fm_visible_select(p, p->y);
}

// }}}

// Terminal {{{

static inline int
fm_term_resize(struct fm *p)
{
  if (term_size_update(&p->t, &p->row, &p->col) < 0)
    return -1;
  p->row = p->row > DFM_MARGIN ? p->row - DFM_MARGIN : 1;
  rl_vw_set(&p->r, p->col);
  vt_decstbm(&p->io, 1, p->row);
  fm_cursor_set(p, p->y, p->o);
  p->f |= FM_REDRAW;
  return 0;
}

static inline int
fm_term_raw(struct fm *p)
{
  STR_PUSH(&p->io,
    VT_ALT_SCREEN_Y VT_DECTCEM_N VT_DECAWM_N VT_BPASTE_ON VT_ED2 VT_CUP1);
  return term_raw(&p->t) < 0 ? -1 : fm_term_resize(p);
}

static inline int
fm_term_cooked(struct fm *p)
{
  vt_decstbm(&p->io, 1, p->row + DFM_MARGIN);
  STR_PUSH(&p->io,
    VT_SGR0 VT_BPASTE_OFF VT_DECAWM_Y VT_DECTCEM_Y VT_ALT_SCREEN_N);
  fm_draw_flush(p);
  return term_cooked(&p->t);
}

static inline int
fm_term_init(struct fm *p)
{
  int r = term_init(&p->t);
  return fm_term_raw(p) < 0 ? -1 : r < 0 ? -1 : fm_term_resize(p);
}

static inline int
fm_term_free(struct fm *p)
{
  int r = fm_term_cooked(p);
  term_destroy(&p->t);
  return r;
}

// }}}

// Entry Marking {{{

static inline void *
fm_mark_slot(struct fm *p, usize i)
{
  return p->d.d + i * sizeof(char *);
}

static inline char *
fm_mark_load(struct fm *p, usize i)
{
  char *v;
  memcpy(&v, fm_mark_slot(p, p->mp + i), sizeof(v));
  return v;
}

static inline void
fm_mark_store(struct fm *p, usize i, char *v)
{
  memcpy(fm_mark_slot(p, p->mp + i), &v, sizeof(v));
}

static inline int
fm_mark_has_room(const struct fm *p)
{
  return p->mp * sizeof(char *) >
   (p->dl + DFM_MARK_CMD_PRE) * sizeof(u32) + sizeof(char *);
}

static inline u8
fm_mark_len(const char *p)
{
  return (u8)p[-1];
}

static inline cut
fm_mark_at(struct fm *p, usize i)
{
  char *m = fm_mark_load(p, i);
  return (cut) { m, fm_mark_len(m) };
}

static inline void
fm_mark_terminate(struct fm *p)
{
  fm_mark_store(p, p->ml, NULL);
}

static inline void
fm_mark_write_at(struct fm *p, usize i, char *v)
{
  fm_mark_store(p, i, v);
}

static inline void
fm_mark_write_newest(struct fm *p, char *v)
{
  p->mp--;
  p->ml++;
  fm_mark_store(p, 0, v);
  fm_mark_terminate(p);
}

static inline void
fm_mark_clear_ptr(struct fm *p)
{
#define DIR_PTR_CAP (DFM_DIR_MAX / (sizeof(char *) / sizeof(u32)))
  p->mp = DIR_PTR_CAP - DFM_MARK_CMD_PRE - DFM_MARK_CMD_POST;
}

static inline void
fm_mark_clear_range(struct fm *p, usize lo, usize hi)
{
  if (hi <= lo) return;
  usize b0 = lo >> 6;
  usize b1 = (hi - 1) >> 6;
  for (usize b = b0; b <= b1; b++) {
    u64 m = ~0ULL;
    if (b == b0) {
      u64 m0 = (lo & 63) ? ((1ULL << (lo & 63)) - 1ULL) : 0ULL;
      m &= ~m0;
    }
    if (b == b1) {
      u64 end = ((hi - 1) & 63);
      u64 m1 = (end == 63) ? ~0ULL : ((1ULL << (end + 1)) - 1ULL);
      m &= m1;
    }
    u64 tc = p->vm[b] & p->v[b] & m;
    if (!tc) continue;
    u64 w = tc;
    while (w) {
      usize i = (b << 6) + u64_ctz(w);
      w &= w - 1;
      if (i >= p->dl) break;
      u32 x = ent_v_load(p, i);
      ent_v_set(&x, MARK, 0);
      ent_v_store(p, i, x);
    }
    p->vm[b] &= ~m;
    p->vml -= u64_popcount(tc);
  }
}

static inline void
fm_mark_clear_all(struct fm *p)
{
  p->ml = 0;
  p->vml = 0;
  memset(p->vm, 0, sizeof(p->vm));
  fm_mark_clear_ptr(p);
  fm_mark_terminate(p);
  for (usize i = 0; i < p->dl; i++) {
    u32 e = ent_v_load(p, i);
    ent_v_set(&e, MARK, 0);
    ent_v_store(p, i, e);
  }
  p->dec = sizeof(p->de);
}

static inline int
fm_mark_push(struct fm *p, cut c)
{
  usize n = c.l + 4;
  if (unlikely(!fm_mark_has_room(p) || p->dec < p->del + n))
    return 0;
  p->dec -= n;
  char *b = p->de + p->dec;
  u16 h = (u16) hash_fnv1a32(c.d, c.l);
  b[0] = (unsigned char)(h & 0xff);
  b[1] = (unsigned char)(h >> 8);
  b[2] = (unsigned char)c.l;
  memcpy(b + 3, c.d, c.l);
  b[c.l + 3] = 0;
  fm_mark_write_newest(p, b + 3);
  p->f |= FM_MARK_PWD;
  return 1;
}

static inline void
fm_mark_drop_idx(struct fm *p, usize i)
{
  if (!p->ml) return;
  if (i != p->ml - 1)
    fm_mark_write_at(p, i, fm_mark_load(p, p->ml - 1));
  p->ml--;
  fm_mark_terminate(p);
}

static inline usize
fm_mark_find(struct fm *p, usize c, int d)
{
  usize n = SIZE_MAX;
  usize nw = BITSET_W(p->dl);
  for (usize b = 0; b < nw; b++) {
    for (u64 w = p->vm[b] & p->v[b]; w; ) {
      usize j = (b << 6) + u64_ctz(w);
      w &= w - 1;
      if (d > 0) {
        if (j > c && (n == SIZE_MAX || j < n))
          n = j;
      } else {
        if (j < c && (n == SIZE_MAX || j > n))
          n = j;
      }
    }
  }
  return n;
}

static inline void
fm_mark_apply_bitset(struct fm *p, const u64 *s)
{
  usize nw = BITSET_W(p->dl);
  for (usize b = 0; b < nw; b++) {
    u64 w = s[b];
    while (w) {
      usize i = (b << 6) + u64_ctz(w);
      w &= w - 1;
      if (i >= p->dl) break;
      u32 x = ent_v_load(p, i);
      ent_v_set(&x, MARK, 1);
      ent_v_store(p, i, x);
    }
  }
}

static inline void
fm_mark_invalidate(struct fm *p)
{
  p->ml = 0;
  fm_mark_clear_ptr(p);
  fm_mark_terminate(p);
  p->dec = sizeof(p->de);
}

static inline usize
fm_mark_materialize_range(struct fm *p, usize *x)
{
  if (!p->vml) return 0;
  if (!p->mpwd.l) return 0;
  usize n = 0;
  usize i = *x;
  usize nw = BITSET_W(p->dl);
  for (usize b = i >> 6; b < nw; b++) {
    u64 w = p->vm[b] & p->v[b];
    if (b == (i >> 6))
      w &= ~((1ULL << (i & 63)) - 1ULL);
    while (w) {
      usize bit = (b << 6) + u64_ctz(w);
      w &= w - 1;
      if (bit >= p->dl) continue;
      cut c = fm_ent(p, bit);
      usize cl = c.l + 4;
      if (!fm_mark_has_room(p) || p->dec < p->del + cl) {
        *x = n ? bit : i;
        return n;
      }
      if (!fm_mark_push(p, c)) {
        *x = n ? bit : i;
        return n;
      }
      n++;
      *x = bit + 1;
    }
  }
  *x = p->dl;
  return n;
}

static inline int
fm_mark_materialize(struct fm *p)
{
  if (!p->vml)    return 0;
  if (p->ml)      return 0;
  if (!p->mpwd.l) return 0;
  if (!str_cmp(&p->mpwd, &p->pwd)) return 0;
  fm_mark_invalidate(p);
  usize oml  = p->ml;
  usize omp  = p->mp;
  usize odec = p->dec;
  usize i = 0;
  usize n = fm_mark_materialize_range(p, &i);
  if (n != p->vml) {
    p->ml  = oml;
    p->mp  = omp;
    p->dec = odec;
    return -1;
  }
  return 0;
}

static inline void
fm_mark_clear_idx(struct fm *p, usize i)
{
  if (!ent_v_geto(p, i, MARK))
    return;
  u32 x = ent_v_load(p, i);
  ent_v_set(&x, MARK, 0);
  ent_v_store(p, i, x);
  usize b = i >> 6;
  u64 bit = 1ULL << (i & 63);
  p->vm[b] &= ~bit;
  p->vml--;
}

static inline void
fm_mark_pop_first(struct fm *p)
{
  if (!p->ml) return;
  cut m = fm_mark_at(p, 0);
  u16 j;
  fm_dir_ht_find(p, m, &j);
  if (j != 0xFFFF)
    fm_mark_clear_idx(p, j);
  fm_mark_drop_idx(p, 0);
}

static inline void
fm_mark_clear(struct fm *p)
{
  fm_mark_clear_all(p);
  p->mpwd.l = 0;
  p->f &= ~FM_MARK_PWD;
}

static inline void
fm_mark_init(struct fm *p)
{
  p->mpwd.l = 0;
  str_push(&p->mpwd, p->pwd.m, p->pwd.l);
  str_terminate(&p->mpwd);
  p->f |= FM_MARK_PWD;
}

static inline u32
fm_mark_toggle_idx(struct fm *p, usize i)
{
  u8 s = ent_v_geto(p, i, MARK);
  u32 x = ent_v_load(p, i);
  ent_v_set(&x, MARK, !s);
  ent_v_store(p, i, x);
  usize b = i >> 6;
  u64 bit = 1ULL << (i & 63);
  if (s) {
    p->vm[b] &= ~bit;
    p->vml--;
  } else {
    p->vm[b] |= bit;
    p->vml++;
  }
  if (p->ml) fm_mark_invalidate(p);
  return 1;
}

// }}}

// Filesystem {{{

static inline int
fm_dir_has_room(const struct fm *p, usize e)
{
  return (p->dl + e) * sizeof(u32) <=
    p->mp * sizeof(char *) - DFM_MARK_CMD_PRE * sizeof(char *);
}

static inline void
fm_dir_rebuild_loc(struct fm *p)
{
  for (usize i = 0; i < p->dl; i++) {
    u64 m = ent_load(p, i);
    ent_set(&m, LOC, (u16)i);
    ent_store(p, i, m);
  }
}

static inline void
fm_dir_sort(struct fm *p)
{
  if (likely(!(p->f & FM_TRUNC))) {
    fm_ent_qsort(p, fm_sort_fn(p->ds), 0, p->dl, 32);
    fm_dir_rebuild_loc(p);
  }
  fm_filter f = rl_empty(&p->r) ? fm_filter_hidden : p->sf;
  fm_filter_apply(p, f, rl_cl_get(&p->r), rl_cr_get(&p->r));
  fm_cursor_set(p, p->y, p->o);
}

static inline void
fm_dir_mark_rebuild(struct fm *p)
{
  if (!p->ml || !(p->f & FM_MARK_PWD))
    return;
  memset(p->vm, 0, sizeof(p->vm));
  p->vml = 0;
  for (usize i = 0; i < p->dl; i++) {
    u32 x = ent_v_load(p, i);
    ent_v_set(&x, MARK, 0);
    ent_v_store(p, i, x);
  }
  for (usize i = 0; i < p->ml; i++) {
    cut m = fm_mark_at(p, i);
    u16 j;
    fm_dir_ht_find(p, m, &j);
    if (j != 0xFFFF) {
      u32 x = ent_v_load(p, j);
      ent_v_set(&x, MARK, 1);
      ent_v_store(p, j, x);
      p->vm[j >> 6] |= 1ULL << (j & 63);
      p->vml++;
      u64 md = ent_load(p, j);
      ent_store(p, j, md);
    }
  }
}

static inline void
fm_dir_clear(struct fm *p)
{
  p->y = 0;
  p->o = 0;
  p->c = 0;
  p->f &= ~FM_TRUNC;
  rl_clear(&p->r);
  p->del = 0;
  p->dl = 0;
  p->du = 0;
  p->st = 0;
  fm_dir_ht_clear(p);
}

static inline int
fm_dir_load_ent(struct fm *p, const char *s)
{
  if (s[0] == '.' && (s[1] == '\0' || (s[1] == '.' &&  s[2] == '\0')))
    return 0;
  if (unlikely(!fm_dir_has_room(p, 1)))
    return -1;

  u8 utf8;
  u8 wide;
  u8 l = (u8)ent_name_len(s, &utf8, &wide);

  if (unlikely(p->del + sizeof(u64) + l + 1 >= p->dec))
    return -1;

  u64 m = 0;
  u32 o = p->del;
  u32 x = 0;
  ent_v_set(&x, OFF, o + sizeof(m));
  ent_v_set(&x, CHAR, s[0]);
  ent_v_set(&x, DOT, s[0] == '.');
  ent_v_store(p, p->dl, x);
  ent_set(&m, LEN, l);
  ent_set(&m, LOC, (u16)p->dl);
  ent_set(&m, UTF8, utf8);
  ent_set(&m, WIDE, wide);

  memcpy(p->de + p->del + sizeof(m), s, l + 1);
  p->del += sizeof(m) + l + 1;
  p->dl++;

  struct stat st;
  if (unlikely(fstatat(p->dfd, s, &st, AT_SYMLINK_NOFOLLOW) == -1)) {
    ent_set(&m, TYPE, ENT_UNKNOWN);
    ent_set(&m, SIZE, 0);
    ent_set(&m, TIME, 0);
    ent_set(&m, PERM, 0);
    goto t;
  }

  if (S_ISLNK(st.st_mode)) {
    struct stat ts;
    if (fstatat(p->dfd, s, &ts, 0) == -1)
      ent_map_stat(&m, &st, ENT_LNK_BRK);
    else
      ent_map_stat(&m, &ts, S_ISDIR(ts.st_mode) ? ENT_LNK_DIR : ENT_LNK);

    usize ll = (usize)st.st_size;
    if (p->del + ll + 2 < p->dec) {
      char *lm = p->de + p->del + 1;
      ssize_t r = readlinkat(p->dfd, s, lm, st.st_size);
      if (likely(r != -1)) {
        u8 lu;
        u8 lw;
        ent_name_len(lm, &lu, &lw);
        u8 f = 0;
        lnk_set(&f, UTF8, lu);
        lnk_set(&f, WIDE, lw);
        lm[-1] = f;
        lm[ll] = 0;
        p->del += ll + 2;
      } else {
        ent_set(&m, SIZE, 0);
        goto t;
      }
    }
    goto w;
  }

  ent_map_stat(&m, &st, ENT_TYPE_MAX);
w:
  ent_map_stat_size(&m, &st);
  u64 sz = ent_size_bytes(ent_get(m, SIZE), ent_get(m, TYPE));
  p->du = ent_size_add(p->du, sz);
t:
  fm_dir_ht_insert(p, (cut){ s, l }, (u16)(p->dl - 1), &m);
  memcpy(p->de + o, &m, sizeof(m));
  return 0;
}

static inline int
fm_dir_load(struct fm *p)
{
  int d = openat(p->dfd, ".", O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  if (d < 0) return 0;
  DIR *n = fdopendir(d);
  if (unlikely(!n)) { close(d); return 0; }
  fm_dir_clear(p);

  for (struct dirent *e; (e = readdir(n)); )
    if (fm_dir_load_ent(p, e->d_name) == -1) {
      p->f |= FM_TRUNC;
      break;
    }

  closedir(n);
  fm_dir_sort(p);
  fm_dir_mark_rebuild(p);
  fs_watch(&p->p, ".");
  return 1;
}

static inline int
fm_dir_add(struct fm *p, cut c)
{
  if (fm_dir_exists(p, c))
    return 0;
  if (fm_dir_load_ent(p, c.d) == -1)
    return -1;
  int h = !(p->f & FM_HIDDEN) && *c.d == '.';
  fm_v_assign(p, p->dl - 1, !h);
  p->f |= FM_DIRTY;
  p->st = ent_v_geto(p, p->dl - 1, OFF);
  return 0;
}

static inline int
fm_dir_del(struct fm *p, cut c)
{
  u16 f;
  usize s = fm_dir_ht_find(p, c, &f);
  if (f == 0xFFFF) return -1;

  u64 m = ent_load(p, f);
  u64 sz = ent_size_bytes(ent_get(m, SIZE), ent_get(m, TYPE));
  p->du = ent_size_sub(p->du, sz);

  u32 x = ent_v_load(p, f);
  ent_v_set(&x, TOMB, 1);
  ent_v_set(&x, MARK, 0);
  ent_v_store(p, f, x);

  fm_dir_ht_remove(p, s);
  p->f |= FM_DIRTY;
  return 0;
}

static inline void
fm_dir_refresh(struct fm *p)
{
  cut o = p->c == SIZE_MAX ? CUT_NULL : fm_ent(p, p->c);
  fm_dir_load(p);
  fm_scroll_to(p, o);
  fm_cursor_sync(p);
  p->f |= FM_DIRTY;
}

// }}}

// Core {{{

static inline int
fm_path_change(struct fm *p)
{
  fm_filter_clear(p);
  if (fm_mark_materialize(p) < 0) {
    fm_draw_err(p,
      S("Not enough memory to materialize marks, unmark to cd"), 0);
    return 0;
  }
  return 1;
}

static inline int
fm_path_open(struct fm *p)
{
  int fd = open(p->pwd.m, O_DIRECTORY|O_CLOEXEC);
  if (fd == -1) return 0;
  if (p->dfd != AT_FDCWD) close(p->dfd);
  p->dfd = fd;
  p->f ^= (-(p->mpwd.l && str_cmp(&p->mpwd, &p->pwd)) ^ p->f) & FM_MARK_PWD;
  return fchdir(fd) != -1;
}

static inline void
fm_path_save(struct fm *p)
{
  p->ppwd.l = 0;
  str_push(&p->ppwd, p->pwd.m, p->pwd.l);
}

static inline void
fm_path_load(struct fm *p)
{
  p->pwd.l = 0;
  str_push(&p->pwd, p->ppwd.m, p->ppwd.l);
}

static inline int
fm_path_cd(struct fm *p, const char *d, usize l)
{
  if (!fm_path_change(p)) return 0;
  fm_path_save(p);
  p->pwd.l = 0;
  str_push(&p->pwd, d, l);
  str_terminate(&p->pwd);
  usize nl = path_resolve(p->pwd.m, p->pwd.l);
  p->pwd.l = nl;
  int r = fm_path_open(p);
  if (!r) {
    fm_path_load(p);
    fm_draw_err(p, S("cd"), errno);
  }
  return r && fm_dir_load(p);
}

static inline int
fm_path_chdir(struct fm *p, const char *d)
{
  if (!fm_path_change(p)) return 0;
  fm_path_save(p);
  p->pwd.l = 0;
  str_push(&p->pwd, d, strlen(d));
  str_terminate(&p->pwd);
  int r = fm_path_open(p);
  if (!r || !fm_dir_load(p)) {
    fm_path_load(p);
    fm_draw_err(p, S("cd"), errno);
    return 0;
  }
  fm_path_save(p);
  if (!getcwd(p->pwd.m, p->pwd.c)) {
    fm_path_load(p);
    fm_draw_err(p, S("cd"), errno);
    return 0;
  }
  p->pwd.l = strlen(p->pwd.m);
  str_terminate(&p->pwd);
  return 1;
}

static inline int
fm_path_cd_relative(struct fm *p, const char *d, u8 l)
{
  if (!fm_path_change(p)) return 0;
  fm_path_save(p);
  if (p->pwd.l > 1) str_push_c(&p->pwd, '/');
  str_push(&p->pwd, d, l);
  str_terminate(&p->pwd);
  usize nl = path_resolve(p->pwd.m, p->pwd.l);
  p->pwd.l = nl;
  int r = fm_path_open(p);
  if (!r) {
    fm_path_load(p);
    fm_draw_err(p, S("cd"), errno);
  }
  return r && fm_dir_load(p);
}

static inline cut
fm_path_cd_up(struct fm *p)
{
  if (!fm_path_change(p)) return CUT_NULL;
  fm_path_save(p);
  usize l = p->pwd.l;
  usize i = l;
  for (; i > 1 && p->pwd.m[i - 1] != '/'; i--);
  usize n = (i > 1) ? i - 1 : 1;
  char s = p->pwd.m[n];
  p->pwd.m[n] = 0;
  p->pwd.l = n;
  int r = fm_path_open(p);
  if (!r) {
    p->pwd.m[n] = s;
    p->pwd.l = l;
    fm_draw_err(p, S("cd"), errno);
  }
  return r && fm_dir_load(p) ? (cut){ p->ppwd.m + i, l - i } : CUT_NULL;
}

static inline int
fm_exec(struct fm *p, int in, const char *d, const char *const a[], bool bg, bool tf)
{
  if (tf) fm_term_cooked(p);
  int r = run_cmd(bg ? p->t.null : p->t.fd, in, d, a, bg);
  if (tf) fm_term_raw(p);
  if (r == -1) {
    fm_draw_err(p, S("exec"), errno);
    return -1;
  }
  if (WIFEXITED(r)) {
    int ec = WEXITSTATUS(r);
    if (ec == 127) {
      fm_draw_err(p, S("exec: command not found"), 0);
      return -1;
    } else if (ec) {
      fm_draw_err(p, S("exec: exited non-zero"), 0);
      return -1;
    }
  }
  if (WIFSIGNALED(r)) {
    fm_draw_err(p, S("exec: killed by signal"), 0);
    return -1;
  }
  return 0;
}

static inline void
fm_open(struct fm *p)
{
  if (p->c == SIZE_MAX) return;
  cut c = fm_ent(p, p->c);
  if (!c.l) return;
  u64 m = ent_load(p, p->c);
  if (ENT_IS_DIR(ent_get(m, TYPE)))
    fm_path_cd_relative(p, c.d, c.l);
  else if (unlikely(p->f & FM_PICKER)) {
    str_push_c(&p->pwd, '/');
    str_push(&p->pwd, c.d, c.l);
    term_set_dead(&p->t, 1);
  } else {
    const char *const a[] = { p->opener.d, c.d, NULL };
    fm_exec(p, -1, NULL, a, 0, 1);
  }
}

// }}}

// Command {{{

struct fm_cmd {
  cut prompt;
  cut left;
  cut right;
  fm_key_press press;
  fm_key_enter enter;
  u32 config;
};

#define FM_CMD(C, ...)                                                         \
static inline void                                                             \
C(struct fm *p)                                                                \
{                                                                              \
  fm_cmd(p, &(struct fm_cmd){__VA_ARGS__});                                    \
}

enum {
  CMD_BG           = 1 << 0,
  CMD_CONFLICT     = 1 << 1,
  CMD_MUT          = 1 << 2,
  CMD_EXEC         = 1 << 3,
  CMD_MARK_DIR     = 1 << 4,
  CMD_NOT_MARK_DIR = 1 << 5,
  CMD_STDIN        = 1 << 6,
  CMD_FILE_CURSOR  = 1 << 7,
  CMD_EXEC_MARK    = 1 << 8,
  CMD_EXEC_ROOT    = 1 << 9,

  CMD_MODE_EACH    = 0,
  CMD_MODE_VIRTUAL,
  CMD_MODE_CHUNK,
  CMD_MODE_BULK,
  CMD_MODE_SINGLE,
};

static inline void
fm_cmd_exec(struct fm *p)
{
  if (p->kd && p->kd(p, &p->r.cl) >= 0)
    rl_clear(&p->r);
  p->r.vx = 0;
  p->r.pr.l = 0;
  p->kp = 0;
  p->kd = 0;
}

static inline void
fm_cmd(struct fm *p, struct fm_cmd *c)
{
  if (!c->press && !c->enter) {
    fm_draw_err(p, S("no callbacks defined"), 0);
    return;
  }
  rl_clear(&p->r);
  rl_pr_set(&p->r, c->prompt);
  if (c->left.l) {
    str_push(&p->r.cl, c->left.d, c->left.l);
    str_terminate(&p->r.cl);
  }
  if (c->right.l) rl_cr_set(&p->r, c->right);
  if (c->config & CMD_FILE_CURSOR) {
    if (p->c == SIZE_MAX) return;
    cut e = fm_ent(p, p->c);
    str_push(&p->r.cl, e.d, e.l);
  }
  rl_cl_sync(&p->r);
  p->cf = c->config;
  p->kp = c->press;
  p->kd = c->enter;
  p->f |= FM_REDRAW_CMD;
  if (p->f & FM_ROOT && !(p->cf & CMD_EXEC_ROOT))
    return;
  if ((p->cf & CMD_EXEC_MARK && p->vml) || p->cf & CMD_EXEC) {
    rl_join(&p->r);
    fm_cmd_exec(p);
  }
}

static inline void
fm_cmd_search_press(struct fm *p, int k, cut cl, cut cr)
{
  if (cl.l > 1 && k != KEY_BACKSPACE && p->vl != p->dl && !cr.l) {
    fm_filter_apply_inc(p, p->sf, cl, cr);
  } else
    fm_filter_apply(p, p->sf, cl, cr);
  fm_filter_save(p, cl, cr);
  fm_cursor_set(p, 0, 0);
}

static inline int
fm_cmd_search(struct fm *p, str *s)
{
  if (p->vl == 1) fm_open(p);
  else {
    if (s->l) {
      cut q = (cut){ s->m, s->l };
      fm_filter_apply(p, p->sf, q, CUT_NULL);
      fm_filter_save(p, q, CUT_NULL);
    }
    else
      fm_filter_apply(p, fm_filter_hidden, (cut){ s->m, s->l }, CUT_NULL);
    fm_cursor_set(p, 0, 0);
  }
  return -1;
}

static inline int
fm_cmd_cd(struct fm *p, str *s)
{
  int r = 0;
  if (s->m[0] == '/')
    r = fm_path_cd(p, s->m, s->l);
  else
    r = fm_path_cd_relative(p, s->m, s->l);
  return r ? 0 : -1;
}

static u8
fm_prompt_conflict(struct fm *p, cut d)
{
  fm_draw_nav_begin(p, CUT(DFM_COL_NAV_ERR));
  STR_PUSH(&p->io, "conflict: '");
  str_push(&p->io, d.d, d.l);
  STR_PUSH(&p->io, "': try overwrite?");
  STR_PUSH(&p->io, " [a]bort [y]es [Y]es all [n]o [N]o all");
  fm_draw_nav_end(p);
  fm_draw_flush(p);
  for (;;) {
    if (!term_key_read(p->t.fd, &p->k))
      return 'a';
    switch (p->k.b[0]) {
    case 'a': case 'y': case 'Y': case 'n': case 'N':
      return p->k.b[0];
    }
  }
}

static inline int
fm_prepare_marks_conflict(struct fm *p)
{
  usize i = 0;
  cut m = CUT_NULL;
  int om = 0;
  if (!p->ml) {
    m = fm_ent(p, p->c);
    goto c;
  }
  for (; i < p->ml; ) {
    m = fm_mark_at(p, i);
c:
    if (!fm_dir_exists(p, m)) goto s;
    if (om != 'Y' && om != 'N')
      om = fm_prompt_conflict(p, m);
    switch (om) {
    case 'a': return -1;
    case 'y': case 'Y': goto s;
    case 'n': fm_mark_drop_idx(p, i); om = -2; continue;
    case 'N': p->ml = 0; return -1;
    }
s:
    i++;
  }
  return om;
}

static inline int
fm_cmd_build_bulk_exec(struct fm *p, cut s, usize ti, usize tc, u32 f)
{
  usize omi = ti;
  usize prn = ti;
  usize pon = (tc - omi - 1);
  usize tt  = prn + p->ml + pon;
  usize pri = p->mp - prn;
  usize poi = p->mp + p->ml;
  char **m = (char **)(void *)p->d.d;
  cut c;
  for (usize i = 0, n = 0; next_tok(s.d, s.l, &n, &c); i++) {
    if (i == omi) {
      if (p->ml) continue;
      cut e = fm_ent(p, p->c);
      c = e;
    } else if (c.l == 2 && c.d[0] == '%' && c.d[1] == 'd')
      c = (cut) { p->pwd.m, p->pwd.l };
    else if (c.l > 1 && c.d[0] == '$') {
      c.d = getenv(c.d + 1);
      if (!c.d) return -2;
    }
    m[i < ti ? pri++ : poi++] = (char *)c.d;
  }
  char **a = &m[p->mp - prn];
  a[tt] = NULL;
  const char *wd = p->mpwd.m;
  return fm_exec(p, -1, wd, (const char *const *)a, f & CMD_BG, !(f & CMD_BG));
}

static inline int
fm_cmd_build_bulk_chunk(struct fm *p, cut s, usize ti, usize tc, u32 f)
{
  int r = 0;
  usize b = 0;
  while (b < p->dl && p->vml) {
    fm_mark_invalidate(p);
    usize pb = b;
    usize n = fm_mark_materialize_range(p, &b);
    if (!n) break;
    r = fm_cmd_build_bulk_exec(p, s, ti, tc, f);
    if (r < 0) return r;
    fm_mark_clear_range(p, pb, b);
  }
  if (!p->vml) fm_mark_clear_all(p);
  return r;
}

static inline int
fm_cmd_build_bulk(struct fm *p, cut s, usize ti, usize tc, u32 f)
{
  if (fm_mark_materialize(p) < 0) {
    fm_draw_err(p, S("Not enough memory to materialize marks"), 0);
    return -1;
  }
  if (fm_cmd_build_bulk_exec(p, s, ti, tc, f) < 0)
    return -1;
  fm_mark_clear_all(p);
  return 0;
}

static inline int
fm_cmd_build(struct fm *p, cut s, usize ti, usize tc, u32 f, usize mp, cut mk, bool t)
{
  char **m = (char **)(void *)p->d.d;
  usize pri = mp;
  cut c;
  for (usize j = 0, n = 0; next_tok(s.d, s.l, &n, &c); j++)
    if (j == ti)
      m[pri++] = (char *)mk.d;
    else if (c.l == 2 && c.d[0] == '%' && c.d[1] == 'd')
      m[pri++] = p->pwd.m;
    else if (c.l > 1 && c.d[0] == '$') {
      char *e = getenv(c.d + 1);
      if (!e) return -2;
      m[pri++] = e;
    } else
      m[pri++] = (char *)c.d;
  u32 lf = f;
  m[mp + tc] = NULL;
  int fd = -1;
  if (lf & CMD_STDIN) {
    fd = open(mk.d, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return -1;
  }
  int r = fm_exec(p, fd, p->pwd.m, (const char *const *)&m[mp], lf & CMD_BG, t);
  if (fd >= 0) close(fd);
  return r;
}

static inline int
fm_cmd_build_each_virtual(struct fm *p, cut s, usize ti, usize tc, u32 f)
{
  cut mk = CUT_NULL;
  usize mp = p->mp - (tc + 1);
  if (!p->vml) {
    if (p->c == SIZE_MAX) return 0;
    mk = fm_ent(p, p->c);
    return fm_cmd_build(p, s, ti, tc, f, mp, mk, 1);
  }
  if (!(f & CMD_BG)) fm_term_cooked(p);
  int r = 0;
  usize nw = BITSET_W(p->dl);
  for (usize b = 0; b < nw; b++) {
    for (u64 w = p->vm[b] & p->v[b]; w; ) {
      usize i = (b << 6) + u64_ctz(w);
      w &= w - 1;
      if (i >= p->dl) break;
      mk = fm_ent(p, i);
      if (fm_cmd_build(p, s, ti, tc, f, mp, mk, 0) < 0) {
        r = -1;
        goto e;
      }
      fm_mark_clear_idx(p, i);
      if (!p->vml) goto e;
    }
  }
e:
  if (!(f & CMD_BG)) fm_term_raw(p);
  return r;
}

static inline int
fm_cmd_build_each(struct fm *p, cut s, usize ti, usize tc, u32 f)
{
  cut mk = CUT_NULL;
  usize mp = p->mp - (tc + 1);
  if (!p->vml) {
    if (p->c == SIZE_MAX) return 0;
    mk = fm_ent(p, p->c);
    return fm_cmd_build(p, s, ti, tc, f, mp, mk, 1);
  }
  if (!(f & CMD_BG)) fm_term_cooked(p);
  for (; p->ml; ) {
    mk = fm_mark_at(p, 0);
    str fp = p->mpwd;
    str_push_c(&fp, '/');
    str_push(&fp, mk.d, mk.l);
    str_terminate(&fp);
    mk = (cut) { fp.m, fp.l };
    if (fm_cmd_build(p, s, ti, tc, f, mp, mk, 0) < 0) {
      if (!(f & CMD_BG)) fm_term_raw(p);
      return -1;
    }
    fm_mark_pop_first(p);
    if (p->vml) p->vml--;
  }
  if (!(f & CMD_BG)) fm_term_raw(p);
  return 0;
}

static inline int
fm_cmd_sh(struct fm *p, cut c)
{
  if (!c.l) return 0;
  cut f = !p->ml && p->c != SIZE_MAX ? fm_ent(p, p->c) : CUT_NULL;
  cut sh = get_env("SHELL", "/bin/sh");
  const char *cmd[] = { sh.d, DFM_SHELL_OPTS, c.d, CFG_NAME, f.d, NULL };
  STATIC_ASSERT(ARR_SIZE(cmd) < DFM_MARK_CMD_PRE, "");
  usize l = ARR_SIZE(cmd) - (!f.l * 2);
  char **m = (char **)(void *)p->d.d;
  char **a = &m[p->mp - l];
  memcpy(a, cmd, sizeof(*cmd) * l);
  if (!f.l) a[l + p->ml] = NULL;
  int r =
    fm_exec(p, -1, f.l ? p->pwd.m : p->mpwd.m, (const char *const *)a, 0, 1);
  if (!f.l) fm_mark_clear_all(p);
  if (r < 0) p->f |= FM_ERROR;
  return r;
}

static inline int
fm_cmd_run_sh(struct fm *p, str *s)
{
  if (fm_mark_materialize(p) < 0) {
    fm_draw_err(p, S("Not enough memory to materialize marks"), 0);
    return -1;
  }
  usize e = s->m[0] == '!';
  return fm_cmd_sh(p, (cut){ s->m + e, s->l - e });
}

static inline cut
fm_cmd_parse(struct fm *p, str *s, usize *oti, usize *ott, usize *otc)
{
  cut c;
  usize tc = 0;
  usize ti = SIZE_MAX;
  usize tt = 0;
  usize li = 0;
  cut lt = CUT_NULL;
  for (usize i = 0, n = 0; next_tok(s->m, s->l, &n, &c); i++, tc++) {
    ((char *)c.d)[c.l] = 0;
    lt = c;
    li = i;
    if (ti == SIZE_MAX && c.l == 2 && c.d[0] == '%') {
      if (c.d[1] == 'm' || c.d[1] == 'f' ) {
        ti = i;
        tt = c.d[1];
      }
    }
  }
  if (tc && lt.l == 1 && lt.d[0] == '&') {
    p->cf |= CMD_BG;
    tc--;
    if (ti != SIZE_MAX && li < ti) ti--;
  }
  *oti = ti;
  *ott = tt;
  *otc = tc;
  c = (cut){ s->m, s->l };
  switch (s->m[0]) {
    case '<': c.d++; c.l--; p->cf |= CMD_STDIN; break;
  }
  return c;
}

static inline int
fm_cmd_run(struct fm *p, str *s)
{
  int m = CMD_MODE_SINGLE;
  int r = 0;
  if (!s->l) return 0;
  if (p->cf & CMD_MARK_DIR && !(p->f & FM_MARK_PWD) && p->vml) {
    fm_draw_err(p, S("Not in mark directory"), 0);
    return -1;
  }
  if (p->cf & CMD_NOT_MARK_DIR && p->f & FM_MARK_PWD) {
    fm_draw_err(p, S("In mark directory"), 0);
    return -1;
  }
  if (s->m[0] == '!')
    return fm_cmd_run_sh(p, s);
  usize ti;
  usize tt;
  usize tc;
  cut a = fm_cmd_parse(p, s, &ti, &tt, &tc);
  if ((tt && (!p->vml && !p->vl))) {
    fm_draw_err(p, S("nothing to operate on"), 0);
    return -1;
  }
  if (p->cf & (CMD_STDIN|CMD_FILE_CURSOR))
    m = CMD_MODE_SINGLE;
  else if (tt == 'm') {
    m = p->f & FM_MARK_PWD ? CMD_MODE_CHUNK : CMD_MODE_BULK;
    m = p->vml ? m : CMD_MODE_EACH;
  } else if (tt == 'f')
    m = p->f & FM_MARK_PWD ? CMD_MODE_VIRTUAL : CMD_MODE_EACH;
  switch (m) {
  case CMD_MODE_SINGLE:
  case CMD_MODE_EACH:
  case CMD_MODE_VIRTUAL:
    if (tc > DFM_MARK_CMD_PRE) {
      fm_draw_err(p, S("argv too large"), 0);
      return -1;
    }
    break;
  case CMD_MODE_BULK:
  case CMD_MODE_CHUNK:
    if (ti > DFM_MARK_CMD_PRE || tc - ti - 1 > DFM_MARK_CMD_POST) {
      fm_draw_err(p, S("argv too large"), 0);
      return -1;
    }
  }
  switch (m) {
  case CMD_MODE_EACH:
  case CMD_MODE_BULK:
    if (p->cf & CMD_CONFLICT) {
      r = fm_prepare_marks_conflict(p);
      if (r < 0) p->f |= FM_REDRAW_NAV;
      if (r == -1) return 0;
      if (r == -2 && !p->ml) return 0;
    }
    break;
  }
  cut mk = CUT_NULL;
  switch (m) {
  case CMD_MODE_SINGLE:
    if (p->c != SIZE_MAX)
      mk = fm_ent(p, p->c);
    r = fm_cmd_build(p, a, ti, tc, p->cf, p->mp - (tc + 1), mk, 1);
    break;
  case CMD_MODE_EACH:
    r = fm_cmd_build_each(p, a, ti, tc, p->cf);
    break;
  case CMD_MODE_VIRTUAL:
    r = fm_cmd_build_each_virtual(p, a, ti, tc, p->cf);
    break;
  case CMD_MODE_BULK:
    r = fm_cmd_build_bulk(p, a, ti, tc, p->cf);
    break;
  case CMD_MODE_CHUNK:
    r = fm_cmd_build_bulk_chunk(p, a, ti, tc, p->cf);
    break;
  }
#ifdef FS_WATCH
  if (r != -1 && p->cf & CMD_MUT)
    p->f |= FM_DIRTY_WITHIN;
#else
  if (r != -1 && (p->cf & (CMD_MUT)))
    fm_dir_refresh(p);
#endif
  if (r == -2)
    fm_draw_err(p, S("environment variable unset"), 0);
  if (r < 0) p->f |= FM_ERROR;
  return r;
}

// }}}

// Action {{{

static inline void
act_quit(struct fm *p)
{
  term_set_dead(&p->t, 1);
}

// static inline void
// act_crash(struct fm *p)
// {
//   (void) p;
//   *(volatile int *)0 = 1;
// }

static inline void
act_quit_print_pwd(struct fm *p)
{
  p->f |= FM_PRINT_PWD;
  act_quit(p);
}

static inline void
act_cd_home(struct fm *p)
{
  cut h = get_env("HOME", "");
  if (!h.l) return;
  fm_path_cd(p, h.d, h.l);
}

static inline void
act_cd_mark_directory(struct fm *p)
{
  if (!p->vml) return;
  fm_path_cd(p, p->mpwd.m, p->mpwd.l);
}

static inline void
act_cd_trash(struct fm *p)
{
  cut e = get_env("DFM_TRASH_DIR", DFM_TRASH_DIR);
  if (e.l) fm_path_cd(p, e.d, e.l);
  else fm_draw_err(p, S("DFM_TRASH_DIR not set"), 0);
}

static inline void
act_cd_last(struct fm *p)
{
  fm_path_cd(p, p->ppwd.m, p->ppwd.l);
}

static inline void
act_copy_pwd(struct fm *p)
{
  int fd = fd_from_buf(p->pwd.m, p->pwd.l);
  if (fd < 0)
    fm_draw_err(p, S("PWD too large"), errno);
  else {
    const char *const a[] = { get_env("DFM_COPYER", DFM_COPYER).d, NULL };
    fm_exec(p, fd, NULL, a, 1, 0);
    close(fd);
    fm_draw_msg(p, S("Copied PWD to clipboard"));
  }
}

#define ACT_CD_BOOKMARK(N)                                                     \
static inline void                                                             \
act_cd_bookmark_##N(struct fm *p)                                              \
{                                                                              \
  cut e = get_env("DFM_BOOKMARK_" #N, DFM_BOOKMARK_##N);                       \
  if (e.l) fm_path_cd(p, e.d, e.l);                                            \
  else fm_draw_err(p, S("DFM_BOOKMARK_" #N " not set"), 0);                    \
}
ACT_CD_BOOKMARK(0) ACT_CD_BOOKMARK(1) ACT_CD_BOOKMARK(2) ACT_CD_BOOKMARK(3)
ACT_CD_BOOKMARK(4) ACT_CD_BOOKMARK(5) ACT_CD_BOOKMARK(6) ACT_CD_BOOKMARK(7)
ACT_CD_BOOKMARK(8) ACT_CD_BOOKMARK(9)

static inline void
act_cd_up(struct fm *p)
{
  if (p->f & FM_SEARCH) {
    rl_clear(&p->r);
    fm_filter_clear(p);
    if (p->c == SIZE_MAX) {
      fm_cursor_set(p, 0, 0);
      return;
    }
    usize o = ent_next(p, 0);
    if (o == SIZE_MAX) return;
    cut c = fm_ent(p, o);
    fm_scroll_to(p, c);
    p->c = o;
    return;
  }

  cut b = fm_path_cd_up(p);
  if (!b.l) return;
  fm_scroll_to(p, b);
  fm_cursor_sync(p);
}

static inline void
act_stat(struct fm *p)
{
  if (p->c == SIZE_MAX) return;
  cut e = fm_ent(p, p->c);

  struct stat st;
  if (fstatat(p->dfd, e.d, &st, AT_SYMLINK_NOFOLLOW) == -1) {
    fm_draw_err(p, S("stat"), errno);
    return;
  }

  STR_PUSH(&p->io, VT_ED2 VT_CUP1);

  STR_PUSH(&p->io, "Name:   ");
  str_push(&p->io, e.d, e.l);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "Type:   ");
  cut t = fm_file_type(st.st_mode);
  str_push(&p->io, t.d, t.l);
  STR_PUSH(&p->io, VT_CR VT_LF);

  if (S_ISLNK(st.st_mode)) {
    char b[PATH_MAX];
    ssize_t r = readlinkat(p->dfd, e.d, b, sizeof(b) - 1);
    if (r >= 0) {
      b[r] = 0;
      STR_PUSH(&p->io, "Target: ");
      str_push_s(&p->io, b);
      STR_PUSH(&p->io, VT_CR VT_LF);
    }
  }

  STR_PUSH(&p->io, "Size:   ");
  str_push_u64(&p->io, (u64)st.st_size);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "Mode:   0");
  str_push_u32_b(&p->io, st.st_mode & 07777, 8, 0, 0);
  STR_PUSH(&p->io, ", ");
  ent_perm_decode(&p->io, st.st_mode, 0);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "UID:    ");
  str_push_u32(&p->io, (u32)st.st_uid);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "GID:    ");
  str_push_u32(&p->io, (u32)st.st_gid);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "Links:  ");
  str_push_u64(&p->io, (u64)st.st_nlink);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "Blocks: ");
  str_push_u64(&p->io, (u64)st.st_blocks);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "Inode:  ");
  str_push_u64(&p->io, (u64)st.st_ino);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "Device: ");
  str_push_u64(&p->io, (u64)st.st_dev);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "Access: ");
  str_push_time(&p->io, p->tz, st.ST_ATIM);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "Modify: ");
  str_push_time(&p->io, p->tz, st.ST_MTIM);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, "Change: ");
  str_push_time(&p->io, p->tz, st.ST_CTIM);
  STR_PUSH(&p->io, VT_CR VT_LF);

  STR_PUSH(&p->io, VT_CR VT_LF "Press any key...");

  fm_draw_flush(p);
  term_key_read(p->t.fd, &p->k);
  p->f |= FM_REDRAW;
}

static inline void
act_open(struct fm *p)
{
  fm_open(p);
}

static inline void
act_view_next(struct fm *p)
{
  switch (p->dv) {
    default:  p->dv = 's'; break;
    case 's': p->dv = 'p'; break;
    case 'p': p->dv = 't'; break;
    case 't': p->dv = 'a'; break;
    case 'a': p->dv = 'n'; break;
  }
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
}

static inline void
act_sort_next(struct fm *p)
{
  switch (p->ds) {
    default:  p->ds = 'N'; break;
    case 'N': p->ds = 's'; break;
    case 's': p->ds = 'S'; break;
    case 'S': p->ds = 'd'; break;
    case 'd': p->ds = 'D'; break;
    case 'D': p->ds = 'e'; break;
    case 'e': p->ds = 'n'; break;
  }
  fm_dir_sort(p);
}

static inline void
act_redraw(struct fm *p)
{
  p->f |= FM_REDRAW;
}

static inline void
act_refresh(struct fm *p)
{
  fm_dir_refresh(p);
}

static inline void
act_scroll_top(struct fm *p)
{
  fm_cursor_set(p, 0, 0);
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
}

static inline void
act_scroll_bottom(struct fm *p)
{
  fm_cursor_set(p, p->vl - !!p->vl, p->row -1);
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
}

static inline void
act_page_down(struct fm *p)
{
  if (!p->vl) return;
  usize ny = p->y + p->row;
  if (ny >= p->vl) ny = p->vl - 1;
  fm_cursor_set(p, ny, p->row - 1);
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
}

static inline void
act_page_up(struct fm *p)
{
  if (!p->vl) return;
  usize ny = (p->y > p->row) ? (p->y - p->row) : 0;
  fm_cursor_set(p, ny, 0);
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
}

static inline void
act_scroll_down(struct fm *p)
{
  if (unlikely(p->y + 1 >= p->vl)) return;
  usize l = p->c;
  p->y++;
  p->o += p->o < p->row - 1;
  usize n = ent_next(p, p->c + 1);
  if (n == SIZE_MAX) return;
  p->c = n;
  fm_draw_ent(p, l);
  STR_PUSH(&p->io, VT_LF);
  fm_draw_ent(p, p->c);
  p->f |= FM_REDRAW_NAV;
}

static inline void
act_scroll_up(struct fm *p)
{
  if (unlikely(!p->y)) return;
  usize l = p->c;
  p->y--;
  usize n = ent_prev(p, p->c - 1);
  if (n == SIZE_MAX) return;
  p->c = n;
  fm_draw_ent(p, l);

  if (!p->o) {
    STR_PUSH(&p->io, VT_IL0);
  } else {
    p->o -= !!p->o;
    STR_PUSH(&p->io, VT_CUU1);
  }

  fm_draw_ent(p, p->c);
  p->f |= FM_REDRAW_NAV;
}

static inline void
act_toggle_hidden(struct fm *p)
{
  if (p->c == SIZE_MAX) return;
  cut c = fm_ent(p, p->c);
  if (!c.l) return;
  p->f ^= FM_HIDDEN;
  fm_filter_clear(p);
  fm_scroll_to(p, c);
  fm_cursor_sync(p);
}

static inline void
act_search_startswith(struct fm *p)
{
  p->sf = fm_filter_startswith;
  fm_filter_clear(p);
  p->f |= FM_SEARCH;
  fm_cursor_set(p, 0, 0);
  fm_cmd(p, &(struct fm_cmd){
    .prompt = CUT("/"),
    .press  = fm_cmd_search_press,
    .enter  = fm_cmd_search,
  });
}

static inline void
act_search_substring(struct fm *p)
{
  p->sf = fm_filter_substr;
  fm_filter_clear(p);
  p->f |= FM_SEARCH;
  fm_cursor_set(p, 0, 0);
  fm_cmd(p, &(struct fm_cmd){
    .prompt = CUT("/*"),
    .press  = fm_cmd_search_press,
    .enter  = fm_cmd_search,
  });
}

static inline void
act_shell(struct fm *p)
{
  cut sh = get_env("SHELL", "/bin/sh");
  const char *const a[] = { sh.d, NULL };
  fm_exec(p, -1, NULL, a, 0, 1);
}

static inline void
act_alt_buffer(struct fm *p)
{
  STR_PUSH(&p->io, VT_ALT_SCREEN_N);
  fm_draw_flush(p);
  term_key_read(p->t.fd, &p->k);
  STR_PUSH(&p->io, VT_ALT_SCREEN_Y);
  fm_draw_flush(p);
  p->f &= ~FM_ERROR;
  p->f |= FM_REDRAW;
}

static inline void
act_mark_toggle(struct fm *p)
{
  if (p->c == SIZE_MAX) return;
  if (!(p->f & FM_MARK_PWD)) fm_mark_clear(p);
  fm_mark_init(p);
  if (!fm_mark_toggle_idx(p, p->c)) {
    fm_draw_err(p, S("Not enough memory to mark"), 0);
    return;
  }
  fm_mark_invalidate(p);
  fm_draw_ent(p, p->c);
  p->f |= FM_REDRAW_NAV;
}

static inline void
act_mark_toggle_all(struct fm *p)
{
  usize i = ent_next(p, 0);
  if (i == SIZE_MAX) return;
  int pr = ent_v_geto(p, i, MARK);
  fm_mark_clear(p);
  if (pr) goto e;
  fm_mark_init(p);
  p->vml = 0;
  usize nw = BITSET_W(p->dl);
  for (usize b = 0; b < nw; b++) {
    p->vm[b] = p->v[b];
    p->vml  += u64_popcount(p->vm[b]);
  }
  fm_mark_apply_bitset(p, p->vm);
  p->ml = 0;
e:
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
}

static inline void
act_mark_clear(struct fm *p)
{
  fm_mark_clear(p);
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
}

static inline void
act_mark_next(struct fm *p)
{
  if (!p->vml || p->c == SIZE_MAX) return;
  usize b = fm_mark_find(p, p->c, 1);
  if (b == SIZE_MAX) return;
  usize r = fm_filter_pct_rank(p, b);
  size y = fm_scroll_to_rank(p, r);
  while (y-- > 0) act_scroll_down(p);
}

static inline void
act_mark_prev(struct fm *p)
{
  if (!p->vml || p->c == SIZE_MAX) return;
  usize b = fm_mark_find(p, p->c, -1);
  if (b == SIZE_MAX) return;
  usize r = fm_filter_pct_rank(p, b);
  size y = fm_scroll_to_rank(p, r);
  while (y++ < 0) act_scroll_up(p);
}

static inline void
act_mark_invert(struct fm *p)
{
  if (!p->vl) return;
  if (!(p->f & FM_MARK_PWD)) {
    fm_mark_clear(p);
    fm_mark_init(p);
  }
  p->vml = 0;
  usize nw = BITSET_W(p->dl);
  for (usize b = 0; b < nw; b++) {
    p->vm[b] = p->v[b] & ~p->vm[b];
    p->vml += u64_popcount(p->vm[b]);
  }
  fm_mark_apply_bitset(p, p->vm);
  for (usize b = 0; b < nw; b++) {
    u64 cl = p->v[b] & ~p->vm[b];
    while (cl) {
      usize i = (b << 6) + u64_ctz(cl);
      cl &= cl - 1;
      if (i >= p->dl) break;
      u32 x = ent_v_load(p, i);
      ent_v_set(&x, MARK, 0);
      ent_v_store(p, i, x);
    }
  }
  fm_mark_invalidate(p);
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
}

// }}}

// Input {{{

static inline void
input_disabled(struct fm *p)
{
  (void) p;
}

static inline void
input_move_beginning(struct fm *p)
{
  switch (rl_home(&p->r)) {
  case RL_FULL:
    p->f |= FM_REDRAW_CMD;
    break;
  case RL_PARTIAL:
    STR_PUSH(&p->io, VT_CR);
    p->f |= FM_REDRAW_FLUSH;
    break;
  }
}

static inline void
input_move_end(struct fm *p)
{
  switch (rl_end(&p->r)) {
  case RL_FULL:
    p->f |= FM_REDRAW_CMD;
    break;
  case RL_PARTIAL:
    STR_PUSH(&p->io, VT_CR);
    vt_cuf(&p->io, p->r.vx);
    p->f |= FM_REDRAW_FLUSH;
    break;
  }
}

static inline void
input_move_left(struct fm *p)
{
  usize n;
  switch (rl_left(&p->r, &n)) {
  case RL_FULL:
    p->f |= FM_REDRAW_CMD;
    break;
  case RL_PARTIAL:
    vt_cub(&p->io, n);
    p->f |= FM_REDRAW_FLUSH;
    break;
  }
}

static inline void
input_move_word_left(struct fm *p)
{
  if (rl_word_left(&p->r) != -1)
    p->f |= FM_REDRAW_CMD;
}

static inline void
input_move_word_right(struct fm *p)
{
  if (rl_word_right(&p->r) != -1)
    p->f |= FM_REDRAW_CMD;
}

static inline void
input_move_right(struct fm *p)
{
  usize n;
  switch (rl_right(&p->r, &n)) {
  case RL_FULL:
    p->f |= FM_REDRAW_CMD;
    break;
  case RL_PARTIAL:
    vt_cuf(&p->io, n);
    p->f |= FM_REDRAW_FLUSH;
    break;
  }
}

static inline void
input_delete_to_end(struct fm *p)
{
  int r = rl_delete_right(&p->r);
  if (r == RL_NONE) return;
  STR_PUSH(&p->io, VT_EL0);
  p->f |= FM_REDRAW_FLUSH;
  if (p->kp) p->kp(p, 0, rl_cl_get(&p->r), rl_cr_get(&p->r));
}

static inline void
input_delete_to_beginning(struct fm *p)
{
  int r = rl_delete_left(&p->r);
  if (r == RL_NONE) return;
  p->f |= FM_REDRAW_CMD;
  if (p->kp) p->kp(p, 0, rl_cl_get(&p->r), rl_cr_get(&p->r));
}

static inline void
input_delete(struct fm *p)
{
  usize n;
  switch (rl_delete(&p->r, &n)) {
  case RL_FULL:
    p->f |= FM_REDRAW_CMD;
    break;
  case RL_PARTIAL:
    vt_dch(&p->io, n);
    p->f |= FM_REDRAW_FLUSH;
    break;
  case RL_NONE: return;
  }
  if (p->kp) p->kp(p, 0, rl_cl_get(&p->r), rl_cr_get(&p->r));
}

static inline void
input_delete_word_left(struct fm *p)
{
  int r = rl_delete_word_prev(&p->r);
  if (r == RL_NONE) return;
  p->f |= FM_REDRAW_CMD;
  if (p->kp) p->kp(p, 0, rl_cl_get(&p->r), rl_cr_get(&p->r));
}

static inline void
input_delete_word_right(struct fm *p)
{
  int r = rl_delete_word_right(&p->r);
  if (r == RL_NONE) return;
  p->f |= FM_REDRAW_CMD;
  if (p->kp) p->kp(p, 0, rl_cl_get(&p->r), rl_cr_get(&p->r));
}

static inline void
input_backspace(struct fm *p)
{
  usize n;
  switch (rl_backspace(&p->r, &n)) {
  case RL_FULL:
    p->f |= FM_REDRAW_CMD;
    break;
  case RL_PARTIAL:
    vt_cub(&p->io, n);
    vt_dch(&p->io, n);
    p->f |= FM_REDRAW_FLUSH;
    break;
  case RL_NONE: return;
  }
  if (p->kp) p->kp(p, 0, rl_cl_get(&p->r), rl_cr_get(&p->r));
}

static inline void
input_cancel(struct fm *p)
{
  rl_clear(&p->r);
  p->kp = 0;
  p->kd = 0;
  STR_PUSH(&p->io, VT_EL2);
  p->f |= FM_REDRAW_NAV;
}

static inline void
input_submit(struct fm *p)
{
  rl_join(&p->r);
  fm_cmd_exec(p);
  p->r.vx = 0;
  STR_PUSH(&p->io, VT_EL2);
  p->f |= FM_REDRAW_NAV;
}

static inline void
input_insert(struct fm *p)
{
  assert(!(p->k.c & KEY_TAG));
  usize n;
  switch (rl_insert(&p->r, p->k.c, p->k.b, p->k.l, &n)) {
  case RL_FULL:
    p->f |= FM_REDRAW_CMD;
    break;
  case RL_PARTIAL:
    vt_ich(&p->io, n);
    str_push(&p->io, (char *) p->k.b, p->k.l);
    p->f |= FM_REDRAW_FLUSH;
    break;
  case RL_NONE: return;
  }
  if (p->kp) p->kp(p, 0, rl_cl_get(&p->r), rl_cr_get(&p->r));
}

static inline void
input_insert_paste(struct fm *p)
{
  for (bool s = false;;) {
    if (!term_key_read(p->t.fd, &p->k)) return;
    if (p->k.c == KEY_PASTE_END) return;
    if (p->k.b[0] == '\r' || p->k.b[0] == '\n') {
      if (!s) p->k.c = p->k.b[0] = ' ';
      s = true;
    } else
      s = false;
    if (KEY_GET_MOD(p->k.c) || KEY_IS_SYM(p->k.c) || p->k.c < 32)
      continue;
    input_insert(p);
  }
}

// }}}

#include "config_cmd.h"
#include "config_key.h"

// Init {{{

static inline usize
fm_io_flush(str *s, void *ctx, usize n)
{
  (void) n;
  struct fm *p = ctx;
  write_all(p->t.fd, s->m, s->l);
  s->l = 0;
  return 0;
}

static inline int
fm_init(struct fm *p)
{
  if (fs_watch_init(&p->p) == -1)
    return -1;
  p->opener = get_env("DFM_OPENER", DFM_OPENER);
  p->dfd = AT_FDCWD;
  p->ds = DFM_DEFAULT_SORT;
  p->dv = DFM_DEFAULT_VIEW;
  p->sf = fm_filter_startswith;
  p->dec = sizeof(p->de);
  p->tz  = tz_offset();
#if DFM_SHOW_HIDDEN
  p->f |= FM_HIDDEN;
#endif
  if (!geteuid()) p->f |= FM_ROOT;
  fm_mark_clear_all(p);
  STR_INIT(&p->pwd,  DFM_PATH_MAX, 0, 0);
  STR_INIT(&p->ppwd, DFM_PATH_MAX, 0, 0);
  STR_INIT(&p->mpwd, DFM_PATH_MAX, 0, 0);
  STR_INIT(&p->io,   DFM_IO_MAX,   fm_io_flush, p);
  return 0;
}

static inline void
fm_free(struct fm *p)
{
  fs_watch_free(&p->p);
  close(p->dfd);
  int fd = term_dead(&p->t) ? STDOUT_FILENO : STDERR_FILENO;
  if (!p->pwd.l) return;
  write_all(fd, p->pwd.m, p->pwd.l);
  write_all(fd, S("\n"));
}

// }}}

// Main {{{

static inline void
fm_watch_handle(struct fm *p)
{
  for (cut n = {0};;) {
    switch (fs_watch_pump(&p->p, &n.d, &n.l)) {
    case '!': fm_dir_refresh(p); return;
    case '+': fm_dir_add(p, n); break;
    case '-': fm_dir_del(p, n); break;
    case '~': fm_dir_del(p, n); fm_dir_add(p, n); break;
    default: return;
    }
  }
}

static inline void
fm_update(struct fm *p)
{
  term_reap();
  fm_watch_handle(p);
  if (!(p->f & FM_DIRTY)) return;
  p->f &= ~FM_DIRTY;
  p->f |= FM_REDRAW_DIR|FM_REDRAW_NAV;
  fm_dir_sort(p);
  fm_cursor_sync(p);
  if (p->f & FM_DIRTY_WITHIN && p->st) {
    u64 m = ent_load_off(p, p->st);
    cut st = { p->de + p->st, ent_get(m, LEN) };
    fm_scroll_to(p, st);
    p->st = 0;
    p->f &= ~FM_DIRTY_WITHIN;
  }
}

static inline void
fm_draw(struct fm *p)
{
  if ((p->f & FM_REDRAW) == FM_REDRAW) {
    STR_PUSH(&p->io, VT_ED2);
    fm_dir_ht_clear_cache(p);
  }
  if (p->f & FM_REDRAW_DIR)
    fm_draw_dir(p);
  if (p->f & FM_REDRAW_NAV)
    fm_draw_nav(p);
  if (p->f & FM_REDRAW_CMD)
    fm_draw_cmd(p);
  if (p->f & FM_REDRAW) {
    if (p->kp || p->kd) {
      vt_cup(&p->io, p->r.vx, p->row + DFM_MARGIN);
      STR_PUSH(&p->io, VT_DECTCEM_Y);
    } else {
      vt_cup(&p->io, 0, p->o + 1);
      STR_PUSH(&p->io, VT_DECTCEM_N);
    }
    fm_draw_flush(p);
  }
  p->f &= ~FM_REDRAW;
}

static inline void
fm_input(struct fm *p)
{
  if (!term_key_read(p->t.fd, &p->k))
    return;
  if (p->r.pr.l) fm_key_input(p->k.c)(p);
  else fm_key(p->k.c)(p);
}

static inline int
fm_run(struct fm *p)
{
  if (fm_term_init(p) < 0) return -1;
  rl_init(&p->r, p->col, CUT_NULL);
  for (; likely(!term_dead(&p->t)); ) {
    fm_update(p);
    fm_draw(p);
    int e = term_wait(&p->t);
    if (e & TERM_WAIT_WCH)
      if (fm_term_resize(p) < 0)
        fm_draw_err(p, S("resize failed"), errno);
    if (e & TERM_WAIT_KEY)
      fm_input(p);
  }
  fm_term_free(p);
  if (!(p->f & FM_PRINT_PWD)) p->pwd.l = 0;
  return 0;
}

int
main(int argc, char *argv[])
{
  static struct fm p;
  str *s = &p.pwd;

  if (fm_init(&p) < 0) {
    STR_PUSH(s, "error?: ");
    str_push_s(s, strerror(errno));
    goto e;
  }

  const char *pwd = ".";
  struct argv A = arg_init(argc, argv);

  for (struct arg a; (a = arg_next(&A)).sign != -1;) {
    const char *n;
    switch (a.name) {
    case 'H':
      p.f ^= (-(a.sign == '+') ^ p.f) & FM_HIDDEN;
      continue;
    case 'p':
      p.f |= FM_PICKER;
      continue;
    case 'o':
      n = arg_next_positional(&A);
      if (!n) goto arg_no_val;
      p.opener.d = n;
      continue;
    case 's':
      n = arg_next_positional(&A);
      if (!n) goto arg_no_val;
      p.ds = fm_sort_fn(*n) ? *n : 'n';
      continue;
    case 'v':
      n = arg_next_positional(&A);
      if (!n) goto arg_no_val;
      p.dv = *n;
      continue;
    case '-':
      if (!strcmp(a.pos, "--help")) {
        STR_PUSH(s, DFM_HELP);
        term_set_dead(&p.t, 1);
        goto e;
      } else if (!strcmp(a.pos, "--version")) {
        STR_PUSH(s, CFG_NAME " " CFG_VERSION " "
          CC_COMMIT " (" CC_BRANCH ") " CC_DATE);
        goto e;
      }
      STR_PUSH(s, "unknown arg ");
      str_push_s(s, a.pos);
      goto e;
    default:
      if (unlikely(a.name)) {
        STR_PUSH(s, "unknown arg ");
        str_push_c(s, a.sign);
        str_push_c(s, a.name);
        goto e;
      }
      pwd = a.pos;
      continue;
    }
arg_no_val:
    STR_PUSH(s, "arg ");
    str_push_c(s, a.sign);
    str_push_c(s, a.name);
    STR_PUSH(s, " missing value");
    goto e;
  }

  if (!fm_path_chdir(&p, pwd)) {
    STR_PUSH(s, "cd: '");
    str_push_s(s, pwd);
    STR_PUSH(s, "': ");
    str_push_s(s, strerror(errno));
    goto e;
  }

  if (fm_run(&p) < 0) {
    STR_PUSH(s, "term: '");
    str_push_s(s, strerror(errno));
    goto e;
  }

  fm_free(&p);
  return EXIT_SUCCESS;
e:
  fm_free(&p);
  return EXIT_FAILURE;
}

// }}}

