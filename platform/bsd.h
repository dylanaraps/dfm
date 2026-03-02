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
#ifndef DFM_PLATFORM_BSD_H
#define DFM_PLATFORM_BSD_H

#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "../lib/util.h"

#define ST_ATIM st_atimespec.tv_sec
#define ST_MTIM st_mtimespec.tv_sec
#define ST_CTIM st_ctimespec.tv_sec

struct platform {
  int kq;
  int dfd;
  struct kevent ev;
};

static inline int
fs_watch_init(struct platform *p)
{
  p->dfd = -1;
  p->kq = kqueue();
  return p->kq;
}

static inline void
fs_watch(struct platform *p, const char *path)
{
  if (p->dfd != -1) {
    close(p->dfd);
    p->dfd = -1;
  }
  p->dfd = open(path, O_EVTONLY);
  if (p->dfd == -1) return;
  EV_SET(&p->ev, p->dfd,
    EVFILT_VNODE, EV_ADD|EV_CLEAR,
    NOTE_WRITE|NOTE_DELETE|NOTE_RENAME|NOTE_ATTRIB, 0, NULL);
  kevent(p->kq, &p->ev, 1, NULL, 0, NULL);
}

static inline int
fs_watch_pump(struct platform *p, const char **s, size_t *l)
{
  *s = NULL;
  *l = 0;
  if (p->kq == -1 || p->dfd == -1)
    return 0;
  struct kevent o;
  struct timespec ts = {0, 0};
  int n = kevent(p->kq, NULL, 0, &o, 1, &ts);
  if (n <= 0) return 0;
  if (o.flags & EV_ERROR)     return '!';
  if (o.fflags & NOTE_DELETE) return '-';
  if (o.fflags & NOTE_RENAME) return '~';
  if (o.fflags & NOTE_ATTRIB) return '~';
  if (o.fflags & NOTE_WRITE)  return '+';
  return 0;
}

static inline void
fs_watch_free(struct platform *p)
{
  if (p->dfd != -1) close(p->dfd);
  if (p->kq != -1)  close(p->kq);
}

#define FS_WATCH 1

#endif // DFM_PLATFORM_BSD_H
