/* config.h -- Smash Hit's own constants (the runtime's settings are in
 * port_config.h).
 *
 * Smash Hit (com.mediocre.smashhit 1.5.14, Mediocre), armeabi-v7a: one
 * native module the port loads, libsmashhit.so (the engine, the game, Lua,
 * the image and sound decoders, Google's GameActivity glue and libc++, all
 * linked in; GLES 2). MIT.
 */
#ifndef SH_CONFIG_H
#define SH_CONFIG_H

#include "rt_settings.h"

#define SH_LIB     "libsmashhit.so"
#define SH_PACKAGE PORT_PACKAGE

#endif /* SH_CONFIG_H */
