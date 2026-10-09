/*
	openbv: the platform side of the FMOD shim.

	The shim never opens files and never runs on its own: the platform layer
	hands it a reader for game files and pulls the mix once per frame.

	  fmodshim_set_reader(read)   read(name, &size) returns a malloc'd buffer
	                              (the shim frees it) or NULL if missing; names
	                              are as the game passes them ("main/sounds/x.wav")
	  fmodshim_mix_rate()         the rate FSOUND_Init was given (0 before), the
	                              rate fmodshim_render produces
	  fmodshim_render(out, n)     mix n stereo frames, interleaved float [-1, 1]

	Output depends only on the sequence of calls (no clock, no threads).

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#ifndef FMODSHIM_H
#define FMODSHIM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*fmodshim_reader)(const char *name, size_t *size);

void fmodshim_set_reader(fmodshim_reader read);
int  fmodshim_mix_rate(void);
void fmodshim_render(float *out, int frames);

/* fmodshim_stdio.c: reads name relative to root (case-insensitive fallback
   per path component); for native tests and tools */
void fmodshim_stdio_root(const char *root);
void *fmodshim_stdio_read(const char *name, size_t *size);

#ifdef __cplusplus
}
#endif

#endif
