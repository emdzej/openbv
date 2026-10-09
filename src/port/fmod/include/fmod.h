/*
	openbv: the subset of the FMOD 3.75 API (FSOUND_*) that BaboViolent 2 uses,
	implemented on a software mixer (src/port/fmod/fmodshim.c).

	Names, signatures and constant values follow FMOD 3.75's fmod.h so the
	vendored engine (src/engine/dk/dks.cpp) and the game build unchanged.
	The audio itself is pulled by the platform layer: see fmodshim.h.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#ifndef _FMOD_H_
#define _FMOD_H_

#define FMOD_VERSION 3.75f

#define F_API
#define F_CALLBACKAPI

typedef struct FSOUND_SAMPLE    FSOUND_SAMPLE;
typedef struct FSOUND_STREAM    FSOUND_STREAM;
typedef struct FSOUND_DSPUNIT   FSOUND_DSPUNIT;
typedef struct FSOUND_SYNCPOINT FSOUND_SYNCPOINT;
typedef struct FMUSIC_MODULE    FMUSIC_MODULE;

/* error codes (FSOUND_GetError) */
enum FMOD_ERRORS
{
	FMOD_ERR_NONE,
	FMOD_ERR_BUSY,
	FMOD_ERR_UNINITIALIZED,
	FMOD_ERR_INIT,
	FMOD_ERR_ALLOCATED,
	FMOD_ERR_PLAY,
	FMOD_ERR_OUTPUT_FORMAT,
	FMOD_ERR_COOPERATIVELEVEL,
	FMOD_ERR_CREATEBUFFER,
	FMOD_ERR_FILE_NOTFOUND,
	FMOD_ERR_FILE_FORMAT,
	FMOD_ERR_FILE_BAD,
	FMOD_ERR_MEMORY,
	FMOD_ERR_VERSION,
	FMOD_ERR_INVALID_PARAM,
	FMOD_ERR_NO_EAX,
	FMOD_ERR_CHANNEL_ALLOC,
	FMOD_ERR_RECORD,
	FMOD_ERR_MEDIAPLAYER,
	FMOD_ERR_CDDEVICE
};

/* sample and stream modes */
#define FSOUND_LOOP_OFF      0x00000001
#define FSOUND_LOOP_NORMAL   0x00000002
#define FSOUND_LOOP_BIDI     0x00000004
#define FSOUND_8BITS         0x00000008
#define FSOUND_16BITS        0x00000010
#define FSOUND_MONO          0x00000020
#define FSOUND_STEREO        0x00000040
#define FSOUND_UNSIGNED      0x00000080
#define FSOUND_SIGNED        0x00000100
#define FSOUND_DELTA         0x00000200
#define FSOUND_IT214         0x00000400
#define FSOUND_IT215         0x00000800
#define FSOUND_HW3D          0x00001000
#define FSOUND_2D            0x00002000
#define FSOUND_STREAMABLE    0x00004000
#define FSOUND_LOADMEMORY    0x00008000
#define FSOUND_LOADRAW       0x00010000
#define FSOUND_MPEGACCURATE  0x00020000
#define FSOUND_FORCEMONO     0x00040000
#define FSOUND_HW2D          0x00080000
#define FSOUND_ENABLEFX      0x00100000
#define FSOUND_MPEGHALFRATE  0x00200000
#define FSOUND_IMAADPCM      0x00400000
#define FSOUND_VAG           0x00800000
#define FSOUND_NONBLOCKING   0x01000000
#define FSOUND_GCADPCM       0x02000000
#define FSOUND_MULTICHANNEL  0x04000000
#define FSOUND_USECORE0      0x08000000
#define FSOUND_USECORE1      0x10000000
#define FSOUND_LOADMEMORYIOP 0x20000000
#define FSOUND_IGNORETAGS    0x40000000
#define FSOUND_STREAM_NET    0x80000000

#define FSOUND_NORMAL (FSOUND_16BITS | FSOUND_SIGNED | FSOUND_MONO)

/* special channel and sample indices */
#define FSOUND_FREE          -1
#define FSOUND_UNMANAGED     -2
#define FSOUND_ALL           -3
#define FSOUND_STEREOPAN     -1
#define FSOUND_SYSTEMCHANNEL -1000
#define FSOUND_SYSTEMSAMPLE  -1000

#ifdef __cplusplus
extern "C" {
#endif

signed char     F_API FSOUND_Init(int mixrate, int maxsoftwarechannels, unsigned int flags);
void            F_API FSOUND_Close(void);
void            F_API FSOUND_Update(void);
int             F_API FSOUND_GetError(void);
int             F_API FSOUND_GetMaxChannels(void);
int             F_API FSOUND_GetOutputRate(void);

void            F_API FSOUND_SetSFXMasterVolume(int volume);
int             F_API FSOUND_GetSFXMasterVolume(void);

FSOUND_SAMPLE * F_API FSOUND_Sample_Load(int index, const char *name_or_data, unsigned int mode, int offset, int length);
void            F_API FSOUND_Sample_Free(FSOUND_SAMPLE *sptr);
signed char     F_API FSOUND_Sample_SetMode(FSOUND_SAMPLE *sptr, unsigned int mode);
unsigned int    F_API FSOUND_Sample_GetMode(FSOUND_SAMPLE *sptr);
unsigned int    F_API FSOUND_Sample_GetLength(FSOUND_SAMPLE *sptr);

int             F_API FSOUND_PlaySound(int channel, FSOUND_SAMPLE *sptr);
int             F_API FSOUND_PlaySoundEx(int channel, FSOUND_SAMPLE *sptr, FSOUND_DSPUNIT *dsp, signed char startpaused);
signed char     F_API FSOUND_StopSound(int channel);

signed char     F_API FSOUND_SetVolume(int channel, int vol);
signed char     F_API FSOUND_SetPan(int channel, int pan);
signed char     F_API FSOUND_SetPaused(int channel, signed char paused);
signed char     F_API FSOUND_SetFrequency(int channel, int freq);
signed char     F_API FSOUND_SetLoopMode(int channel, unsigned int loopmode);
int             F_API FSOUND_GetVolume(int channel);
int             F_API FSOUND_GetFrequency(int channel);
signed char     F_API FSOUND_GetPaused(int channel);
signed char     F_API FSOUND_IsPlaying(int channel);

signed char     F_API FSOUND_3D_SetAttributes(int channel, const float *pos, const float *vel);
signed char     F_API FSOUND_3D_SetMinMaxDistance(int channel, float min, float max);
void            F_API FSOUND_3D_SetDistanceFactor(float scale);
void            F_API FSOUND_3D_SetRolloffFactor(float scale);
void            F_API FSOUND_3D_SetDopplerFactor(float scale);
void            F_API FSOUND_3D_Listener_SetAttributes(const float *pos, const float *vel, float fx, float fy, float fz, float tx, float ty, float tz);

FSOUND_STREAM * F_API FSOUND_Stream_Open(const char *name_or_data, unsigned int mode, int offset, int length);
int             F_API FSOUND_Stream_Play(int channel, FSOUND_STREAM *stream);
int             F_API FSOUND_Stream_PlayEx(int channel, FSOUND_STREAM *stream, FSOUND_DSPUNIT *dsp, signed char startpaused);
signed char     F_API FSOUND_Stream_Stop(FSOUND_STREAM *stream);
signed char     F_API FSOUND_Stream_Close(FSOUND_STREAM *stream);
signed char     F_API FSOUND_Stream_SetMode(FSOUND_STREAM *stream, unsigned int mode);
int             F_API FSOUND_Stream_GetMode(FSOUND_STREAM *stream);

#ifdef __cplusplus
}
#endif

#endif
