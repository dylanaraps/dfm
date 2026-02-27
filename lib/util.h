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
#ifndef DYLAN_UTIL_H
#define DYLAN_UTIL_H

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <spawn.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#define ARR_SIZE(a) ((intptr_t)(sizeof(a) / sizeof(*(a))))
#define IS_POW2(x)  ((x) > 0 && (((x) & ((x) - 1)) == 0))
#define MIN(x, y)   ((x) < (y) ? (x) : (y))
#define MAX(x, y)   ((x) > (y) ? (x) : (y))

#if defined(__GNUC__) || defined(__clang__)
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   !!(x)
#define unlikely(x) !!(x)
#endif

#if __STDC_VERSION__ >= 201112L
#define STATIC_ASSERT _Static_assert
#else
#define SA_CAT_(a,b) a##b
#define SA_CAT(a,b)  SA_CAT_(a,b)
#define STATIC_ASSERT(c, m) \
  enum { SA_CAT(static_assert_line_, __LINE__) = 1 / (!!(c)) }
#endif

typedef union {
  long long ll;
  long double ld;
  void *p;
} align_max;

typedef uint64_t  u64;
typedef uint32_t  u32;
typedef uint16_t  u16;
typedef uint8_t   u8;
typedef int64_t   s64;
typedef int32_t   s32;
typedef int16_t   s16;
typedef int8_t    s8;
typedef size_t    usize;
typedef ptrdiff_t size;

typedef struct {
  const char *d;
  usize l;
} cut;

//
// Add four bytes to the end of the CUT to allow the branchless utf8 decoder to
// potentially overread the string.
//
#define CUT(s)   (cut) { s "\0\0\0\0", sizeof(s) - 1 }
#define CUT_NULL ((cut){0})
#define STR_NULL (&(str){0})
#define S(s)     (s), (sizeof(s) - 1)

static inline int
cut_cmp(cut a, cut b)
{
  return a.l == b.l && *a.d == *b.d && !memcmp(a.d, b.d, b.l);
}

static inline u64
bitfield_get64(u64 v, u8 s, u8 b)
{
  return (v >> s) & ((1ULL << b) - 1);
}

static inline void
bitfield_set64(u64 *t, u64 v, u8 s, u8 b)
{
  u64 m = ((1ULL << b) - 1) << s;
  *t = (*t & ~m) | ((v << s) & m);
}

static inline u32
bitfield_get32(u32 v, int o, int l)
{
  return (v >> o) & ((1u << l) - 1);
}

static inline void
bitfield_set32(u32 *v, u32 x, int o, int l)
{
  u32 m = ((1u << l) - 1) << o;
  *v = (*v & ~m) | ((x << o) & m);
}

static inline void
bitfield_set8(u8 *t, u8 v, u8 s, u8 b)
{
  u8 m = ((1ULL << b) - 1) << s;
  *t = (*t & ~m) | ((v << s) & m);
}

static inline cut
get_env(const char *e, const char *f)
{
  const char *p = getenv(e);
  cut r;
  r.d = p && *p ? p : f;
  r.l = r.d ? strlen(r.d) : 0;
  return r;
}

static inline int
write_all(int fd, const char *b, usize l)
{
  for (usize o = 0; o < l; ) {
    ssize_t r = write(fd, b + o, l - o);
    if (r > 0) {
      o += (usize) r;
      continue;
    }
    if (r == -1 && errno == EINTR)
      continue;
    return -1;
  }
  return 0;
}

static inline int
run_cmd(int tty, int in, const char *d, const char *const a[], bool bg)
{
  extern char **environ;
  posix_spawn_file_actions_t fa;
  pid_t pid;
  int rc;
  int st;
  pid_t r;
  rc = posix_spawn_file_actions_init(&fa);
  if (rc) { errno = rc; return -1; }
  if (in >= 0) {
    rc = posix_spawn_file_actions_adddup2(&fa, in, 0);
    if (rc) goto fail_fa;
  }
  if (tty >= 0) {
    if ((rc = posix_spawn_file_actions_adddup2(&fa, tty, 1)) ||
        (rc = posix_spawn_file_actions_adddup2(&fa, tty, 2)))
      goto fail_fa;
  }
  if (d) {
#if defined(_POSIX_VERSION) && _POSIX_VERSION >= 202405L
    rc = posix_spawn_file_actions_addchdir(&fa, d);
#elif defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
    rc = posix_spawn_file_actions_addchdir_np(&fa, d);
#else
    posix_spawn_file_actions_destroy(&fa);
    pid = fork();
    if (pid == -1) return -1;
    if (!pid) {
      if (in >= 0) {
        if (dup2(in, 0) == -1) _exit(127);
        if (in != 0) close(in);
      }
      if (tty >= 0) {
        if (dup2(tty, 1) == -1) _exit(127);
        if (dup2(tty, 2) == -1) _exit(127);
        if (tty != 1 && tty != 2) close(tty);
      }
      if (d && chdir(d) == -1)
        _exit(127);
      execvp(a[0], (char *const *)a);
      _exit(127);
    }
    goto end;
#endif
    if (rc) goto fail_fa;
  }
  rc = posix_spawnp(&pid, a[0], &fa, NULL, (char *const *)a, environ);
  posix_spawn_file_actions_destroy(&fa);
  if (rc) { errno = rc; return -1; }
  goto end; // Silence compiler warning when ifdefs cause no jump.
end:
  if (bg) return 0;
  do r = waitpid(pid, &st, 0);
  while (r == -1 && errno == EINTR);
  return r == -1 ? -1 : st;
fail_fa:
  posix_spawn_file_actions_destroy(&fa);
  errno = rc;
  return -1;
}

static inline int
fd_from_buf(const char *b, usize l)
{
  int fd[2];
  if (pipe(fd)) return -1;
  if (l > PIPE_BUF) {
#ifdef F_GETPIPE_SZ
  int c = fcntl(fd[1], F_GETPIPE_SZ);
  if (c < 0 || l > (usize)c) {
    close(fd[0]);
    close(fd[1]);
    return -1;
  }
#else
  close(fd[0]);
  close(fd[1]);
  return -1;
#endif
  }
  ssize_t w = write(fd[1], b, l);
  close(fd[1]);
  if (w < 0 || (usize)w != l) {
    close(fd[0]);
    return -1;
  }
  return fd[0];
}

static inline usize
u64_popcount(u64 x)
{
#if defined(__GNUC__) || defined(__clang__)
  return (usize) __builtin_popcountll(x);
#else
  usize c = 0;
  for (; x; x &= x - 1) c++;
  return c;
#endif
}

static inline u64
u64_ctz(u64 x)
{
#if defined(__GNUC__) || defined(__clang__)
  return (u64)__builtin_ctzll(x);
#else
  u64 i = 0;
  while (!(x & 1ull)) { x >>= 1; i++; }
  return i;
#endif
}

static inline u64
u64_clz(u64 x)
{
#if defined(__GNUC__) || defined(__clang__)
  return (u64)__builtin_clzll(x);
#else
  u64 i = 0;
  for (u64 m = 1ull << 63; !(x & m); m >>= 1) i++;
  return i;
#endif
}

static inline u32
hash_fnv1a32(const char *d, usize l)
{
  u32 h = 2166136261u;
  for (usize i = 0; i < l; i++)
    h = (h ^ (unsigned char)d[i]) * 16777619u;
  return h | 1;
}

static inline s64
tz_offset(void)
{
  time_t n = time(NULL);
  struct tm lt;
  struct tm gt;
  if (!localtime_r(&n, &lt)) return 0;
  if (!gmtime_r(&n, &gt)) return 0;
  time_t lo = mktime(&lt);
  time_t gm = mktime(&gt);
  if (lo == (time_t)-1 || gm == (time_t)-1) return 0;
  return (s64)(lo - gm);
}

static inline usize
path_resolve(char *s, usize l)
{
  char *m = s;
  usize i = 0, w = 0;
  while (i < l) {
    while (i < l && m[i] == '/') i++;
    if (i >= l) break;
    usize b = i;
    while (i < l && m[i] != '/') i++;
    usize n = i - b;
    if (n == 1 && m[b] == '.')
      continue;
    if (n == 2 && m[b] == '.' && m[b + 1] == '.') {
      if (w > 1) {
        if (m[w - 1] == '/') w--;
        while (w > 1 && m[w - 1] != '/') w--;
      }
      continue;
    }
    if (w == 0 || m[w - 1] != '/')
      m[w++] = '/';
    if (w != b) memmove(m + w, m + b, n);
    w += n;
  }
  if (w > 1 && m[w - 1] == '/') w--;
  if (!w) m[w++] = '/';
  m[w] = 0;
  return w;
}

#endif // DYLAN_UTIL_H

