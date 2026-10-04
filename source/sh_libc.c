/* sh_libc.c -- the few bionic functions libsmashhit.so imports that the
 * runtime's shims do not have (runtime/tools/gen_imports.py binds them by
 * their b_ names): the FORTIFY checks a current NDK compiles in, the stdio
 * streams as bionic exports them since Android 6, and two that only exist
 * to be called.
 *
 * The _chk functions take the destination's size as the compiler knew it
 * ((size_t)-1 when it did not): overrunning it is a bug in the game, and is
 * reported as one. MIT.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bionic.h"
#include "error.h"
#include "util.h"

char *b_fgets(char *s, int n, void *fp);
ssize_t b_read(int fd, void *buf, size_t n);
ssize_t b_write(int fd, const void *buf, size_t n);
ssize_t b_pread(int fd, void *buf, size_t n, b_off_t off);
b_off_t b_lseek(int fd, b_off_t off, int whence);
void b_abort(void) __attribute__((noreturn));

/* ------------------------------------------------------------ the streams */
/* FILE *stdin, *stdout, *stderr: variables that point into __sF (the
 * runtime's b___sF, which its stdio shims know). */
void *b_stdin = &b___sF[0];
void *b_stdout = &b___sF[B_FILE_SIZE];
void *b_stderr = &b___sF[2 * B_FILE_SIZE];

/* ---------------------------------------------------------------- FORTIFY */
static void overrun(const char *what, size_t n, size_t cap, void *from) {
  fatal_error("%s: %u bytes into %u (from %p)", what, (unsigned)n, (unsigned)cap, from);
}

char *b___fgets_chk(char *dst, int size, void *fp, size_t dst_len) {
  if (size < 0 || (size_t)size > dst_len)
    overrun("__fgets_chk", (size_t)size, dst_len, __builtin_return_address(0));
  return b_fgets(dst, size, fp);
}

ssize_t b___read_chk(int fd, void *buf, size_t count, size_t buf_size) {
  if (count > buf_size)
    overrun("__read_chk", count, buf_size, __builtin_return_address(0));
  return b_read(fd, buf, count);
}

ssize_t b___write_chk(int fd, const void *buf, size_t count, size_t buf_size) {
  if (count > buf_size)
    overrun("__write_chk", count, buf_size, __builtin_return_address(0));
  return b_write(fd, buf, count);
}

ssize_t b___pread_chk(int fd, void *buf, size_t count, b_off_t offset, size_t buf_size) {
  if (count > buf_size)
    overrun("__pread_chk", count, buf_size, __builtin_return_address(0));
  return b_pread(fd, buf, count, offset);
}

/* The runtime has no pwrite: the position kept around a seek and a write.
 * (Not atomic, as pwrite is; nothing in the game writes one file from two
 * threads.) */
ssize_t b___pwrite_chk(int fd, const void *buf, size_t count, b_off_t offset, size_t buf_size) {
  if (count > buf_size)
    overrun("__pwrite_chk", count, buf_size, __builtin_return_address(0));
  const b_off_t was = b_lseek(fd, 0, SEEK_CUR);
  if (was < 0 || b_lseek(fd, offset, SEEK_SET) < 0)
    return -1;
  const ssize_t r = b_write(fd, buf, count);
  b_lseek(fd, was, SEEK_SET);
  return r;
}

char *b___strchr_chk(const char *s, int c, size_t s_len) {
  (void)s_len;
  return strchr(s, c);
}

size_t b___strlen_chk(const char *s, size_t s_len) {
  const size_t n = strlen(s);
  if (n >= s_len)
    overrun("__strlen_chk", n + 1, s_len, __builtin_return_address(0));
  return n;
}

int b___vsprintf_chk(char *dst, int flags, size_t dst_len, const char *fmt, va_list ap) {
  (void)flags;
  /* an unknown size is (size_t)-1, more than vsnprintf takes */
  const size_t cap = dst_len > 0x7fffffffu ? 0x7fffffffu : dst_len;
  const int r = b_vsnprintf(dst, cap, fmt, ap);
  if (r >= 0 && (size_t)r >= cap)
    overrun("__vsprintf_chk", (size_t)r + 1, dst_len, __builtin_return_address(0));
  return r;
}

/* ------------------------------------------------------------------ others */
/* pthread_atfork, as libc++ registers it: this process never forks. */
int b___register_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void), void *dso) {
  (void)prepare, (void)parent, (void)child, (void)dso;
  return 0;
}

/* A failed assertion of the game's Android glue (GameActivity_register's
 * "!gInsetsClassInfo.left" and the like): the condition and the message into
 * the log, then the runtime's abort (which writes crash.log). */
void b___android_log_assert(const char *cond, const char *tag, const char *fmt, ...) {
  char msg[512] = "";
  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
  }
  debugPrintf("[libc] __android_log_assert(%s) %s: %s\n", cond ? cond : "", tag ? tag : "", msg);
  log_flush_ring();
  b_abort();
}
