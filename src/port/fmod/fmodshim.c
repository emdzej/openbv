/*
	openbv: FMOD 3.75's FSOUND API (the part BaboViolent 2 uses) on a software
	mixer. See include/fmod.h for the API and include/fmodshim.h for the
	platform side. Behaviour follows FMOD 3's software mixer as documented;
	where the documentation was silent the choice is noted (README.md).

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#include "fmod.h"
#include "fmodshim.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "third_party/stb_vorbis.c"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define MAX_CHANNELS   256
#define HANDLE_SHIFT   12          /* handle = index | serial << 12 */
#define HANDLE_MASK    0xfff
#define STREAM_FRAMES  4096
#define PRIO_DEFAULT   128
#define PRIO_STREAM    256         /* above any sample: music is never stolen */
#define LOOP_BITS      (FSOUND_LOOP_OFF | FSOUND_LOOP_NORMAL | FSOUND_LOOP_BIDI)

struct FSOUND_SAMPLE
{
	int16_t *pcm;              /* interleaved */
	uint32_t frames;
	int chans;
	int freq;
	unsigned int mode;
	int priority, defvol, defpan;
	float mindist, maxdist;
	FSOUND_SAMPLE *next;
};

struct FSOUND_STREAM
{
	uint8_t *file;
	size_t size;
	stb_vorbis *vorbis;        /* Ogg Vorbis, or */
	FSOUND_SAMPLE *pcm;        /* a WAV kept whole */
	int chans, freq;
	unsigned int mode;
	int channel;               /* handle of the channel playing it, or -1 */
	int16_t buf[STREAM_FRAMES * 2];
	uint64_t base;             /* absolute frame of buf[0] */
	int len;
	int eof;
	FSOUND_STREAM *next;
};

typedef struct
{
	int active, paused;
	FSOUND_SAMPLE *sample;
	FSOUND_STREAM *stream;
	uint64_t pos;              /* 32.32 frames */
	int freq, vol, pan;
	unsigned int loop;
	int serial;
	uint32_t order;
	int priority;
	int is3d;
	float p3[3], v3[3];
	float mindist, maxdist;
} Channel;

static struct
{
	int init;
	int rate;
	int nch;
	int master;
	int error;
	uint32_t order;
	float distfactor, rolloff, doppler;
	float lpos[3], lvel[3], lfwd[3], ltop[3];      /* as set */
	float cpos[3], cfwd[3], ctop[3];               /* as committed by FSOUND_Update */
	Channel ch[MAX_CHANNELS];
	FSOUND_SAMPLE *samples;
	FSOUND_STREAM *streams;
	fmodshim_reader read;
} S;

void fmodshim_set_reader(fmodshim_reader read) { S.read = read; }
int fmodshim_mix_rate(void) { return S.init ? S.rate : 0; }

static void *read_file(const char *name, size_t *size)
{
	if (!S.read || !name) return 0;
	return S.read(name, size);
}

/* ------------------------------------------------------------------ */
/* WAV                                                                 */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* PCM WAV (8/16-bit, 1-2 channels) to an int16 sample */
static FSOUND_SAMPLE *wav_decode(const uint8_t *d, size_t n)
{
	const uint8_t *fmt = 0, *data = 0;
	uint32_t datalen = 0;
	size_t p = 12;
	if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return 0;
	while (p + 8 <= n)
	{
		uint32_t len = rd32(d + p + 4);
		if (!memcmp(d + p, "fmt ", 4) && len >= 16 && p + 8 + 16 <= n) fmt = d + p + 8;
		else if (!memcmp(d + p, "data", 4))
		{
			data = d + p + 8;
			datalen = len;
			if (p + 8 + datalen > n) datalen = (uint32_t)(n - p - 8);
		}
		p += 8 + len + (len & 1);
	}
	if (!fmt || !data) return 0;
	{
		int format = rd16(fmt), chans = rd16(fmt + 2), bits = rd16(fmt + 14);
		int freq = (int)rd32(fmt + 4);
		int bps = bits / 8;
		uint32_t frames, i;
		FSOUND_SAMPLE *s;
		if (format != 1 || chans < 1 || chans > 2 || (bits != 8 && bits != 16) || freq <= 0) return 0;
		frames = datalen / (uint32_t)(bps * chans);
		s = (FSOUND_SAMPLE *)calloc(1, sizeof *s);
		if (!s) return 0;
		s->pcm = (int16_t *)malloc((size_t)(frames ? frames : 1) * chans * sizeof(int16_t));
		if (!s->pcm) { free(s); return 0; }
		for (i = 0; i < frames * (uint32_t)chans; i++)
			s->pcm[i] = bits == 8 ? (int16_t)((data[i] - 128) << 8) : (int16_t)rd16(data + i * 2);
		s->frames = frames;
		s->chans = chans;
		s->freq = freq;
		s->mode = (bits == 8 ? FSOUND_8BITS | FSOUND_UNSIGNED : FSOUND_16BITS | FSOUND_SIGNED)
		        | (chans == 2 ? FSOUND_STEREO : FSOUND_MONO);
		return s;
	}
}

static void sample_defaults(FSOUND_SAMPLE *s)
{
	s->priority = PRIO_DEFAULT;
	s->defvol = 255;
	s->defpan = 128;
	s->mindist = 1.0f;
	s->maxdist = 1000000000.0f;
}

/* ------------------------------------------------------------------ */
/* Init                                                                */

signed char F_API FSOUND_Init(int mixrate, int maxsoftwarechannels, unsigned int flags)
{
	(void)flags;
	if (S.init) { S.error = FMOD_ERR_BUSY; return FALSE; }
	if (mixrate < 4000 || mixrate > 65535 || maxsoftwarechannels < 1) { S.error = FMOD_ERR_INVALID_PARAM; return FALSE; }
	memset(S.ch, 0, sizeof S.ch);
	S.init = 1;
	S.rate = mixrate;
	S.nch = maxsoftwarechannels > MAX_CHANNELS ? MAX_CHANNELS : maxsoftwarechannels;
	S.master = 255;
	S.order = 0;
	S.distfactor = S.rolloff = S.doppler = 1.0f;
	memset(S.lpos, 0, sizeof S.lpos);
	memset(S.lvel, 0, sizeof S.lvel);
	S.lfwd[0] = 0; S.lfwd[1] = 0; S.lfwd[2] = 1;
	S.ltop[0] = 0; S.ltop[1] = 1; S.ltop[2] = 0;
	memcpy(S.cpos, S.lpos, sizeof S.cpos);
	memcpy(S.cfwd, S.lfwd, sizeof S.cfwd);
	memcpy(S.ctop, S.ltop, sizeof S.ctop);
	S.error = FMOD_ERR_NONE;
	return TRUE;
}

static void sample_free(FSOUND_SAMPLE *s) { free(s->pcm); free(s); }
static void stream_free(FSOUND_STREAM *st)
{
	if (st->vorbis) stb_vorbis_close(st->vorbis);
	if (st->pcm) sample_free(st->pcm);
	free(st->file);
	free(st);
}

void F_API FSOUND_Close(void)
{
	while (S.samples) { FSOUND_SAMPLE *n = S.samples->next; sample_free(S.samples); S.samples = n; }
	while (S.streams) { FSOUND_STREAM *n = S.streams->next; stream_free(S.streams); S.streams = n; }
	memset(S.ch, 0, sizeof S.ch);
	S.init = 0;
}

int F_API FSOUND_GetError(void) { return S.error; }
int F_API FSOUND_GetMaxChannels(void) { return S.init ? S.nch : 0; }
int F_API FSOUND_GetOutputRate(void) { return S.init ? S.rate : 0; }

/* The 3D engine takes the listener as of the last update, once a frame. */
void F_API FSOUND_Update(void)
{
	memcpy(S.cpos, S.lpos, sizeof S.cpos);
	memcpy(S.cfwd, S.lfwd, sizeof S.cfwd);
	memcpy(S.ctop, S.ltop, sizeof S.ctop);
}

void F_API FSOUND_SetSFXMasterVolume(int volume)
{
	S.master = volume < 0 ? 0 : volume > 255 ? 255 : volume;
}

int F_API FSOUND_GetSFXMasterVolume(void) { return S.master; }

/* ------------------------------------------------------------------ */
/* Samples                                                             */

FSOUND_SAMPLE * F_API FSOUND_Sample_Load(int index, const char *name_or_data, unsigned int mode, int offset, int length)
{
	uint8_t *d;
	size_t n;
	FSOUND_SAMPLE *s;
	(void)index;
	if (!S.init) { S.error = FMOD_ERR_UNINITIALIZED; return 0; }
	if (mode & FSOUND_LOADMEMORY)
	{
		if (!name_or_data || length <= 0) { S.error = FMOD_ERR_INVALID_PARAM; return 0; }
		d = (uint8_t *)name_or_data + (offset > 0 ? offset : 0);
		n = (size_t)length;
		s = wav_decode(d, n);
	}
	else
	{
		d = (uint8_t *)read_file(name_or_data, &n);
		if (!d) { S.error = FMOD_ERR_FILE_NOTFOUND; return 0; }
		if (offset > 0 && (size_t)offset < n) s = wav_decode(d + offset, length > 0 ? (size_t)length : n - offset);
		else s = wav_decode(d, n);
		free(d);
	}
	if (!s) { S.error = FMOD_ERR_FILE_FORMAT; return 0; }
	sample_defaults(s);
	s->mode |= (mode & LOOP_BITS) ? (mode & LOOP_BITS) : FSOUND_LOOP_OFF;
	s->mode |= mode & FSOUND_2D;
	s->next = S.samples;
	S.samples = s;
	S.error = FMOD_ERR_NONE;
	return s;
}

static void stop_channel(Channel *c)
{
	if (c->stream && c->stream->channel >= 0 && (c->stream->channel & HANDLE_MASK) == (int)(c - S.ch))
		c->stream->channel = -1;
	c->active = 0;
	c->sample = 0;
	c->stream = 0;
}

void F_API FSOUND_Sample_Free(FSOUND_SAMPLE *sptr)
{
	FSOUND_SAMPLE **pp;
	int i;
	if (!sptr) return;
	for (i = 0; i < S.nch; i++)
		if (S.ch[i].active && S.ch[i].sample == sptr) stop_channel(&S.ch[i]);
	for (pp = &S.samples; *pp; pp = &(*pp)->next)
		if (*pp == sptr) { *pp = sptr->next; sample_free(sptr); return; }
}

/* Only the loop bits and FSOUND_2D can be set. Loop bits replace the loop
   mode; FSOUND_2D takes the sample out of 3D processing (it is never put
   back: BV2 only ever sets it, from dksPlaySound). */
signed char F_API FSOUND_Sample_SetMode(FSOUND_SAMPLE *sptr, unsigned int mode)
{
	if (!sptr) { S.error = FMOD_ERR_INVALID_PARAM; return FALSE; }
	if (mode & LOOP_BITS) sptr->mode = (sptr->mode & ~LOOP_BITS) | (mode & LOOP_BITS);
	if (mode & FSOUND_2D) sptr->mode |= FSOUND_2D;
	return TRUE;
}

unsigned int F_API FSOUND_Sample_GetMode(FSOUND_SAMPLE *sptr) { return sptr ? sptr->mode : 0; }
unsigned int F_API FSOUND_Sample_GetLength(FSOUND_SAMPLE *sptr) { return sptr ? sptr->frames : 0; }

/* ------------------------------------------------------------------ */
/* Channels                                                            */

static Channel *resolve(int handle)
{
	int idx, ser;
	if (!S.init || handle < 0) return 0;
	idx = handle & HANDLE_MASK;
	ser = handle >> HANDLE_SHIFT;
	if (idx >= S.nch) return 0;
	if (ser && ser != S.ch[idx].serial) return 0;      /* stale handle */
	return &S.ch[idx];
}

/* FSOUND_FREE: the lowest free channel, else the oldest of the lowest
   priority that isn't above the new sound's */
static int pick_channel(int channel, int priority)
{
	int i, best = -1;
	if (channel != FSOUND_FREE)
		return channel >= 0 && channel < S.nch ? channel : -1;
	for (i = 0; i < S.nch; i++)
		if (!S.ch[i].active) return i;
	for (i = 0; i < S.nch; i++)
	{
		Channel *c = &S.ch[i];
		if (c->priority > priority) continue;
		if (best < 0 || c->priority < S.ch[best].priority ||
		    (c->priority == S.ch[best].priority && c->order < S.ch[best].order))
			best = i;
	}
	return best;
}

static int start_channel(int i, FSOUND_SAMPLE *s, FSOUND_STREAM *st, signed char paused)
{
	Channel *c = &S.ch[i];
	int serial;
	if (c->active) stop_channel(c);
	serial = c->serial + 1;
	if (serial >= (1 << (31 - HANDLE_SHIFT))) serial = 1;
	memset(c, 0, sizeof *c);
	c->serial = serial;
	c->active = 1;
	c->paused = paused ? 1 : 0;
	c->order = ++S.order;
	c->vol = 255;
	c->pan = 128;
	c->mindist = 1.0f;
	c->maxdist = 1000000000.0f;
	if (s)
	{
		c->sample = s;
		c->freq = s->freq;
		c->vol = s->defvol;
		c->pan = s->defpan;
		c->priority = s->priority;
		c->loop = s->mode & LOOP_BITS;
		c->is3d = !(s->mode & FSOUND_2D);
		c->mindist = s->mindist;
		c->maxdist = s->maxdist;
	}
	else
	{
		c->stream = st;
		c->freq = st->freq;
		c->priority = PRIO_STREAM;
		c->loop = st->mode & LOOP_BITS;
		c->is3d = 0;
	}
	return i | serial << HANDLE_SHIFT;
}

int F_API FSOUND_PlaySoundEx(int channel, FSOUND_SAMPLE *sptr, FSOUND_DSPUNIT *dsp, signed char startpaused)
{
	int i;
	(void)dsp;
	if (!S.init) { S.error = FMOD_ERR_UNINITIALIZED; return -1; }
	if (!sptr) { S.error = FMOD_ERR_INVALID_PARAM; return -1; }
	i = pick_channel(channel, sptr->priority);
	if (i < 0) { S.error = FMOD_ERR_CHANNEL_ALLOC; return -1; }
	S.error = FMOD_ERR_NONE;
	return start_channel(i, sptr, 0, startpaused);
}

int F_API FSOUND_PlaySound(int channel, FSOUND_SAMPLE *sptr)
{
	return FSOUND_PlaySoundEx(channel, sptr, 0, FALSE);
}

/* Runs f on one channel, or on every channel for FSOUND_ALL. */
#define FOR_CHANNELS(handle, stmt) do { \
	if ((handle) == FSOUND_ALL) { int i_; if (!S.init) return FALSE; \
		for (i_ = 0; i_ < S.nch; i_++) { Channel *c = &S.ch[i_]; stmt; } return TRUE; } \
	else { Channel *c = resolve(handle); if (!c) { S.error = FMOD_ERR_INVALID_PARAM; return FALSE; } \
		stmt; return TRUE; } } while (0)

signed char F_API FSOUND_StopSound(int channel)
{
	FOR_CHANNELS(channel, if (c->active) stop_channel(c));
}

signed char F_API FSOUND_SetVolume(int channel, int vol)
{
	if (vol < 0) vol = 0;
	if (vol > 255) vol = 255;
	FOR_CHANNELS(channel, c->vol = vol);
}

signed char F_API FSOUND_SetPan(int channel, int pan)
{
	if (pan != FSOUND_STEREOPAN) { if (pan < 0) pan = 0; if (pan > 255) pan = 255; }
	FOR_CHANNELS(channel, c->pan = pan);
}

signed char F_API FSOUND_SetPaused(int channel, signed char paused)
{
	FOR_CHANNELS(channel, c->paused = paused ? 1 : 0);
}

signed char F_API FSOUND_SetFrequency(int channel, int freq)
{
	if (freq < 100) freq = 100;
	if (freq > 705600) freq = 705600;
	FOR_CHANNELS(channel, c->freq = freq);
}

signed char F_API FSOUND_SetLoopMode(int channel, unsigned int loopmode)
{
	FOR_CHANNELS(channel, c->loop = loopmode & LOOP_BITS);
}

int F_API FSOUND_GetVolume(int channel) { Channel *c = resolve(channel); return c ? c->vol : 0; }
int F_API FSOUND_GetFrequency(int channel) { Channel *c = resolve(channel); return c ? c->freq : 0; }
signed char F_API FSOUND_GetPaused(int channel) { Channel *c = resolve(channel); return c ? (signed char)c->paused : FALSE; }
signed char F_API FSOUND_IsPlaying(int channel) { Channel *c = resolve(channel); return c && c->active ? TRUE : FALSE; }

/* ------------------------------------------------------------------ */
/* 3D                                                                  */

signed char F_API FSOUND_3D_SetAttributes(int channel, const float *pos, const float *vel)
{
	Channel *c = resolve(channel);
	if (!c) { S.error = FMOD_ERR_INVALID_PARAM; return FALSE; }
	if (pos) memcpy(c->p3, pos, sizeof c->p3);
	if (vel) memcpy(c->v3, vel, sizeof c->v3);
	return TRUE;
}

signed char F_API FSOUND_3D_SetMinMaxDistance(int channel, float min, float max)
{
	Channel *c = resolve(channel);
	if (!c) { S.error = FMOD_ERR_INVALID_PARAM; return FALSE; }
	c->mindist = min;
	c->maxdist = max;
	return TRUE;
}

/* Distance factor and doppler only scale doppler, which needs velocities;
   BV2 passes none, so they are kept but have no audible effect. */
void F_API FSOUND_3D_SetDistanceFactor(float scale) { S.distfactor = scale; }
void F_API FSOUND_3D_SetRolloffFactor(float scale) { S.rolloff = scale < 0 ? 0 : scale; }
void F_API FSOUND_3D_SetDopplerFactor(float scale) { S.doppler = scale; }

void F_API FSOUND_3D_Listener_SetAttributes(const float *pos, const float *vel, float fx, float fy, float fz, float tx, float ty, float tz)
{
	if (pos) memcpy(S.lpos, pos, sizeof S.lpos);
	if (vel) memcpy(S.lvel, vel, sizeof S.lvel);
	S.lfwd[0] = fx; S.lfwd[1] = fy; S.lfwd[2] = fz;
	S.ltop[0] = tx; S.ltop[1] = ty; S.ltop[2] = tz;
}

/* Volume and pan of a 3D channel from the committed listener.
   Attenuation: 1 inside min, min / (min + rolloff * (d - min)) beyond it,
   d clamped to max (past max it stays at the max-distance level).
   Pan: the direction to the sound on the listener's right vector
   (FMOD 3 is left-handed: right = top x forward), 128 + 127.5 * that. */
static void calc3d(const Channel *c, float *att, int *pan)
{
	float d[3], dist, right[3], rl, dot;
	int k;
	for (k = 0; k < 3; k++) d[k] = c->p3[k] - S.cpos[k];
	dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (dist <= c->mindist || c->mindist <= 0) *att = c->mindist <= 0 && dist > 0 ? 0.0f : 1.0f;
	else
	{
		float dd = dist > c->maxdist ? c->maxdist : dist;
		*att = c->mindist / (c->mindist + S.rolloff * (dd - c->mindist));
	}
	right[0] = S.ctop[1] * S.cfwd[2] - S.ctop[2] * S.cfwd[1];
	right[1] = S.ctop[2] * S.cfwd[0] - S.ctop[0] * S.cfwd[2];
	right[2] = S.ctop[0] * S.cfwd[1] - S.ctop[1] * S.cfwd[0];
	rl = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
	if (dist <= 0 || rl <= 0) { *pan = 128; return; }
	dot = (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / (dist * rl);
	k = (int)floorf(128.0f + 127.5f * dot + 0.5f);
	*pan = k < 0 ? 0 : k > 255 ? 255 : k;
}

/* ------------------------------------------------------------------ */
/* Streams                                                             */

FSOUND_STREAM * F_API FSOUND_Stream_Open(const char *name_or_data, unsigned int mode, int offset, int length)
{
	FSOUND_STREAM *st;
	uint8_t *d;
	size_t n;
	if (!S.init) { S.error = FMOD_ERR_UNINITIALIZED; return 0; }
	if (mode & FSOUND_LOADMEMORY)
	{
		if (!name_or_data || length <= 0) { S.error = FMOD_ERR_INVALID_PARAM; return 0; }
		n = (size_t)length;
		d = (uint8_t *)malloc(n);
		if (!d) { S.error = FMOD_ERR_MEMORY; return 0; }
		memcpy(d, name_or_data + (offset > 0 ? offset : 0), n);
	}
	else
	{
		d = (uint8_t *)read_file(name_or_data, &n);
		if (!d) { S.error = FMOD_ERR_FILE_NOTFOUND; return 0; }
	}
	st = (FSOUND_STREAM *)calloc(1, sizeof *st);
	if (!st) { free(d); S.error = FMOD_ERR_MEMORY; return 0; }
	st->file = d;
	st->size = n;
	st->channel = -1;
	st->mode = (mode & LOOP_BITS) ? (mode & LOOP_BITS) : FSOUND_LOOP_OFF;
	if (n >= 4 && !memcmp(d, "OggS", 4))
	{
		int err = 0;
		stb_vorbis_info info;
		st->vorbis = stb_vorbis_open_memory(d, (int)n, &err, 0);
		if (st->vorbis)
		{
			info = stb_vorbis_get_info(st->vorbis);
			st->chans = info.channels > 2 ? 2 : info.channels;
			st->freq = (int)info.sample_rate;
			if (info.channels > 2) { stb_vorbis_close(st->vorbis); st->vorbis = 0; }
		}
	}
	else if ((st->pcm = wav_decode(d, n)) != 0)
	{
		st->chans = st->pcm->chans;
		st->freq = st->pcm->freq;
		free(st->file);
		st->file = 0;
	}
	if (!st->vorbis && !st->pcm)
	{
		free(st->file);
		free(st);
		S.error = FMOD_ERR_FILE_FORMAT;     /* MP3 and the rest: not supported */
		return 0;
	}
	st->next = S.streams;
	S.streams = st;
	S.error = FMOD_ERR_NONE;
	return st;
}

static void stream_rewind(FSOUND_STREAM *st)
{
	if (st->vorbis) stb_vorbis_seek_start(st->vorbis);
	st->base = 0;
	st->len = 0;
	st->eof = 0;
}

int F_API FSOUND_Stream_PlayEx(int channel, FSOUND_STREAM *stream, FSOUND_DSPUNIT *dsp, signed char startpaused)
{
	int i;
	(void)dsp;
	if (!S.init) { S.error = FMOD_ERR_UNINITIALIZED; return -1; }
	if (!stream) { S.error = FMOD_ERR_INVALID_PARAM; return -1; }
	if (stream->channel >= 0) { Channel *c = resolve(stream->channel); if (c && c->stream == stream) stop_channel(c); }
	i = pick_channel(channel, PRIO_STREAM);
	if (i < 0) { S.error = FMOD_ERR_CHANNEL_ALLOC; return -1; }
	stream_rewind(stream);
	stream->channel = start_channel(i, 0, stream, startpaused);
	S.error = FMOD_ERR_NONE;
	return stream->channel;
}

int F_API FSOUND_Stream_Play(int channel, FSOUND_STREAM *stream)
{
	return FSOUND_Stream_PlayEx(channel, stream, 0, FALSE);
}

signed char F_API FSOUND_Stream_Stop(FSOUND_STREAM *stream)
{
	Channel *c;
	if (!stream) { S.error = FMOD_ERR_INVALID_PARAM; return FALSE; }
	c = stream->channel >= 0 ? resolve(stream->channel) : 0;
	if (c && c->active && c->stream == stream) stop_channel(c);
	stream->channel = -1;
	stream_rewind(stream);
	return TRUE;
}

signed char F_API FSOUND_Stream_Close(FSOUND_STREAM *stream)
{
	FSOUND_STREAM **pp;
	if (!stream) { S.error = FMOD_ERR_INVALID_PARAM; return FALSE; }
	FSOUND_Stream_Stop(stream);
	for (pp = &S.streams; *pp; pp = &(*pp)->next)
		if (*pp == stream) { *pp = stream->next; stream_free(stream); return TRUE; }
	return FALSE;
}

signed char F_API FSOUND_Stream_SetMode(FSOUND_STREAM *stream, unsigned int mode)
{
	if (!stream) return FALSE;
	if (mode & LOOP_BITS) stream->mode = mode & LOOP_BITS;
	return TRUE;
}

int F_API FSOUND_Stream_GetMode(FSOUND_STREAM *stream) { return stream ? (int)stream->mode : 0; }

/* Decodes more of the stream so that absolute frame f is in buf; keeps the
   last frame for interpolation. 0 when f is past a stream that ended. */
static int stream_have(FSOUND_STREAM *st, int loop, uint64_t f)
{
	int tries = 0;
	while (f >= st->base + (uint64_t)st->len)
	{
		if (st->eof) return 0;
		if (st->len > 0)
		{
			memcpy(st->buf, st->buf + (st->len - 1) * st->chans, (size_t)st->chans * sizeof(int16_t));
			st->base += (uint64_t)(st->len - 1);
			st->len = 1;
		}
		while (st->len < STREAM_FRAMES && !st->eof)
		{
			int got = 0, room = STREAM_FRAMES - st->len;
			if (st->vorbis)
				got = stb_vorbis_get_samples_short_interleaved(st->vorbis, st->chans, st->buf + st->len * st->chans, room * st->chans);
			else
			{
				/* WAV streams play from memory: the next frame to copy is
				   base + len, wrapped to the sample when looping */
				FSOUND_SAMPLE *s = st->pcm;
				uint64_t at = st->base + (uint64_t)st->len;
				if (s->frames && (loop || at < s->frames))
				{
					uint32_t from = (uint32_t)(at % s->frames);
					got = (int)(s->frames - from);
					if (got > room) got = room;
					memcpy(st->buf + st->len * st->chans, s->pcm + (size_t)from * s->chans, (size_t)got * s->chans * sizeof(int16_t));
				}
			}
			if (got > 0) { st->len += got; tries = 0; continue; }
			if (loop && tries++ == 0)
			{
				if (st->vorbis) stb_vorbis_seek_start(st->vorbis);
				continue;
			}
			st->eof = 1;
		}
	}
	return 1;
}

/* ------------------------------------------------------------------ */
/* Mixer                                                               */

static void frame_at(Channel *c, uint64_t f, int *ok, float out[2])
{
	if (c->sample)
	{
		FSOUND_SAMPLE *s = c->sample;
		const int16_t *p;
		if (f >= s->frames)
		{
			if (c->loop & (FSOUND_LOOP_NORMAL | FSOUND_LOOP_BIDI) && s->frames) f %= s->frames;
			else { *ok = 0; return; }
		}
		p = s->pcm + (size_t)f * s->chans;
		out[0] = p[0] / 32768.0f;
		out[1] = s->chans == 2 ? p[1] / 32768.0f : out[0];
	}
	else
	{
		FSOUND_STREAM *st = c->stream;
		const int16_t *p;
		if (!stream_have(st, (c->loop & (FSOUND_LOOP_NORMAL | FSOUND_LOOP_BIDI)) != 0, f)) { *ok = 0; return; }
		p = st->buf + (size_t)(f - st->base) * st->chans;
		out[0] = p[0] / 32768.0f;
		out[1] = st->chans == 2 ? p[1] / 32768.0f : out[0];
	}
	*ok = 1;
}

static void mix_channel(Channel *c, float *out, int frames)
{
	float att = 1.0f, gl, gr, vol;
	int pan = c->pan, i;
	uint64_t step;
	if (c->is3d) calc3d(c, &att, &pan);
	vol = (c->vol / 255.0f) * (S.master / 255.0f) * att;
	if (pan == FSOUND_STEREOPAN) { gl = vol; gr = vol; }
	else
	{
		/* linear pan: 128 is a little under half on each side */
		gl = vol * (255 - pan) / 255.0f;
		gr = vol * pan / 255.0f;
	}
	step = ((uint64_t)(uint32_t)c->freq << 32) / (uint64_t)S.rate;
	for (i = 0; i < frames && c->active; i++)
	{
		uint64_t ip = c->pos >> 32;
		float frac = (float)(uint32_t)c->pos / 4294967296.0f;
		float a[2], b[2];
		int ok;
		int okb;
		/* a stream keeps frame ip when it decodes ip + 1: fetch ip + 1 first */
		frame_at(c, ip + 1, &okb, b);
		frame_at(c, ip, &ok, a);
		if (!ok) { stop_channel(c); break; }
		if (!okb) { b[0] = a[0]; b[1] = a[1]; }
		out[i * 2]     += (a[0] + (b[0] - a[0]) * frac) * gl;
		out[i * 2 + 1] += (a[1] + (b[1] - a[1]) * frac) * gr;
		c->pos += step;
	}
}

void fmodshim_render(float *out, int frames)
{
	int i;
	if (frames <= 0) return;
	memset(out, 0, (size_t)frames * 2 * sizeof(float));
	if (!S.init) return;
	for (i = 0; i < S.nch; i++)
		if (S.ch[i].active && !S.ch[i].paused) mix_channel(&S.ch[i], out, frames);
	for (i = 0; i < frames * 2; i++)
	{
		if (out[i] > 1.0f) out[i] = 1.0f;
		else if (out[i] < -1.0f) out[i] = -1.0f;
	}
}
