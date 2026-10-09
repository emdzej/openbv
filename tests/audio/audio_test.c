/*
	openbv: FMOD shim test. Plays a scripted sequence through the FSOUND API
	the way dks.cpp and the game call it, with the real sounds, renders it in
	per-frame blocks like the gasm platform does, writes out/audio/test.wav,
	prints peak/RMS per segment and an FNV-1a hash of the output.

	usage: audio_test [content root (has main/sounds)] [out.wav]
	exit 0 if every check passes.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#include "fmod.h"
#include "fmodshim.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 22050
#define FPS 60

static float *outbuf;
static size_t outlen, outcap;
static int fails;

static void check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) fails++;
}

/* renders seconds of audio in per-frame blocks, FSOUND_Update each frame */
static void run(float seconds)
{
	static uint64_t frameno;
	int frames = (int)(seconds * FPS + 0.5f), f;
	for (f = 0; f < frames; f++, frameno++)
	{
		/* frame k covers [k*RATE/FPS, (k+1)*RATE/FPS) */
		int cnt = (int)((frameno + 1) * RATE / FPS - frameno * RATE / FPS);
		if (outlen + (size_t)cnt * 2 > outcap)
		{
			outcap = (outlen + (size_t)cnt * 2) * 2;
			outbuf = (float *)realloc(outbuf, outcap * sizeof(float));
		}
		FSOUND_Update();
		fmodshim_render(outbuf + outlen, cnt);
		outlen += (size_t)cnt * 2;
	}
}

typedef struct { const char *name; size_t from, to; } Seg;
static Seg segs[64];
static int nseg;
static size_t mark;

/* each segment starts from silence, so its levels are its own */
static void seg_begin(void) { FSOUND_StopSound(FSOUND_ALL); mark = outlen / 2; }
static void seg_end(const char *name) { segs[nseg].name = name; segs[nseg].from = mark; segs[nseg].to = outlen / 2; nseg++; }

static void levels(const Seg *s, float peak[2], float rms[2])
{
	size_t i;
	double sum[2] = {0, 0};
	peak[0] = peak[1] = 0;
	for (i = s->from; i < s->to; i++)
	{
		int k;
		for (k = 0; k < 2; k++)
		{
			float v = fabsf(outbuf[i * 2 + k]);
			if (v > peak[k]) peak[k] = v;
			sum[k] += (double)v * v;
		}
	}
	for (int k = 0; k < 2; k++) rms[k] = s->to > s->from ? (float)sqrt(sum[k] / (double)(s->to - s->from)) : 0;
}

/* dksPlaySound */
static int play2d(FSOUND_SAMPLE *s, int vol)
{
	int ch;
	FSOUND_Sample_SetMode(s, FSOUND_2D);
	ch = FSOUND_PlaySoundEx(FSOUND_FREE, s, 0, 1);
	FSOUND_SetVolume(ch, vol);
	FSOUND_SetPaused(ch, 0);
	return ch;
}

/* dksPlay3DSound */
static int play3d(FSOUND_SAMPLE *s, float range, float x, float y, float z, int vol)
{
	float p[3] = {x, y, z};
	int ch = FSOUND_PlaySoundEx(FSOUND_FREE, s, 0, 1);
	FSOUND_3D_SetMinMaxDistance(ch, range, 10000000.0f);
	FSOUND_3D_SetAttributes(ch, p, 0);
	FSOUND_SetVolume(ch, vol);
	FSOUND_SetPaused(ch, 0);
	return ch;
}

static FSOUND_SAMPLE *load(const char *name, int loop)
{
	FSOUND_SAMPLE *s = FSOUND_Sample_Load(FSOUND_FREE, name, loop ? FSOUND_LOOP_NORMAL : FSOUND_LOOP_OFF, 0, 0);
	char msg[256];
	snprintf(msg, sizeof msg, "load %s (%u frames)", name, s ? FSOUND_Sample_GetLength(s) : 0);
	check(s != 0, msg);
	return s;
}

static void write_wav(const char *path)
{
	FILE *f = fopen(path, "wb");
	uint32_t frames = (uint32_t)(outlen / 2), data = frames * 4, v;
	size_t i;
	if (!f) { printf("cannot write %s\n", path); fails++; return; }
	fwrite("RIFF", 1, 4, f); v = 36 + data; fwrite(&v, 4, 1, f);
	fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
	{ uint16_t fmt[2] = {1, 2}; fwrite(fmt, 2, 2, f); }
	v = RATE; fwrite(&v, 4, 1, f); v = RATE * 4; fwrite(&v, 4, 1, f);
	{ uint16_t b[2] = {4, 16}; fwrite(b, 2, 2, f); }
	fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
	for (i = 0; i < outlen; i++)
	{
		int16_t s = (int16_t)lrintf(outbuf[i] * 32767.0f);
		fwrite(&s, 2, 1, f);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	const char *root = argc > 1 ? argv[1] : "ref/BaboViolent2/BaboViolent2/Content";
	const char *out = argc > 2 ? argv[2] : "out/audio/test.wav";
	FSOUND_SAMPLE *button, *expl, *ric, *rain, *hit;
	FSOUND_STREAM *music;
	float cam[3] = {0, 0, 7};
	int ch, i;
	uint32_t h = 2166136261u;

	fmodshim_stdio_root(root);
	fmodshim_set_reader(fmodshim_stdio_read);
	check(FSOUND_Init(RATE, 16, 0) == 1, "FSOUND_Init 22050 Hz, 16 channels");
	FSOUND_3D_SetDistanceFactor(64.0f);
	check(fmodshim_mix_rate() == RATE, "mix rate");
	FSOUND_SetSFXMasterVolume(255);
	/* the in-game listener: camera above the map, looking down +z, top +y */
	FSOUND_3D_Listener_SetAttributes(cam, 0, 0, 0, 1, 0, 1, 0);

	button = load("main/sounds/Button.wav", 0);
	expl = load("main/sounds/Explosion1.wav", 0);
	ric = load("main/sounds/ric1.wav", 0);
	rain = load("main/sounds/rain2.wav", 1);
	hit = load("main/sounds/hit1.wav", 0);
	check(FSOUND_Sample_Load(FSOUND_FREE, "main/sounds/nope.wav", 0, 0, 0) == 0 && FSOUND_GetError() == FMOD_ERR_FILE_NOTFOUND, "missing file fails");
	check(load("main/sounds/BUTTON.WAV", 0) != 0, "case-insensitive lookup");
	if (fails) return 1;

	run(0.1f);
	seg_begin(); play2d(button, 200); run(0.5f); seg_end("2D button vol 200");
	seg_begin(); play3d(expl, 10, 0, 0, 0, 255); run(1.0f); seg_end("3D explosion under the camera, min 10");
	seg_begin(); play3d(ric, 5, 20, 0, 0, 255); run(0.5f); seg_end("3D ric 20 right, min 5");
	seg_begin(); play3d(ric, 1, -50, 0, 0, 255); run(0.5f); seg_end("3D ric 50 left, min 1");
	seg_begin(); ch = play2d(rain, 50); run(1.0f); FSOUND_StopSound(ch); run(0.1f); seg_end("2D loop rain vol 50, stopped");
	mark = outlen / 2; run(0.2f); seg_end("silence after the loop stopped");
	seg_begin();
	{
		float p[3] = {0, 0, 0};
		ch = FSOUND_PlaySoundEx(-1, expl, 0, 1);
		FSOUND_3D_SetMinMaxDistance(ch, 10, 10000000.0f);
		FSOUND_3D_SetAttributes(ch, p, 0);
		FSOUND_SetFrequency(ch, 5000);
		FSOUND_SetVolume(ch, 255);
		FSOUND_SetPaused(ch, 0);
	}
	run(1.5f); seg_end("explosion at 5000 Hz (GameSpawn)");

	/* the 2D flag sticks: hit played 2D, then "3D" far away stays loud */
	seg_begin(); play2d(hit, 255); run(0.4f); seg_end("hit 2D");
	seg_begin(); play3d(hit, 1, 500, 0, 0, 255); run(0.4f); seg_end("hit 3D far, after a 2D play");
	check(FSOUND_Sample_GetMode(hit) & FSOUND_2D, "FSOUND_2D sticks on the sample");

	/* stale handles: a handle from a finished sound must not stop the next one */
	{
		int a = play2d(button, 10), b;
		FSOUND_StopSound(a);
		b = play2d(button, 10);
		check((a & 0xfff) == (b & 0xfff) && a != b, "channel reused with a new handle");
		check(FSOUND_StopSound(a) == 0 && FSOUND_IsPlaying(b), "stale handle refused");
		FSOUND_StopSound(b);
	}

	/* 20 sounds on 16 channels: the oldest are stolen, all calls succeed */
	{
		int hs[20], okc = 1;
		for (i = 0; i < 20; i++) { hs[i] = play2d(ric, 30); if (hs[i] < 0) okc = 0; }
		check(okc, "FSOUND_FREE steals when all 16 are busy");
		check(!FSOUND_IsPlaying(hs[0]) && FSOUND_IsPlaying(hs[19]), "the oldest was stolen");
		FSOUND_StopSound(FSOUND_ALL);
	}
	run(0.1f);

	/* music: the way dksPlayMusic does it */
	music = FSOUND_Stream_Open("main/sounds/Menu.ogg", FSOUND_LOOP_NORMAL, 0, 0);
	check(music != 0, "stream Menu.ogg");
	check(FSOUND_Stream_Open("main/sounds/IntroScreen.mp3", FSOUND_LOOP_NORMAL, 0, 0) == 0, "missing mp3 stream fails");
	seg_begin();
	if (music)
	{
		ch = FSOUND_Stream_PlayEx(FSOUND_FREE, music, 0, 1);
		FSOUND_SetVolume(ch, 60);
		FSOUND_SetPaused(ch, 0);
	}
	run(2.0f); seg_end("music Menu.ogg vol 60");
	if (music) { FSOUND_Stream_Stop(music); FSOUND_Stream_Close(music); }
	mark = outlen / 2; run(0.2f); seg_end("after music stop");

	/* the in-game track, through its loop point, at the in-game volume */
	music = FSOUND_Stream_Open("main/sounds/Music.ogg", FSOUND_LOOP_NORMAL, 0, 0);
	check(music != 0, "stream Music.ogg");
	seg_begin();
	if (music) { ch = FSOUND_Stream_PlayEx(FSOUND_FREE, music, 0, 1); FSOUND_SetVolume(ch, 60); FSOUND_SetPaused(ch, 0); }
	run(1.0f); seg_end("music Music.ogg vol 60");
	if (music) { FSOUND_Stream_Stop(music); FSOUND_Stream_Close(music); }

	FSOUND_Sample_Free(button);
	FSOUND_Close();

	printf("\n%-42s %13s %13s\n", "segment", "peak L/R", "rms L/R");
	for (i = 0; i < nseg; i++)
	{
		float pk[2], rm[2];
		levels(&segs[i], pk, rm);
		printf("%-42s %6.3f/%6.3f %6.3f/%6.3f\n", segs[i].name, pk[0], pk[1], rm[0], rm[1]);
	}
	{
		float pk[2], rm[2], pk2[2], rm2[2];
		levels(&segs[0], pk, rm);
		check(pk[0] > 0.05f && fabsf(pk[0] - pk[1]) < 0.01f, "2D button centred and audible");
		levels(&segs[2], pk, rm);
		check(rm[1] > rm[0] * 1.5f, "ric to the right is louder on the right");
		levels(&segs[3], pk2, rm2);
		check(rm2[0] > rm2[1] * 1.5f && rm2[0] + rm2[1] < rm[0] + rm[1], "ric far left: left, quieter");
		levels(&segs[5], pk, rm);
		check(pk[0] == 0 && pk[1] == 0, "silence after stop");
		levels(&segs[8], pk, rm);
		levels(&segs[7], pk2, rm2);
		check(fabsf(rm[0] - rm2[0]) < 0.25f * rm2[0] + 1e-4f, "2D-flagged sample ignores 3D distance");
		levels(&segs[9], pk, rm);
		check(rm[0] > 0.01f && rm[1] > 0.01f, "music audible");
		levels(&segs[10], pk, rm);
		check(pk[0] == 0, "music stopped");
	}

	for (i = 0; i < (int)outlen; i++)
	{
		uint32_t bits;
		memcpy(&bits, &outbuf[i], 4);
		for (int k = 0; k < 4; k++) { h ^= (bits >> (k * 8)) & 0xff; h *= 16777619u; }
	}
	write_wav(out);
	printf("\n%zu frames, fnv32 %08x, %s\n", outlen / 2, h, out);
	printf("%s\n", fails ? "FAILED" : "PASS");
	return fails ? 1 : 0;
}
