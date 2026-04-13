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
#include <unistd.h>
#include <ogc/semaphore.h>
#include <ogc/lwp_watchdog.h>
#include <ogcsys.h>

/* LWP_SemTryWait / LWP_SemTimedWait are not present in current libogc.
   Implement them using LWP_SemGetValue + LWP_SemWait. */

static int ogc_sem_trywait(sem_t sem)
{
    u32 val;
    if (LWP_SemGetValue(sem, &val) != 0 || val == 0) {
        return EAGAIN;
    }
    return LWP_SemWait(sem);
}

static int ogc_sem_timedwait(sem_t sem, const struct timespec *tv)
{
    const u64 timeout_ns = (u64)tv->tv_sec * TB_NSPERSEC + (u64)tv->tv_nsec;
    const u64 deadline   = ticks_to_nanosecs(gettime()) + timeout_ns;

    while (1) {
        if (ogc_sem_trywait(sem) == 0) {
            return 0;
        }
        if (ticks_to_nanosecs(gettime()) >= deadline) {
            return ETIMEDOUT;
        }
        usleep(500);
    }
}

struct SDL_Semaphore
{
    sem_t sem;
};

/* Create a semaphore, initialized with value */
SDL_Semaphore *SDL_CreateSemaphore(Uint32 initial_value)
{
    SDL_Semaphore *sem;

    sem = (SDL_Semaphore *)SDL_calloc(1, sizeof(*sem));
    if (sem) {
        if (LWP_SemInit(&sem->sem, initial_value, SDL_MAX_UINT32) != 0) {
            SDL_SetError("LWP_SemInit() failed");
            SDL_free(sem);
            sem = NULL;
        }
    }
    return sem;
}

/* Destroy a semaphore */
void SDL_DestroySemaphore(SDL_Semaphore *sem)
{
    if (sem) {
        LWP_SemDestroy(sem->sem);
        SDL_free(sem);
    }
}

/* Wait on the semaphore, returning true if successful, or false if timed out */
bool SDL_WaitSemaphoreTimeoutNS(SDL_Semaphore *sem, Sint64 timeoutNS)
{
    bool retval = true;

    if (!sem) {
        SDL_InvalidParamError("sem");
        return false;
    }

    if (timeoutNS == 0) {
        /* Poll - try to acquire without blocking */
        const int rc = ogc_sem_trywait(sem->sem);
        if (rc != 0) {
            retval = false;
        }
    } else if (timeoutNS < 0) {
        /* Wait indefinitely */
        const int rc = LWP_SemWait(sem->sem);
        if (rc != 0) {
            SDL_SetError("LWP_SemWait() failed");
            retval = false;
        }
    } else {
        /* Wait with timeout */
        struct timespec tv;
        Sint64 ms = timeoutNS / 1000000LL;

        tv.tv_sec = (long)(ms / TB_MSPERSEC);
        tv.tv_nsec = (long)((ms % TB_MSPERSEC) * TB_NSPERMS);

        const int rc = ogc_sem_timedwait(sem->sem, &tv);
        if (rc != 0) {
            if (rc == ETIMEDOUT) {
                retval = false;
            } else {
                SDL_SetError("LWP_SemTimedWait() failed");
                retval = false;
            }
        }
    }
    return retval;
}

/* NOTE:
 * The simple non-timeout wrappers `SDL_WaitSemaphore` and `SDL_TryWaitSemaphore`
 * are intentionally not defined in this OGC platform-specific file. The SDL
 * core provides the non-timeout wrappers and defining them here would produce
 * duplicate-symbol link errors. Use the timeout-capable function above from
 * platform-independent code, or let the core's wrappers call this function.
 */

/* Returns the current count of the semaphore */
Uint32 SDL_GetSemaphoreValue(SDL_Semaphore *sem)
{
    Uint32 value = 0;

    if (sem) {
        LWP_SemGetValue(sem->sem, &value);
    }
    return value;
}

/* Atomically increases the semaphore's count (not blocking) */
void SDL_SignalSemaphore(SDL_Semaphore *sem)
{
    if (sem) {
        const int rc = LWP_SemPost(sem->sem);
        SDL_assert(rc == 0);
        (void)rc;
    }
}

/* vi: set ts=4 sw=4 expandtab: */
