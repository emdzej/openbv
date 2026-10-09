/*
	openbv: UI assets built into the module (tools/fonts/embed_assets.py), served by src/port/fs_gasm.c
	ahead of the game's data: white TGAs stored as their alpha channel, run-length encoded.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/
#ifndef OPENBV_EMBEDDED_ASSETS_H
#define OPENBV_EMBEDDED_ASSETS_H

struct openbv_embedded
{
	const char *name;           /* the game file it replaces, e.g. "main/fonts/babo.tga" */
	int width, height;
	const unsigned char *alphaRle;  /* (count, value) pairs, rows bottom-up */
	unsigned int rleSize;
};

extern const struct openbv_embedded openbv_embedded_assets[];

#endif
