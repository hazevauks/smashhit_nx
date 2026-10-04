#---------------------------------------------------------------------------------
# Smash Hit -- Nintendo Switch wrapper (32-bit / AArch32)
#
# Ships NO game code and NO game assets: the game's own APK (the user's copy,
# any file name) is read at run time; its engine is unpacked from it on the
# first launch, its assets are read from the APK itself.
#
# The build is the android32 runtime's (runtime/runtime.mk: devkitARM +
# libnx32 + mesa32 from portlibs32/); ./build.sh runs it in the toolchain
# container. Output: smashhit_nx.nsp, which the launcher NRO carries
# (launcher/).
#---------------------------------------------------------------------------------
TARGET               := smashhit_nx
PORT_NPDM_PROGRAM_ID := 0x01000000000010E4
include runtime/runtime.mk

# The runtime's OpenSL ES audio thread, with the stack the engine's mixing
# callback needs (source/sh_audio.c).
$(BUILD)/rt/opensles.o: CFLAGS += -DthreadCreate=sh_audio_thread_create
