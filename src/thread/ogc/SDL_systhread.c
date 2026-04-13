/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_internal.h"

#ifdef SDL_THREAD_OGC

#include <ogc/lwp.h>

#include "../SDL_thread_c.h"
#include "../SDL_systhread.h"

/* Current libogc only defines LWP_PRIO_IDLE (0) and LWP_PRIO_HIGHEST (127). */
#ifndef LWP_PRIO_LOWEST
#define LWP_PRIO_LOWEST        1
#endif
#ifndef LWP_PRIO_NORMAL
#define LWP_PRIO_NORMAL        64
#endif
#ifndef LWP_PRIO_TIME_CRITICAL
#define LWP_PRIO_TIME_CRITICAL LWP_PRIO_HIGHEST
#endif

static void *RunThread(void *data)
{
    SDL_RunThread((SDL_Thread *)data);
    return NULL;
}

bool SDL_SYS_CreateThread(SDL_Thread *thread,
                          SDL_FunctionPointer pfnBeginThread,
                          SDL_FunctionPointer pfnEndThread)
{
    int priority = LWP_PRIO_NORMAL;

    /* pfnBeginThread and pfnEndThread are not used on OGC */
    (void)pfnBeginThread;
    (void)pfnEndThread;

    /* Create the thread and go! */
    if (LWP_CreateThread(&thread->handle, RunThread, thread, NULL, thread->stacksize, priority) != 0) {
        return SDL_SetError("Not enough resources to create thread");
    }
    return true;
}

void SDL_SYS_SetupThread(const char *name)
{
    return;
}

SDL_ThreadID SDL_GetCurrentThreadID(void)
{
    return (SDL_ThreadID)LWP_GetSelf();
}

bool SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority)
{
    int value;

    /* Range is 1 (lowest) to 127 (highest) */
    if (priority == SDL_THREAD_PRIORITY_LOW) {
        value = LWP_PRIO_LOWEST;
    } else if (priority == SDL_THREAD_PRIORITY_HIGH) {
        value = LWP_PRIO_HIGHEST;
    } else if (priority == SDL_THREAD_PRIORITY_TIME_CRITICAL) {
        value = LWP_PRIO_TIME_CRITICAL;
    } else {
        value = LWP_PRIO_NORMAL;
    }
    LWP_SetThreadPriority(LWP_THREAD_NULL, value);
    return true;
}

void SDL_SYS_WaitThread(SDL_Thread *thread)
{
    LWP_JoinThread(thread->handle, NULL);
}

void SDL_SYS_DetachThread(SDL_Thread *thread)
{
    (void)thread; /* LWP_DetachThread not available in libogc; thread runs to completion */
}

#endif /* SDL_THREAD_OGC */

/* vi: set ts=4 sw=4 expandtab: */
