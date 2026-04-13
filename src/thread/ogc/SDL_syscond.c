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

#include <errno.h>
#include <ogc/mutex.h>
#include <ogc/cond.h>
#include <ogcsys.h>

#include "SDL_sysmutex_c.h"

struct SDL_Condition
{
    cond_t cond;
};

/* Create a condition variable */
SDL_Condition *SDL_CreateCondition(void)
{
    SDL_Condition *cond;

    cond = (SDL_Condition *)SDL_calloc(1, sizeof(*cond));
    if (cond) {
        if (LWP_CondInit(&cond->cond) != 0) {
            SDL_SetError("LWP_CondInit() failed");
            SDL_free(cond);
            cond = NULL;
        }
    }
    return cond;
}

/* Destroy a condition variable */
void SDL_DestroyCondition(SDL_Condition *cond)
{
    if (cond) {
        LWP_CondDestroy(cond->cond);
        SDL_free(cond);
    }
}

/* Restart one of the threads that are waiting on the condition variable */
void SDL_SignalCondition(SDL_Condition *cond)
{
    if (cond) {
        const int rc = LWP_CondSignal(cond->cond);
        SDL_assert(rc == 0);
        (void)rc;
    }
}

/* Restart all threads that are waiting on the condition variable */
void SDL_BroadcastCondition(SDL_Condition *cond)
{
    if (cond) {
        const int rc = LWP_CondBroadcast(cond->cond);
        SDL_assert(rc == 0);
        (void)rc;
    }
}

/* Wait on the condition variable for at most 'timeoutNS' nanoseconds.
   The mutex must be locked before entering this function!
   The mutex is unlocked during the wait, and locked again after the wait.

Typical use:

Thread A:
    SDL_LockMutex(lock);
    while ( ! condition ) {
        SDL_WaitCondition(cond, lock);
    }
    SDL_UnlockMutex(lock);

Thread B:
    SDL_LockMutex(lock);
    ...
    condition = true;
    ...
    SDL_SignalCondition(cond);
    SDL_UnlockMutex(lock);
 */
bool SDL_WaitConditionTimeoutNS(SDL_Condition *cond, SDL_Mutex *mutex, Sint64 timeoutNS)
{
    bool retval = true;
    struct timespec tv;

    if (!cond) {
        return SDL_InvalidParamError("cond");
    }
    if (!mutex) {
        return SDL_InvalidParamError("mutex");
    }

    if (timeoutNS < 0) {
        /* Wait indefinitely */
        if (LWP_CondWait(cond->cond, mutex->id) != 0) {
            retval = false;
        }
    } else {
        /* Convert nanoseconds to timespec */
        Sint64 ms = timeoutNS / 1000000LL;
        tv.tv_sec = (long)(ms / TB_MSPERSEC);
        tv.tv_nsec = (long)((ms % TB_MSPERSEC) * TB_NSPERMS);

        const int rc = LWP_CondTimedWait(cond->cond, mutex->id, &tv);
        if (rc != 0) {
            retval = (rc == ETIMEDOUT) ? false : false;
        }
    }
    return retval;
}

/* vi: set ts=4 sw=4 expandtab: */
