/* sh_audio.c -- the stack of the thread the engine mixes its sound on.
 *
 * The engine plays through OpenSL ES, which the runtime's opensles.c
 * implements on audout: its audio thread calls the engine's buffer-queue
 * callback, as Android's does. That callback is QiAudio::fillBuffer, which
 * keeps its mixing buffers on the stack: its first instructions take 128 KB
 * (sub sp, #0x20000), and what it calls (the mixer, the Vorbis decoder) goes
 * below that. Android's callback thread has a pthread's 1 MB; the runtime's
 * has 64 KB, enough for the engines it was written for. The first hardware
 * run ended there: a data abort at sp - 12, in the first push after the
 * callback's own frame.
 *
 * So opensles.c is built with threadCreate named sh_audio_thread_create (the
 * Makefile's rule for build/rt/opensles.o): the one thread it starts gets
 * Android's stack, everything else about it as the runtime asks. MIT.
 */
#include <switch.h>

#include "util.h"

#define SH_AUDIO_STACK 0x100000 /* 1 MB: a pthread's default on Android */

Result sh_audio_thread_create(Thread *t, ThreadFunc entry, void *arg, void *stack_mem, size_t stack_sz,
                              int prio, int cpuid) {
  (void)stack_mem;
  debugPrintf("[audio] the OpenSL thread: stack %u KB instead of %u KB\n",
              (unsigned)(SH_AUDIO_STACK >> 10), (unsigned)(stack_sz >> 10));
  return threadCreate(t, entry, arg, NULL, SH_AUDIO_STACK, prio, cpuid);
}
