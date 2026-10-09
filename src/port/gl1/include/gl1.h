/*
	Hooks of the OpenGL 1.x emulator (src/port/gl1) for the platform layer.
	Part of openbv, GPL-3.0-or-later.
*/

#ifndef OPENBV_GL1_H
#define OPENBV_GL1_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call at the start of each gasm frame with the size the game renders at (its resolution;
   gl1_end_frame scales it to the drawable). The game draws into an RGBA8 + depth framebuffer of that size (the default one has no
   alpha, and BV2 blends with GL_DST_ALPHA); a new size makes a new, cleared one. The GL
   viewport and scissor box start as the first size, as a window's do; later sizes leave
   them alone (the game sets its own viewport on resize). */
void gl1_begin_frame(int width, int height);

/* Call at the end of each gasm frame (the game's SwapBuffers): draws what is batched and
   copies the frame to the default framebuffer. */
void gl1_end_frame(void);

/* Where gl1_end_frame puts the frame in a window of winW x winH: the game's framebuffer scaled
   with its aspect kept and centred (GL's bottom-left origin). */
void gl1_frame_rect(int winW, int winH, int *x, int *y, int *w, int *h);

/* Draw what is batched now. */
void gl1_flush(void);

#ifdef __cplusplus
}
#endif

#endif
