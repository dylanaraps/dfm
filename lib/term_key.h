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
#ifndef DYLAN_TERM_KEY_H
#define DYLAN_TERM_KEY_H

#include <stdbool.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/time.h>

#include "utf8.h"
#include "util.h"

//
// Store encoded special keys alongside utf8 codepoints in an unused range.
//
#define KEY_TAG        0x80000000u
#define KEY_SYM        0x40000000u
#define KEY_MOD_SHIFT  27u
#define KEY_MOD_MASK   (0x7u << KEY_MOD_SHIFT)
#define KEY_TXT_MASK   0x001FFFFFu
#define KEY_SYM_MASK   0x000000FFu
#define KEY_IS_SYM(k)  ((((u32)(k)) & (KEY_TAG|KEY_SYM)) == (KEY_TAG|KEY_SYM))
#define KEY_GET_MOD(k) ((((u32)(k)) & KEY_MOD_MASK) >> KEY_MOD_SHIFT)

#define K(m, c) \
((u32)(m) == 0u ? (u32)(c) : (KEY_IS_SYM(c) \
? (((u32)(c) & ~KEY_MOD_MASK) | (((u32)(m) & 0x7u) << KEY_MOD_SHIFT)) \
: (KEY_TAG | (((u32)(m) & 0x7u) << KEY_MOD_SHIFT) | ((u32)(c) & KEY_TXT_MASK))))

#define KEY_REG(id) (KEY_TAG|KEY_SYM | ((u32)(id) & KEY_SYM_MASK))

#define MOD_SHIFT (1u << 0)
#define MOD_ALT   (1u << 1)
#define MOD_CTRL  (1u << 2)

#define KEY_ESCAPE       27
#define KEY_BACKSPACE    127
#define KEY_TAB          K(MOD_CTRL, 'i')
#define KEY_SHIFT_TAB    K(MOD_SHIFT|MOD_CTRL, 'i')
#define KEY_ENTER        K(MOD_CTRL, 'm')
#define KEY_UP           KEY_REG(1)
#define KEY_DOWN         KEY_REG(2)
#define KEY_LEFT         KEY_REG(3)
#define KEY_RIGHT        KEY_REG(4)
#define KEY_HOME         KEY_REG(5)
#define KEY_END          KEY_REG(6)
#define KEY_PAGE_UP      KEY_REG(7)
#define KEY_PAGE_DOWN    KEY_REG(8)
#define KEY_INSERT       KEY_REG(9)
#define KEY_DELETE       KEY_REG(10)
#define KEY_F1           KEY_REG(11)
#define KEY_F2           KEY_REG(12)
#define KEY_F3           KEY_REG(13)
#define KEY_F4           KEY_REG(14)
#define KEY_F5           KEY_REG(15)
#define KEY_F6           KEY_REG(16)
#define KEY_F7           KEY_REG(17)
#define KEY_F8           KEY_REG(18)
#define KEY_F9           KEY_REG(19)
#define KEY_F10          KEY_REG(20)
#define KEY_F11          KEY_REG(21)
#define KEY_F12          KEY_REG(22)
#define KEY_PASTE        KEY_REG(23)
#define KEY_PASTE_END    KEY_REG(24)

struct term_key {
  u8 b[64];
  u16 l;
  u32 c;
};

static inline u32
term_key_csi_tilde(u8 c, u32 m)
{
  switch (c) {
  case   1: return K(m, KEY_HOME);
  case   2: return K(m, KEY_INSERT);
  case   3: return K(m, KEY_DELETE);
  case   4: return K(m, KEY_END);
  case   5: return K(m, KEY_PAGE_UP);
  case   6: return K(m, KEY_PAGE_DOWN);
  case   7: return K(m, KEY_HOME);
  case   8: return K(m, KEY_END);
  case  11: return K(m, KEY_F1);
  case  12: return K(m, KEY_F2);
  case  13: return K(m, KEY_F3);
  case  14: return K(m, KEY_F4);
  case  15: return K(m, KEY_F5);
  case  17: return K(m, KEY_F6);
  case  18: return K(m, KEY_F7);
  case  19: return K(m, KEY_F8);
  case  20: return K(m, KEY_F9);
  case  21: return K(m, KEY_F10);
  case  23: return K(m, KEY_F11);
  case  24: return K(m, KEY_F12);
  case 200: return KEY_PASTE;
  case 201: return KEY_PASTE_END;
  default:  return 0;
  }
}

static inline u32
term_key_csi_final(u8 c, u32 m)
{
  switch (c) {
  case 'A': return K(m, KEY_UP);
  case 'B': return K(m, KEY_DOWN);
  case 'C': return K(m, KEY_RIGHT);
  case 'D': return K(m, KEY_LEFT);
  case 'H': return K(m, KEY_HOME);
  case 'F': return K(m, KEY_END);
  case 'Z': return K(MOD_SHIFT|MOD_CTRL | m, 'i');
  default:  return 0;
  }
}

static inline u32
term_key_csi_ss3(u8 c)
{
  switch (c) {
  case 'P': return K(0, KEY_F1);
  case 'Q': return K(0, KEY_F2);
  case 'R': return K(0, KEY_F3);
  case 'S': return K(0, KEY_F4);
  case 'A': return K(0, KEY_UP);
  case 'B': return K(0, KEY_DOWN);
  case 'C': return K(0, KEY_RIGHT);
  case 'D': return K(0, KEY_LEFT);
  case 'H': return K(0, KEY_HOME);
  case 'F': return K(0, KEY_END);
  default:  return 0;
  }
}

static inline s32
term_key_csi_int(const u8 *b, usize l, usize *i)
{
  u32 v = 0;
  usize j = *i;
  if (j >= l || b[j] < '0' || b[j] > '9') return -1;
  for (; j < l && b[j] >= '0' && b[j] <= '9'; j++)
    v = v * 10u + (u32)(b[j] - '0');
  *i = j;
  return (s32) v;
}

static inline u32
term_key_csi_xterm_mod(u32 x)
{
  u32 m = 0;
  if (x < 2 || x > 8) return m;
  if (x & 1u) m |= MOD_ALT;
  if (x & 2u) m |= MOD_SHIFT;
  if (x & 4u) m |= MOD_CTRL;
  return m;
}

static inline bool
term_key_csi_end(u8 c)
{
  return c >= 0x40 && c <= 0x7E;
}

static inline bool
term_key_csi_read(int fd, struct term_key *k)
{
  for (;;) {
    if (k->l >= sizeof(k->b))
      return false;
    if (read(fd, &k->b[k->l], 1) != 1)
      return false;
    u8 c = k->b[k->l++];
    if (term_key_csi_end(c))
      break;
  }
  return true;
}

static inline bool
term_key_ss3(int fd, struct term_key *k)
{
  if (read(fd, &k->b[k->l], 1) != 1) return false;
  k->l++;
  k->c = term_key_csi_ss3(k->b[2]);
  return !!k->c;
}

static inline bool
term_key_csi(int fd, struct term_key *k)
{
  if (!term_key_csi_read(fd, k)) return false;
  usize n = k->l;
  if (n < 3) return false;
  u8 f = k->b[n - 1];
  usize i = 2;
  if (i < n && (k->b[i] == '?' || k->b[i] == '>' || k->b[i] == '<'))
    i++;
  s32 p1 = term_key_csi_int(k->b, n, &i);
  s32 p2 = -1;
  if (p1 != -1 && i < n && k->b[i] == ';') {
    i++;
    p2 = term_key_csi_int(k->b, n, &i);
  }
  u32 m = p2 != -1 ? term_key_csi_xterm_mod(p2) : 0;
  if (f == '~' && p1 != -1)
    k->c = term_key_csi_tilde(p1, m);
  else
    k->c = term_key_csi_final(f, m);
  return !!k->c;
}

static inline bool
term_key_utf8(int fd, struct term_key *k, usize o, u32 m)
{
  u8 c = k->b[o];
  if (c >= 1 && c <= 26) {
    k->c = K(m | MOD_CTRL, 'a' + (c - 1));
    k->l = o + 1;
    return true;
  }
  if (c >= 0xC0) {
    usize l = utf8_expected(c);
    if (!l) return false;
    for (usize i = 1; i < l; i++)
      if (read(fd, &k->b[o + i], 1) != 1)
        return false;
    int e;
    u32 cp;
    utf8_decode_untrusted(&k->b[o], &cp, &e);
    if (e) return false;
    k->c = K(m, cp);
    k->l = o + l;
    return true;
  }
  k->c = K(m, c);
  k->l = o + 1;
  return true;
}

static inline bool
term_key_read(int fd, struct term_key *k)
{
  if (read(fd, k->b, 1) != 1) return false;
  k->l = 1;
  if (unlikely(k->b[0] == '\033')) {
    struct timeval tv = { .tv_sec = 0, .tv_usec = 30000 };
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0) {
      k->c = k->b[0];
      return true;
    }
    if (read(fd, &k->b[1], 1) != 1) return false;
    k->l = 2;
    switch (k->b[1]) {
    case '[': return term_key_csi(fd, k);
    case 'O': return term_key_ss3(fd, k);
    default:  return term_key_utf8(fd, k, 1, MOD_ALT);
    }
  }
  return term_key_utf8(fd, k, 0, 0);
}

#endif // DYLAN_TERM_KEY_H

