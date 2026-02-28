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
#ifndef DYLAN_VT_H
#define DYLAN_VT_H

#include "str.h"
#include "util.h"

#define VT_ESC           "\x1b"
#define VT_CR            "\r"
#define VT_LF            "\n"
#define VT_CUU1          VT_ESC "[A"
#define VT_CUU(n)        VT_ESC "[" #n "A"
#define VT_CUD1          VT_ESC "[B"
#define VT_CUD(n)        VT_ESC "[" #n "B"
#define VT_CUF1          VT_ESC "[C"
#define VT_CUF(n)        VT_ESC "[" #n "C"
#define VT_CUB1          VT_ESC "[D"
#define VT_CUB(n)        VT_ESC "[" #n "D"
#define VT_CUP(x, y)     VT_ESC "[" #x  ";" #y "H"
#define VT_CUP1          VT_ESC "[H"
#define VT_DECAWM_Y      VT_ESC "[?7h"
#define VT_DECAWM_N      VT_ESC "[?7l"
#define VT_DECSC         VT_ESC "7"
#define VT_DECRC         VT_ESC "8"
#define VT_DECSTBM(x, y) VT_ESC "[" #x ";" #y "r"
#define VT_DECTCEM_Y     VT_ESC "[?25h"
#define VT_DECTCEM_N     VT_ESC "[?25l"
#define VT_ED0           VT_ESC "[J"
#define VT_ED1           VT_ESC "[1J"
#define VT_ED2           VT_ESC "[2J"
#define VT_EL0           VT_ESC "[K"
#define VT_EL1           VT_ESC "[1K"
#define VT_EL2           VT_ESC "[2K"
#define VT_IL0           VT_ESC "[L"
#define VT_IL(n)         VT_ESC "[" #n "L"
#define VT_ICH1          VT_ESC "[@"
#define VT_ICH(n)        VT_ESC "[" #n "@"
#define VT_DCH1          VT_ESC "[P"
#define VT_DCH(n)        VT_ESC "[" #n "P"
#define VT_SGR0          VT_ESC "[m"

//
// XTerm Alternate Screen.
// https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h2-The-Alternate-Screen-Buffer
//
#define VT_ALT_SCREEN_Y  VT_ESC "[?1049h"
#define VT_ALT_SCREEN_N  VT_ESC "[?1049l"

//
// Synchronized Updates.
// https://gist.github.com/christianparpart/d8a62cc1ab659194337d73e399004036
// https://github.com/contour-terminal/vt-extensions/blob/master/synchronized-output.md
//
#define VT_BSU VT_ESC "[?2026h"
#define VT_ESU VT_ESC "[?2026l"

//
// Bracketed Paste.
//
#define VT_BPASTE_ON  VT_ESC "[?2004h"
#define VT_BPASTE_OFF VT_ESC "[?2004l"

//
// VT_SGR(...) macro supporting 16 arguments.
// NOTE: A byte can be saved by using VT_SGR0 instead of VT_SGR(0).
//
#define VT_Fa(f, x) f(x)
#define VT_Fb(f, x, ...)  f(x) ";" VT_Fa(f, __VA_ARGS__)
#define VT_Fc(f, x, ...)  f(x) ";" VT_Fb(f, __VA_ARGS__)
#define VT_Fd(f, x, ...)  f(x) ";" VT_Fc(f, __VA_ARGS__)
#define VT_Fe(f, x, ...)  f(x) ";" VT_Fd(f, __VA_ARGS__)
#define VT_Ff(f, x, ...)  f(x) ";" VT_Fe(f, __VA_ARGS__)
#define VT_Fg(f, x, ...)  f(x) ";" VT_Ff(f, __VA_ARGS__)
#define VT_Fh(f, x, ...)  f(x) ";" VT_Fg(f, __VA_ARGS__)
#define VT_Fi(f, x, ...)  f(x) ";" VT_Fh(f, __VA_ARGS__)
#define VT_Fj(f, x, ...)  f(x) ";" VT_Fi(f, __VA_ARGS__)
#define VT_Fk(f, x, ...)  f(x) ";" VT_Fj(f, __VA_ARGS__)
#define VT_Fl(f, x, ...)  f(x) ";" VT_Fk(f, __VA_ARGS__)
#define VT_Fm(f, x, ...)  f(x) ";" VT_Fl(f, __VA_ARGS__)
#define VT_Fn(f, x, ...)  f(x) ";" VT_Fm(f, __VA_ARGS__)
#define VT_Fo(f, x, ...)  f(x) ";" VT_Fn(f, __VA_ARGS__)
#define VT_Fp(f, x, ...)  f(x) ";" VT_Fo(f, __VA_ARGS__)
#define VT_GET_q(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,...) q
#define VT_CNT(...)      VT_GET_q(__VA_ARGS__,p,o,n,m,l,k,j,i,h,g,f,e,d,c,b,a,0)
#define VT_STR(x)        #x
#define VT_CAT(a,b)      a##b
#define VT_FN(N, f, ...) VT_CAT(VT_F, N)(f, __VA_ARGS__)
#define VT_JOIN(...)     VT_FN(VT_CNT(__VA_ARGS__), VT_STR, __VA_ARGS__)
#define VT_SGR(...)      VT_ESC "[" VT_JOIN(__VA_ARGS__) "m"

static inline void
vt_cup(str *s, u32 x, u32 y)
{
  STR_PUSH(s, VT_ESC "[");
  str_push_u32(s, y);
  str_push_c(s, ';');
  str_push_u32(s, x);
  str_push_c(s, 'H');
}

static inline void
vt_cuf(str *s, u32 n)
{
  STR_PUSH(s, VT_ESC "[");
  str_push_u32(s, n);
  str_push_c(s, 'C');
}

static inline void
vt_cub(str *s, u32 n)
{
  STR_PUSH(s, VT_ESC "[");
  str_push_u32(s, n);
  str_push_c(s, 'D');
}

static inline void
vt_ich(str *s, u32 n)
{
  STR_PUSH(s, VT_ESC "[");
  str_push_u32(s, n);
  str_push_c(s, '@');
}

static inline void
vt_dch(str *s, u32 n)
{
  STR_PUSH(s, VT_ESC "[");
  str_push_u32(s, n);
  str_push_c(s, 'P');
}

static inline void
vt_decstbm(str *s, u32 x, u32 y)
{
  STR_PUSH(s, VT_ESC "[");
  str_push_u32(s, x);
  str_push_c(s, ';');
  str_push_u32(s, y);
  str_push_c(s, 'r');
}

static inline void
vt_sgr(str *s, u32 a, u32 b)
{
  STR_PUSH(s, VT_ESC "[");
  str_push_u32(s, a);
  str_push_c(s, ';');
  str_push_u32(s, b);
  str_push_c(s, 'm');
}

#endif // DYLAN_VT_H

