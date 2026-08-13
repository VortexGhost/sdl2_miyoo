#!/bin/bash
set -e
REPO=/workspace/sdl2
CC=/opt/miyoomini-toolchain/bin/arm-linux-gnueabihf-gcc
OBJDIR=$REPO/build_obj
mkdir -p "$OBJDIR"
: > "$OBJDIR/../compile_errors.log"

# Bring in the external headers this fork expects to find alongside its own
# (SDL_image.h, SDL_ttf.h) without exposing the rest of /usr/include (which
# pollutes cpuinfo's <elf.h> with the host's glibc elf.h instead of the
# target sysroot's).
cp -n /usr/include/SDL2/SDL_image.h "$REPO/include/" 2>/dev/null || true
cp -n /usr/include/SDL2/SDL_ttf.h "$REPO/include/" 2>/dev/null || true
ln -sfn /usr/include/json-c "$REPO/include/json-c"

# Self-referencing symlink so this fork's own (newer) "SDL2/SDL.h"-style
# includes resolve here instead of accidentally picking up armhf apt's older
# SDL2 2.0.9 headers via -I/usr/include - mixing the two produces duplicate/
# conflicting type definitions.
ln -sfn . "$REPO/include/SDL2"

CFLAGS="-I$REPO/include -idirafter $REPO/src/video/khronos \
  -Wall -fno-strict-aliasing -fPIC -mcpu=cortex-a7 -mfpu=neon-vfpv4 -O3 \
  -fvisibility=hidden -Wdeclaration-after-statement -Werror=declaration-after-statement \
  -DMESA_EGL_NO_X11_HEADERS -DEGL_NO_X11 -D_REENTRANT -DHAVE_LINUX_VERSION_H -DMMIYOO"

echo "Compiling $(wc -l < $REPO/sdl_sources.txt) source files..."
FAIL=0
while read -r src; do
  rel=${src#/root/workspace/sdl2-moonlight-miyoo/}
  realsrc="$REPO/$rel"
  obj="$OBJDIR/$(echo "$rel" | tr '/' '_').o"
  if [ ! -f "$realsrc" ]; then
    echo "MISSING SOURCE: $realsrc"
    FAIL=1
    continue
  fi
  if ! "$CC" $CFLAGS -c "$realsrc" -o "$obj" 2>>"$OBJDIR/../compile_errors.log"; then
    echo "COMPILE FAILED: $realsrc"
    FAIL=1
  fi
done < "$REPO/sdl_sources.txt"

echo "Object files: $(ls $OBJDIR/*.o 2>/dev/null | wc -l)"
if [ "$FAIL" = "1" ]; then
  echo "=== last 100 lines of compile errors ==="
  tail -100 "$OBJDIR/../compile_errors.log"
  exit 1
fi

echo "Linking libSDL2-2.0.so.0..."
# -L/workspace/stage/lib comes first so -ljson-c resolves to the device's real
# libjson-c.so.5 (staged there) instead of the container's buster libjson-c.so.3
# - the device doesn't have .so.3 anywhere, only .so.5, so linking against the
# container's older json-c produced a binary that failed to load on-device.
"$CC" -shared -Wl,-soname,libSDL2-2.0.so.0 -o "$REPO/libSDL2-2.0.so.0" \
  "$OBJDIR"/*.o \
  -L/workspace/stage/lib -L/usr/lib/arm-linux-gnueabihf -L"$REPO/mmiyoo/libs" \
  -lm -lEGL -lGLESv2 -lrt -lasound -ljson-c -ldl -lpthread \
  -lmi_ao -lshmvar -lmi_common -lmi_sys -lmi_gfx \
  -Wl,--allow-shlib-undefined
echo "DONE"
file "$REPO/libSDL2-2.0.so.0"
