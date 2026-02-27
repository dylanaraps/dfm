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
#ifndef DYLAN_ARG
#define DYLAN_ARG

struct argv {
  const char *const *argv;
  int c;
};

struct arg {
  const char *pos;
  int sign;
  int name;
};

static inline struct argv
arg_init(int argc, char *a[])
{
  (void) argc;
  return (struct argv) { .argv = (const char *const *) ++a };
}

static inline struct arg
arg_next(struct argv *s)
{
  static const signed char t[256] = {
    ['-']  = '-' - 1, ['+']  = '+' - 1,
  };

  struct arg a = {0};
  a.pos  = *s->argv;
  a.sign = a.pos ? t[(unsigned char) **s->argv] + 1 : -1;

  if (a.sign > 1) {
    s->c |= !s->c;
    a.name = (unsigned char) (a.pos[s->c] ? a.pos[s->c] : a.sign);
    s->c += !!a.pos[s->c];
    s->c *= !!a.pos[s->c];
  }

  s->argv += !s->c;
  return a;
}

static inline const char *
arg_next_positional(struct argv *s)
{
  const char *a = *s->argv;
  if (!a) return a;
  a += s->c;
  s->c = 0;
  s->argv++;
  return a;
}

#endif // DYLAN_ARG

