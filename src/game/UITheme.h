/*
	openbv: the menus' look.

	The original drew every panel and button as a gradient quad in the control's colour
	(renderMenuQuad, Helper.cpp). openbv's menus are redesigned for 16:9 (ui.h): flat translucent
	panels with thin borders and rounded corners, one accent colour (BV2's orange), a modern font.
	Behaviour and layout logic stay the game's; this is how controls are painted.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/
#ifndef OPENBV_UITHEME_H
#define OPENBV_UITHEME_H

struct UIColor
{
	float r, g, b, a;
};

namespace ui
{
	extern const UIColor panel;       // page frames
	extern const UIColor panelLine;   // their border
	extern const UIColor field;       // edit boxes, lists
	extern const UIColor fieldLine;
	extern const UIColor button;
	extern const UIColor buttonHover;
	extern const UIColor buttonDown;
	extern const UIColor buttonLine;
	extern const UIColor accent;      // selection, primary actions, focus
	extern const UIColor accentHover;
	extern const UIColor accentDim;   // selected row tint
	extern const UIColor text;
	extern const UIColor textMuted;
	extern const UIColor textOnAccent;
	extern const UIColor hoverTint;
	extern const UIColor scrim;       // over the menu background
	extern const UIColor danger;

	UIColor withAlpha(const UIColor & c, float a);
	UIColor mix(const UIColor & a, const UIColor & b, float t);
	void setColor(const UIColor & c);

	// A rounded rectangle in the current (UI) coordinates: fill, then a 1-unit border on top.
	// fill or line with alpha 0 are skipped. radius in units.
	void rect(float x, float y, float w, float h, const UIColor & fill, const UIColor & line, float radius = 5);
	void fillRect(float x, float y, float w, float h, const UIColor & fill, float radius = 5);
	void lineRect(float x, float y, float w, float h, const UIColor & line, float radius = 5);
	// Plain quads (no rounding), for hairlines and bars.
	void bar(float x, float y, float w, float h, const UIColor & c);
	// A check mark inside the box (x, y, s, s).
	void checkMark(float x, float y, float s, const UIColor & c);
	// Darkens the edges of the screen (w x h units) so panels read over the background art.
	void vignette(float w, float h);
}

#endif
