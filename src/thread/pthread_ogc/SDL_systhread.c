/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

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

/* OGC (Wii/GameCube) fork of the pthread thread backend.
 * libogc2's pthread implementation does not support scheduler-related APIs
 * (pthread_getschedparam, pthread_setschedparam, sched_get_priority_*), so
 * SDL_SYS_SetThreadPriority is stubbed out here. Everything else is standard
 * pthread and delegates to the same logic as the generic pthread backend. */

#include "SDL_internal.h"

#include <pthread.h>
#include <errno.h>
#include <tuxedo/thread.h>

#include "../SDL_thread_c.h"
#include "../SDL_systhread.h"

static void *RunThread(void *data)
{
    SDL_RunThread((SDL_Thread *)data);
    return NULL;
}

bool SDL_SYS_CreateThread(SDL_Thread *thread,
                          SDL_FunctionPointer pfnBeginThread,
                          SDL_FunctionPointer pfnEndThread)
{
    pthread_attr_t type;

    if (pthread_attr_init(&type) != 0) {
        return SDL_SetError("Couldn't initialize pthread attributes");
    }
    pthread_attr_setdetachstate(&type, PTHREAD_CREATE_JOINABLE);

    if (thread->stacksize) {
        pthread_attr_setstacksize(&type, thread->stacksize);
    }

    if (pthread_create(&thread->handle, &type, RunThread, thread) != 0) {
        return SDL_SetError("Not enough resources to create thread");
    }

    return true;
}

void SDL_SYS_SetupThread(const char *name)
{
#ifdef HAVE_PTHREAD_SETNAME_NP
    if (name) {
        if (pthread_setname_np(pthread_self(), name) == ERANGE) {
            char namebuf[16];
            SDL_strlcpy(namebuf, name, sizeof(namebuf));
            pthread_setname_np(pthread_self(), namebuf);
        }
    }
#endif
}

SDL_ThreadID SDL_GetCurrentThreadID(void)
{
    return (SDL_ThreadID)(uintptr_t)pthread_self();
}

bool SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority)
{
    /* libogc2 pthread does not expose scheduler parameter APIs, but we can
     * reach the underlying KThread directly via KThreadGetSelf() (reads SPRG2)
     * and adjust its priority with KThreadSetPrio().
     *
     * KThread priority scale: KTHR_MAX_PRIO=0 (highest) .. KTHR_MAIN_PRIO=63
     * .. KTHR_MIN_PRIO=127 (lowest).  We map SDL's four levels onto that range,
     * keeping them safely above the hardware-interrupt priority band (< 16). */
    u16 prio;
    switch (priority) {
    case SDL_THREAD_PRIORITY_LOW:           prio = 80; break;
    case SDL_THREAD_PRIORITY_NORMAL:        prio = KTHR_MAIN_PRIO; break;
    case SDL_THREAD_PRIORITY_HIGH:          prio = 32; break;
    case SDL_THREAD_PRIORITY_TIME_CRITICAL: prio = 16; break;
    default:                                prio = KTHR_MAIN_PRIO; break;
    }
    KThreadSetPrio(KThreadGetSelf(), prio);
    return true;
}

void SDL_SYS_WaitThread(SDL_Thread *thread)
{
    pthread_join(thread->handle, NULL);
}

void SDL_SYS_DetachThread(SDL_Thread *thread)
{
    pthread_detach(thread->handle);
}
