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
#ifndef DFM_PLATFORM_POSIX_H
#define DFM_PLATFORM_POSIX_H

#include <stddef.h>

struct platform {
  void *_pad;
};

static inline int
fs_watch_init(struct platform *p)
{
  (void) p;
  return 0;
}

static inline void
fs_watch(struct platform *p, const char *s)
{
  (void) p;
  (void) s;
}

static inline int
fs_watch_pump(struct platform *p, const char **s, size_t *l)
{
  (void) p;
  (void) s;
  (void) l;
  return 0;
}

static inline void
fs_watch_free(struct platform *p)
{
  (void) p;
}

#endif // DFM_PLATFORM_POSIX_H
       
