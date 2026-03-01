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
#include <stdlib.h>
#include <string.h>
#include <CoreServices/CoreServices.h>
#include <pthread.h>

#define ST_ATIM st_atimespec.tv_sec
#define ST_MTIM st_mtimespec.tv_sec
#define ST_CTIM st_ctimespec.tv_sec

struct fs_event {
  char *name;
  size_t len;
  char type;
};

struct platform {
  FSEventStreamRef stream;
  CFRunLoopRef runloop;

  struct fs_event queue[64];
  int qh, qt;

  pthread_mutex_t lock;
};


static void
fs_event_callback(ConstFSEventStreamRef streamRef,
                  void *info,
                  size_t numEvents,
                  void *eventPaths,
                  const FSEventStreamEventFlags flags[],
                  const FSEventStreamEventId ids[])
{
  (void)streamRef;
  (void)ids;

  struct platform *p = info;
  char **paths = eventPaths;

  pthread_mutex_lock(&p->lock);

  for (size_t i = 0; i < numEvents; ++i) {
    char type = 0;

    if (flags[i] & kFSEventStreamEventFlagItemCreated)
      type = '+';
    else if (flags[i] & kFSEventStreamEventFlagItemRemoved)
      type = '-';
    else if (flags[i] & kFSEventStreamEventFlagItemModified)
      type = '~';

    if (!type)
      continue;

    int next = (p->qt + 1) % 64;
    if (next == p->qh)
      continue; 

    const char *full = paths[i]; 
    const char *base = strrchr(full, '/');
    base = base ? base + 1 : full;

    p->queue[p->qt].len = strlen(base);
    p->queue[p->qt].name = strdup(base);
    p->queue[p->qt].type = type;

    p->qt = next;
  }

  pthread_mutex_unlock(&p->lock);
}

static inline int
fs_watch_init(struct platform *p)
{
  memset(p, 0, sizeof(*p));
  pthread_mutex_init(&p->lock, NULL);
  p->runloop = CFRunLoopGetCurrent();
  return 0;
}

static inline void
fs_watch(struct platform *p, const char *path)
{
  if (p->stream) {
    FSEventStreamStop(p->stream);
    FSEventStreamInvalidate(p->stream);
    FSEventStreamRelease(p->stream);
  }

  CFStringRef cfpath =
      CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);

  CFArrayRef paths =
      CFArrayCreate(NULL, (const void **)&cfpath, 1, NULL);

  FSEventStreamContext ctx = {0};
  ctx.info = p;

  p->stream = FSEventStreamCreate(
      NULL,
      fs_event_callback,
      &ctx,
      paths,
      kFSEventStreamEventIdSinceNow,
      0.05,
      kFSEventStreamCreateFlagFileEvents
  );

  FSEventStreamScheduleWithRunLoop(
      p->stream,
      p->runloop,
      kCFRunLoopDefaultMode);

  FSEventStreamStart(p->stream);

  CFRelease(paths);
  CFRelease(cfpath);
}

static inline int
fs_watch_pump(struct platform *p, const char **s, size_t *l)
{
  /* Non-blocking runloop tick */
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true);

  pthread_mutex_lock(&p->lock);

  if (p->qh == p->qt) {
    pthread_mutex_unlock(&p->lock);
    return 0;
  }

  struct fs_event *e = &p->queue[p->qh];
  p->qh = (p->qh + 1) % 64;

  *s = e->name;
  *l = e->len;
  char type = e->type;

  free(e->name);

  pthread_mutex_unlock(&p->lock);

  return type;
}

static inline void
fs_watch_free(struct platform *p)
{
  if (p->stream) {
    FSEventStreamStop(p->stream);
    FSEventStreamInvalidate(p->stream);
    FSEventStreamRelease(p->stream);
  }

  pthread_mutex_destroy(&p->lock);
}

#endif
