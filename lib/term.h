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
#ifndef DYLAN_TERM_H
#define DYLAN_TERM_H

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "util.h"
#include "vt.h"

enum {
  TERM_LOADED = 1 << 0,
  TERM_RESIZE = 1 << 1,
};

static struct term {
  struct termios o;
  int fd;
  int null;
  volatile sig_atomic_t flag;
  volatile sig_atomic_t dead;
} *TERM;

static inline void
term_set_dead(struct term *t, int s)
{
  t->dead = 128 + s;
}

static inline int
term_dead(const struct term *t)
{
  return t->dead;
}

static inline int
term_resize(const struct term *t)
{
  return t->flag & TERM_RESIZE;
}

static inline void
term_restore_on_signal(int s)
{
  if (!TERM) return;
  if (!(TERM->flag & TERM_LOADED)) return;
  term_set_dead(TERM, s);
  tcsetattr(TERM->fd, TCSAFLUSH, &TERM->o);

  //
  // TODO: Unhardcode this.
  //
#define TERM_COOKED \
  S(VT_ED0 VT_BPASTE_OFF VT_DECAWM_Y VT_DECTCEM_Y VT_ALT_SCREEN_N)
  write_all(TERM->fd, TERM_COOKED);
  write_all(STDOUT_FILENO, TERM_COOKED);
}

static inline void
term_signal_fatal(int s)
{
  term_restore_on_signal(s);
  _exit(128 + s);
}

static inline void
term_signal_crash(int s)
{
  term_restore_on_signal(s);
  struct sigaction sa = {0};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(s, &sa, NULL);
  kill(getpid(), s);
}

static inline void
term_signal_sigwinch(int s)
{
  (void) s;
  if (TERM) TERM->flag |= TERM_RESIZE;
}

static inline void
term_signal_setup(void) {
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sa.sa_handler = term_signal_fatal;
  sigaction(SIGINT,  &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGQUIT, &sa, NULL);
  sa.sa_handler = term_signal_crash;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
  sigaction(SIGBUS,  &sa, NULL);
  sigaction(SIGFPE,  &sa, NULL);
  sigaction(SIGILL,  &sa, NULL);
  sa.sa_handler = term_signal_sigwinch;
  sigaction(SIGWINCH, &sa, NULL);
}

static inline int
term_size_update(struct term *t, u16 *row, u16 *col)
{
  struct winsize ws;
  if (ioctl(t->fd, TIOCGWINSZ, &ws) < 0)
    return -1;
  t->flag &= ~TERM_RESIZE;
  *row = ws.ws_row;
  *col = ws.ws_col;
  return 0;
}

static inline int
term_init_io(struct term *t)
{
  if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))
    t->fd = STDIN_FILENO;
  else if (isatty(STDIN_FILENO)) {
    t->fd = open("/dev/tty", O_RDWR|O_CLOEXEC);
    if (t->fd < 0) return -1;
  } else {
    t->fd = -1;
    return -1;
  }
  t->null = open("/dev/null", O_WRONLY|O_CLOEXEC);
  return t->null;
}

static inline int
term_raw(const struct term *t)
{
  struct termios n = t->o;
  n.c_iflag &= ~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);
  n.c_oflag &= ~(OPOST);
  n.c_cflag |= (CS8);
  n.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
  n.c_cc[VMIN] = 1;
  n.c_cc[VTIME] = 0;
  return tcsetattr(t->fd, TCSAFLUSH, &n);
}

static inline int
term_cooked(struct term *t)
{
  assert(t->flag & TERM_LOADED);
  return tcsetattr(t->fd, TCSAFLUSH, &t->o);
}

static inline int
term_init(struct term *t)
{
  if (term_init_io(t) < 0) return -1;
  if (tcgetattr(t->fd, &t->o) < 0) return -1;
  TERM = t;
  t->flag |= TERM_LOADED;
  term_signal_setup();
  return 0;
}

static inline void
term_reap(void)
{
  for (int st; waitpid(-1, &st, WNOHANG) > 0; );
}

static inline void
term_destroy(const struct term *t)
{
  if (t->fd >= 0)   close(t->fd);
  if (t->null >= 0) close(t->null);
  TERM = NULL;
}

#endif // DYLAN_TERM_H

