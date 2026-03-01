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
#ifndef DFM_PLATFORM_LINUX_H
#define DFM_PLATFORM_LINUX_H

#include <stdalign.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include <sys/inotify.h>

#include "../lib/util.h"

#define ST_ATIM st_atim.tv_sec
#define ST_MTIM st_mtim.tv_sec
#define ST_CTIM st_ctim.tv_sec

struct platform {
  int inotify_wd;
  int inotify_fd;
  union {
    align_max _a;
    char b[4096];
  } in;
  ssize_t inl;
  ssize_t ino;
};

static inline int
fs_watch_init(struct platform *p)
{
  p->inotify_wd = -1;
  p->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  return p->inotify_fd;
}

static inline void
fs_watch(struct platform *p, const char *s)
{
  if (p->inotify_wd != -1)
    inotify_rm_watch(p->inotify_fd, p->inotify_wd);
  p->inotify_wd = inotify_add_watch(p->inotify_fd, s,
    IN_CREATE|IN_DELETE|IN_MOVED_FROM|IN_MOVED_TO|IN_ATTRIB);
}

static inline int
fs_watch_pump(struct platform *p, const char **s, size_t *l)
{
  *s = NULL;
  if (p->inotify_fd == -1)
    return 0;
  if (p->ino >= p->inl) {
    p->inl = read(p->inotify_fd, p->in.b, sizeof(p->in.b));
    if (p->inl <= 0)
      return 0;
    p->ino = 0;
  }
  if (p->inl - p->ino < (ssize_t)sizeof(struct inotify_event))
    return 0;
  struct inotify_event *ev =
    (struct inotify_event *)(void *)(p->in.b + p->ino);
  ssize_t size = sizeof(struct inotify_event) + ev->len;
  if (p->inl - p->ino < size)
    return 0;
  p->ino += size;
  if (ev->mask & IN_Q_OVERFLOW)
    return '!';
  if (!ev->len)
    return 0;
  *s = ev->name;
  *l = strlen(ev->name);
  if (ev->mask & (IN_CREATE | IN_MOVED_TO))
    return '+';
  if (ev->mask & (IN_DELETE | IN_MOVED_FROM))
    return '-';
  if (ev->mask & IN_ATTRIB)
    return '~';
  return 0;
}

static inline void
fs_watch_free(struct platform *p)
{
  if (p->inotify_wd != -1)
    inotify_rm_watch(p->inotify_fd, p->inotify_wd);
  if (p->inotify_fd != -1)
    close(p->inotify_fd);
}

#define FS_WATCH 1

#endif // DFM_PLATFORM_LINUX_H

