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
#ifndef DFM_PLATFORM_APPLE_H
#define DFM_PLATFORM_APPLE_H

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/event.h>
#include <sys/stat.h>

#define ST_ATIM st_atimespec.tv_sec
#define ST_MTIM st_mtimespec.tv_sec
#define ST_CTIM st_ctimespec.tv_sec

#define MAX_FILES 4096

struct file_ent {
  char name[PATH_MAX];
  time_t mtime;
};

struct platform {
  int kq;
  int dirfd;

  struct file_ent files[MAX_FILES];
  size_t file_count;

  char pending_name[PATH_MAX];
  char pending_type;
};

static size_t
snapshot(int dirfd, struct file_ent *out)
{
  size_t n = 0;
  DIR *d = fdopendir(dup(dirfd));
  if (!d)
    return 0;

  struct dirent *de;
  while ((de = readdir(d)) && n < MAX_FILES) {
    if (!strcmp(de->d_name, ".") ||
        !strcmp(de->d_name, ".."))
      continue;

    struct stat st;
    if (!fstatat(dirfd, de->d_name, &st, 0)) {
      strncpy(out[n].name, de->d_name, PATH_MAX - 1);
      out[n].name[PATH_MAX - 1] = 0;
      out[n].mtime = st.ST_MTIM;
      n++;
    }
  }

  closedir(d);
  return n;
}

static inline int
fs_watch_init(struct platform *p)
{
  memset(p, 0, sizeof(*p));
  p->kq = kqueue();
  return p->kq;
}

static inline void
fs_watch(struct platform *p, const char *path)
{
  if (p->dirfd != 0)
    close(p->dirfd);

  p->dirfd = open(path, O_RDONLY);
  if (p->dirfd < 0)
    return;

  struct kevent ev;
  EV_SET(&ev, p->dirfd, EVFILT_VNODE,
         EV_ADD | EV_CLEAR,
         NOTE_WRITE | NOTE_DELETE | NOTE_EXTEND | NOTE_ATTRIB,
         0, NULL);

  kevent(p->kq, &ev, 1, NULL, 0, NULL);

  p->file_count = snapshot(p->dirfd, p->files);
}

static inline int
fs_watch_pump(struct platform *p, const char **s, size_t *l)
{
  if (p->pending_type) {
    *s = p->pending_name;
    *l = strlen(p->pending_name);
    int t = p->pending_type;
    p->pending_type = 0;
    return t;
  }

  struct kevent ev;
  struct timespec ts = {0, 0};

  if (kevent(p->kq, NULL, 0, &ev, 1, &ts) <= 0)
    return 0;

  struct file_ent new_files[MAX_FILES];
  size_t new_count = snapshot(p->dirfd, new_files);

  for (size_t i = 0; i < new_count; i++) {
    int found = 0;
    for (size_t j = 0; j < p->file_count; j++)
      if (!strcmp(new_files[i].name, p->files[j].name))
        found = 1;

    if (!found) {
      strcpy(p->pending_name, new_files[i].name);
      p->pending_type = '+';
      goto done;
    }
  }

  for (size_t i = 0; i < p->file_count; i++) {
    int found = 0;
    for (size_t j = 0; j < new_count; j++)
      if (!strcmp(p->files[i].name, new_files[j].name))
        found = 1;

    if (!found) {
      strcpy(p->pending_name, p->files[i].name);
      p->pending_type = '-';
      goto done;
    }
  }

  for (size_t i = 0; i < new_count; i++)
    for (size_t j = 0; j < p->file_count; j++)
      if (!strcmp(new_files[i].name, p->files[j].name) &&
          new_files[i].mtime != p->files[j].mtime) {
        strcpy(p->pending_name, new_files[i].name);
        p->pending_type = '~';
        goto done;
      }

done:
  memcpy(p->files, new_files, sizeof(new_files));
  p->file_count = new_count;

  if (p->pending_type) {
    *s = p->pending_name;
    *l = strlen(p->pending_name);
    int t = p->pending_type;
    p->pending_type = 0;
    return t;
  }

  return 0;
}

static inline void
fs_watch_free(struct platform *p)
{
  if (p->dirfd)
    close(p->dirfd);
  if (p->kq >= 0)
    close(p->kq);
}

#endif
