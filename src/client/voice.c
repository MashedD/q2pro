/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "client.h"
#include "sound/qal.h"

#include <opus/opus.h>

#define VOICE_QUEUE_PACKETS 16
#define VOICE_CAPTURE_BUFFER_SAMPLES (VOICE_FRAME_SAMPLES * 8)

typedef struct {
    byte seq;
    int len;
    byte data[VOICE_MAX_BYTES];
} voice_packet_t;

static cvar_t *voice_enable;
static cvar_t *voice_gain;
static cvar_t *voice_capture_device;
static cvar_t *voice_show;

static OpusEncoder *voice_encoder;
static OpusDecoder *voice_decoder;
static ALCdevice *voice_capture;

static bool voice_pressed;
static bool voice_capture_started;
static bool voice_warned_protocol;
static byte voice_sequence;

static int16_t voice_capture_buffer[VOICE_FRAME_SAMPLES];
static voice_packet_t voice_queue[VOICE_QUEUE_PACKETS];
static int voice_queue_head;
static int voice_queue_tail;
static int voice_queue_count;

static bool voice_protocol_ok(void)
{
    return cls.state == ca_active && !cls.demo.playback &&
           cls.serverProtocol == PROTOCOL_VERSION_Q2PRO &&
           cls.protocolVersion >= PROTOCOL_VERSION_Q2PRO_VOICE;
}

static void voice_queue_packet(const byte *data, int len)
{
    voice_packet_t *packet;

    if (len <= 0 || len > VOICE_MAX_BYTES) {
        return;
    }

    if (voice_queue_count == VOICE_QUEUE_PACKETS) {
        voice_queue_tail = (voice_queue_tail + 1) % VOICE_QUEUE_PACKETS;
        voice_queue_count--;
    }

    packet = &voice_queue[voice_queue_head];
    packet->seq = voice_sequence++;
    packet->len = len;
    memcpy(packet->data, data, len);

    voice_queue_head = (voice_queue_head + 1) % VOICE_QUEUE_PACKETS;
    voice_queue_count++;
}

static void voice_stop_capture(void)
{
    if (!voice_capture) {
        return;
    }

    if (voice_capture_started) {
        qalcCaptureStop(voice_capture);
        voice_capture_started = false;
    }

    qalcCaptureCloseDevice(voice_capture);
    voice_capture = NULL;
}

static bool voice_start_capture(void)
{
    const char *device;

    if (voice_capture) {
        return true;
    }

    if (!qalcCaptureOpenDevice) {
        if (voice_show->integer) {
            Com_Printf("OpenAL capture is not available.\n");
        }
        return false;
    }

    device = voice_capture_device->string[0] ? voice_capture_device->string : NULL;
    voice_capture = qalcCaptureOpenDevice(device, VOICE_RATE, AL_FORMAT_MONO16,
                                          VOICE_CAPTURE_BUFFER_SAMPLES);
    if (!voice_capture) {
        Com_WPrintf("Failed to open voice capture device.\n");
        return false;
    }

    qalcCaptureStart(voice_capture);
    voice_capture_started = true;

    if (voice_show->integer) {
        Com_Printf("Voice capture started.\n");
    }

    return true;
}

void CL_VoiceDown(void)
{
    voice_pressed = true;

    if (voice_protocol_ok()) {
        cl.sendPacketNow = true;
    } else if (!voice_warned_protocol && cls.state == ca_active) {
        Com_WPrintf("Voice requires a Q2PRO voice-capable server.\n");
        voice_warned_protocol = true;
    }
}

void CL_VoiceUp(void)
{
    voice_pressed = false;
    voice_stop_capture();

    if (cls.state == ca_active) {
        cl.sendPacketNow = true;
    }
}

void CL_VoiceFrame(void)
{
    ALCint samples = 0;
    byte encoded[VOICE_MAX_BYTES];
    int len;

    if (!voice_enable->integer || !voice_pressed || !voice_protocol_ok()) {
        voice_stop_capture();
        return;
    }

    if (!voice_encoder || !voice_start_capture()) {
        return;
    }

    qalcGetIntegerv(voice_capture, ALC_CAPTURE_SAMPLES, 1, &samples);
    while (samples >= VOICE_FRAME_SAMPLES) {
        qalcCaptureSamples(voice_capture, voice_capture_buffer, VOICE_FRAME_SAMPLES);

        len = opus_encode(voice_encoder, voice_capture_buffer, VOICE_FRAME_SAMPLES,
                          encoded, sizeof(encoded));
        if (len > 0) {
            voice_queue_packet(encoded, len);
            cl.sendPacketNow = true;
        } else if (voice_show->integer) {
            Com_WPrintf("opus_encode failed: %s\n", opus_strerror(len));
        }

        samples -= VOICE_FRAME_SAMPLES;
    }
}

void CL_WriteVoice(void)
{
    voice_packet_t *packet;

    if (!voice_queue_count || !voice_protocol_ok()) {
        return;
    }

    packet = &voice_queue[voice_queue_tail];
    if (msg_write.cursize + packet->len + 4 > msg_write.maxsize) {
        return;
    }

    MSG_WriteByte(clc_voice);
    MSG_WriteByte(packet->seq);
    MSG_WriteShort(packet->len);
    MSG_WriteData(packet->data, packet->len);

    voice_queue_tail = (voice_queue_tail + 1) % VOICE_QUEUE_PACKETS;
    voice_queue_count--;
}

bool CL_HasVoice(void)
{
    return voice_queue_count && voice_protocol_ok();
}

void CL_ParseVoice(void)
{
    int speaker, sequence q_unused, len, samples;
    const byte *data;
    int16_t pcm[VOICE_FRAME_SAMPLES];
    float gain;

    speaker = MSG_ReadByte();
    sequence = MSG_ReadByte();
    len = MSG_ReadWord();
    if (len <= 0 || len > VOICE_MAX_BYTES) {
        Com_Error(ERR_DROP, "%s: bad voice packet length: %d", __func__, len);
    }

    data = MSG_ReadData(len);

    if (!voice_enable->integer || !voice_decoder || speaker == cl.clientNum) {
        return;
    }

    samples = opus_decode(voice_decoder, data, len, pcm, VOICE_FRAME_SAMPLES, 0);
    if (samples < 0) {
        if (voice_show->integer) {
            Com_WPrintf("opus_decode failed: %s\n", opus_strerror(samples));
        }
        return;
    }

    gain = voice_gain->value;
    if (gain != 1.0f) {
        for (int i = 0; i < samples; i++) {
            int sample = pcm[i] * gain;
            pcm[i] = Q_clip(sample, -32768, 32767);
        }
    }

    S_RawSamples(samples, VOICE_RATE, 2, 1, pcm);
}

void CL_InitVoice(void)
{
    int error;

    voice_enable = Cvar_Get("voice_enable", "1", CVAR_ARCHIVE);
    voice_gain = Cvar_Get("voice_gain", "1", CVAR_ARCHIVE);
    voice_capture_device = Cvar_Get("voice_capture_device", "", CVAR_ARCHIVE);
    voice_show = Cvar_Get("voice_show", "0", 0);

    voice_encoder = opus_encoder_create(VOICE_RATE, 1, OPUS_APPLICATION_VOIP, &error);
    if (!voice_encoder) {
        Com_WPrintf("opus_encoder_create failed: %s\n", opus_strerror(error));
        return;
    }

    opus_encoder_ctl(voice_encoder, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(voice_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    voice_decoder = opus_decoder_create(VOICE_RATE, 1, &error);
    if (!voice_decoder) {
        Com_WPrintf("opus_decoder_create failed: %s\n", opus_strerror(error));
        opus_encoder_destroy(voice_encoder);
        voice_encoder = NULL;
    }
}

void CL_ShutdownVoice(void)
{
    voice_stop_capture();

    if (voice_encoder) {
        opus_encoder_destroy(voice_encoder);
        voice_encoder = NULL;
    }
    if (voice_decoder) {
        opus_decoder_destroy(voice_decoder);
        voice_decoder = NULL;
    }

    voice_pressed = false;
    voice_queue_head = voice_queue_tail = voice_queue_count = 0;
}
