#!/bin/sh
# Build smashhit_nx.nro (the launcher) with the runtime's launcher build
# (devkitPro's 64-bit toolchain container). Build the wrapper first
# (../build.sh): the NRO carries ../smashhit_nx.nsp and ../smashhit_nx.build.
HERE="$(cd "$(dirname "$0")" && pwd)"
LAUNCHER_DIR="$HERE" PAYLOAD=smashhit_nx exec "$HERE/../runtime/launcher/build.sh" "$@"
