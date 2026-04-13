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

#ifdef SDL_AUDIO_DRIVER_OGC

/* OGC Audio driver */

#include "../SDL_sysaudio.h"
#include "SDL_ogcaudio.h"

#include <malloc.h>

#define OGCAUDIO_DRIVER_NAME "ogc"

static bool FindAudioFormat(SDL_AudioDevice *device)
{
    bool found_valid_format = false;

    // Try to find a compatible format
    switch (device->spec.format) {
    case SDL_AUDIO_S8:
        device->hidden->format = VOICE_MONO8;
        device->hidden->bytes_per_sample = device->spec.channels;
        found_valid_format = true;
        break;
    case SDL_AUDIO_U8:
        device->hidden->format = VOICE_MONO8_UNSIGNED;
        device->hidden->bytes_per_sample = device->spec.channels;
        found_valid_format = true;
        break;
    case SDL_AUDIO_S16BE:
        device->hidden->format = VOICE_MONO16;
        device->hidden->bytes_per_sample = device->spec.channels * 2;
        found_valid_format = true;
        break;
    default:
        // Try to convert to a supported format
        device->spec.format = SDL_AUDIO_S16BE;
        device->hidden->format = VOICE_MONO16;
        device->hidden->bytes_per_sample = device->spec.channels * 2;
        found_valid_format = true;
        break;
    }

    if (found_valid_format && device->spec.channels == 2) {
        device->hidden->format++;
    }

    return found_valid_format;
}

/* fully local functions related to the wavebufs / DSP, not the same as the SDL-wide mixer lock */
static SDL_INLINE void contextLock(SDL_AudioDevice *device)
{
    LWP_MutexLock(device->hidden->lock);
}

static SDL_INLINE void contextUnlock(SDL_AudioDevice *device)
{
    LWP_MutexUnlock(device->hidden->lock);
}

static void audio_frame_finished(AESNDPB *pb, u32 state, void *cbArg)
{
    SDL_AudioDevice *device = (SDL_AudioDevice *)cbArg;

    if (state == VOICE_STATE_STREAM) {
        const size_t buffer_size = DMA_BUFFER_SIZE;
        s8 playing_buffer;
        void *buffer;

        /* Immediately send the next buffer to the DSP. It's important that
         * AESND_SetVoiceBuffer() gets called before this callback returns, or
         * some audio gaps might be audible. */
        contextLock(device);
        playing_buffer = (device->hidden->playing_buffer + 1) % NUM_BUFFERS;
        buffer = device->hidden->dma_buffers[playing_buffer];
        AESND_SetVoiceBuffer(pb, buffer, buffer_size);
        device->hidden->playing_buffer = playing_buffer;
        contextUnlock(device);

        /* If a frame has finished playing, it means that the corresponding
         * buffer is no longer in use and can be filled up again. We signal
         * this event to the audio thread via a semaphore. */
        LWP_SemPost(device->hidden->available_buffers);
    } else if (state == VOICE_STATE_STOPPED) {
        contextLock(device);
        device->hidden->playing_buffer = -1;
        contextUnlock(device);
    }
}

static bool OGCAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden =
        memalign(32, sizeof(struct SDL_PrivateAudioData)); // This should NOT be SDL_memalign()
                                                           // (SDL_memalign doesn't even exist anyway why does it even warn this???)
    if (!hidden) {
        return false;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO,
                 "OGCAUDIO_OpenDevice, freq=%d, channels=%d\n",
                 device->spec.freq, device->spec.channels);

    SDL_memset(hidden, 0, sizeof(*hidden));
    hidden->playing_buffer = -1;
    device->hidden = hidden;

    AESND_Init();
    AESND_Pause(true);

    /* Initialise internal state */
    LWP_MutexInit(&hidden->lock, false);
    /* We set the initial number of available buffers to NUM_BUFFERS - 1, since
     * SDL first calls GetDeviceBuf() and starts filling it without first
     * calling WaitDevice(). So we consider the first buffer to be busy already
     * at start. */
    LWP_SemInit(&hidden->available_buffers, NUM_BUFFERS - 1, NUM_BUFFERS);

    if (device->spec.freq <= 0 || device->spec.freq > 144000) {
        device->spec.freq = (int)DSP_DEFAULT_FREQ;
    }

    if (device->spec.channels > 2) {
        device->spec.channels = 2;
    }

    /* Should not happen but better be safe. */
    if (!FindAudioFormat(device)) {
        SDL_free(hidden);
        return SDL_SetError("No supported audio format found.");
    }

    device->sample_frames = DMA_BUFFER_SIZE / device->hidden->bytes_per_sample;

    /* Update the device format */
    SDL_UpdatedAudioDeviceFormat(device);

    hidden->voice = AESND_AllocateVoiceWithArg(audio_frame_finished, device);
    if (hidden->voice == NULL) {
        LWP_SemDestroy(hidden->available_buffers);
        SDL_free(hidden);
        return SDL_SetError("Could not allocate audio voice");
    }

    // start audio
    AESND_SetVoiceFormat(hidden->voice, hidden->format);
    AESND_SetVoiceFrequency(hidden->voice, device->spec.freq);
    AESND_SetVoiceBuffer(hidden->voice, hidden->dma_buffers[0], DMA_BUFFER_SIZE);
    AESND_SetVoiceStream(hidden->voice, true);
    AESND_SetVoiceStop(hidden->voice, false);
    AESND_Pause(false);

    return true;
}

static bool OGCAUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    void *dma_buffer;

    /* This only sends the first audio buffer. The following ones will always
     * be sent from the audio_frame_finished() callback, without having to
     * switch between threads. */
    contextLock(device);
    if (device->hidden->playing_buffer < 0) {
        dma_buffer = device->hidden->dma_buffers[++device->hidden->playing_buffer];
        AESND_SetVoiceBuffer(device->hidden->voice, dma_buffer, DMA_BUFFER_SIZE);
    }
    contextUnlock(device);

    return true;
}

static bool OGCAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    s8 nextbuf;

    /* This will block until at least one buffer is available for writing. */
    LWP_SemWait(device->hidden->available_buffers);

    nextbuf = device->hidden->nextbuf;
    device->hidden->nextbuf = (nextbuf + 1) % NUM_BUFFERS;

    return true;
}

static Uint8 *OGCAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    *buffer_size = device->buffer_size;
    return device->hidden->dma_buffers[device->hidden->nextbuf];
}

static void OGCAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    if (device->hidden) {
        struct SDL_PrivateAudioData *hidden = device->hidden;

        LWP_SemDestroy(hidden->available_buffers);
        if (hidden->voice) {
            AESND_SetVoiceStop(hidden->voice, true);
            AESND_FreeVoice(hidden->voice);
            hidden->voice = NULL;
        }

        AESND_Pause(true);
        SDL_free(device->hidden);
        device->hidden = NULL;
    }
}

static void OGCAUDIO_ThreadInit(SDL_AudioDevice *device)
{
    LWP_SetThreadPriority(LWP_THREAD_NULL, 80);
}

static bool OGCAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    /* Set the function pointers */
    impl->OpenDevice = OGCAUDIO_OpenDevice;
    impl->PlayDevice = OGCAUDIO_PlayDevice;
    impl->WaitDevice = OGCAUDIO_WaitDevice;
    impl->GetDeviceBuf = OGCAUDIO_GetDeviceBuf;
    impl->CloseDevice = OGCAUDIO_CloseDevice;
    impl->ThreadInit = OGCAUDIO_ThreadInit;
    impl->OnlyHasDefaultPlaybackDevice = true;

    return true; /* this audio target is available. */
}

AudioBootStrap OGCAUDIO_bootstrap = {
    OGCAUDIO_DRIVER_NAME, "SDL OGC audio driver", OGCAUDIO_Init, false
};

#endif /* SDL_AUDIO_DRIVER_OGC */

/* vi: set sts=4 ts=4 sw=4 expandtab: */
