# SDL3-libogc2

This is a fork of [SDL3](https://github.com/libsdl-org/SDL) adding support for the **Nintendo Wii** and **Nintendo GameCube** homebrew platforms via [libogc2](https://github.com/extremscorner/libogc2) and the [devkitPPC](https://devkitpro.org/) toolchain.

Actually, this is a fork of a fork, majorly [implemented by leap0x7b](https://github.com/leap0x7b/SDL3-libogc2), and made to work with devkitPPC 15.2.0 and libogc 3.0.4-1.

The changes made were in order to use the latest toolchain and libs, and to uniformize threading code around the (new?) pthreads support.

One missing bit is to give the audio thread higher priority (this does not seem to be possible yet with the pthreads implementation by itself).

## Overview

The port targets the `OGC` platform (the CMake variable `OGC` is set by the devkitPPC toolchain). Both Wii and GameCube are supported; the CMake variable `CMAKE_SYSTEM_NAME` distinguishes between `NintendoWii` and `NintendoGameCube`.

### What is implemented

| Subsystem | Backend | Notes |
|-----------|---------|-------|
| Video | GX (libogc2 GX API) | Framebuffer via `SDL_ogcframebuffer`, OpenGL via opengx |
| Render | Custom GX renderer | `src/render/ogc/` |
| Audio | AESND (DSP) | `src/audio/ogc/`, stereo PCM via DMA double-buffering |
| Events | WPAD + keyboard + mouse | Wiimote, Nunchuk, Classic Controller, USB keyboard/mouse |
| Joystick | Classic Controller | `src/joystick/ogc/` |
| Threading | pthreads (KThread) | libogc2 pthreads backed by KThread; see notes below |
| Filesystem | FAT (libfat) | `fatInitDefault()` called during platform init |
| Locale | POSIX | |
| Time | OGC time | `src/time/ogc/` |

### Platform initialisation

The Wii entry point (`src/main/wii/SDL_sysmain_runapp.c`) calls `SDL_RunApp`, which
handles all required hardware setup before user code runs:

- `L2Enhance()` / IOS reload if needed
- `WPAD_Init()` and WPAD data format / resolution setup
- `MOUSE_Init()`, `KEYBOARD_Init()`
- `fatInitDefault()`

The GameCube entry point (`src/main/gamecube/SDL_sysmain_runapp.c`) is similar but
omits WPAD.

### Threading notes

libogc2 provides two thread systems that coexist in the same binary:

- **LWP** — the original libogc thread API (`LWP_CreateThread`, `LWP_MutexInit`, …)
- **KThread** — the newer kernel thread layer, which backs libogc2's pthread implementation

This port uses **pthreads** for all SDL threading (`SDL_CreateThread`, `SDL_CreateMutex`,
`SDL_CreateSemaphore`, etc.). These map to KThread via the standard newlib syscall shims
(`__syscall_lock_acquire` → `KMutexLock`). The scheduler API
(`pthread_getschedparam` / `pthread_setschedparam`) is not available, so
`SDL_SYS_SetThreadPriority` is a no-op (see `src/thread/pthread_ogc/SDL_systhread.c`).

Do **not** mix SDL synchronisation primitives with raw LWP calls on the same shared
data — they use different underlying locks.

### Building

Requirements: devkitPro with devkitPPC and libogc2 installed.

```sh
# Wii
cmake -B build-wii --toolchain /opt/devkitpro/devkitPPC/cmake/Wii.cmake .
cmake --build build-wii

# GameCube
cmake -B build-gc --toolchain /opt/devkitpro/devkitPPC/cmake/GameCube.cmake .
cmake --build build-gc
```

## Branch structure

| Branch | Description |
|--------|-------------|
| `release-3.4.x-libogc2` | Base OGC port, tracking upstream SDL `release-3.4.x` |
| `fixes` | Additional fixes on top of the base port (see below) |

### Changes in `fixes` relative to `release-3.4.x-libogc2`

- **`[sysmain]`** fix reset callback type
- **`[audio]`** fix audio callback type
- **`[sys]`** fix threading and timing (use LWP where appropriate for system-level code)
- **`[mouse]`** skip opengx mouse code when OpenGL is disabled
- **`[mouse/video]`** guard opengx mouse code; fall back to a standard video mode when preferred mode detection fails
- **`[thread]`** switch OGC threading backend from LWP to pthreads; fix audio driver to use SDL mutex/semaphore primitives; fix `SDL_runapp.c` generic fallback overriding the Wii-specific `SDL_RunApp` (which meant `WPAD_Init` was never called)

---

<!-- Original SDL README below -->

Simple DirectMedia Layer (SDL for short) is a cross-platform library
designed to make it easy to write multi-media software, such as games
and emulators.

You can find the latest release and additional information at:
https://www.libsdl.org/

Installation instructions and a quick introduction is available in
[INSTALL.md](INSTALL.md)

This library is distributed under the terms of the zlib license,
available in [LICENSE.txt](LICENSE.txt).

Enjoy!

Sam Lantinga (slouken@libsdl.org)
