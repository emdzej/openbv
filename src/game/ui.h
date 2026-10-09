/*
	openbv: the UI's virtual canvas.

	The original laid out its menus, HUD and console in 800x600 units (4:3), with an optional
	"widescreen" mode that pillarboxed or stretched them. openbv is 16:9 throughout: the same 600 units
	high, 1066 wide (600 * 16 / 9), drawn at the window's 16:9 resolution.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/
#ifndef OPENBV_UI_H
#define OPENBV_UI_H

#define UI_W 1066
#define UI_H 600

// the HUD and in-game screens were placed for 800 units: the centre moves to UI_CX, what hung on
// the right edge moves right by UI_RX
#define UI_CX (UI_W / 2)
#define UI_RX (UI_W - 800)

// the main menu's frame: a tab bar across the top, pages under it, a footer line below
#define UI_MARGIN 40
#define UI_TAB_Y 20
#define UI_TAB_H 32
#define UI_PAGE_W (UI_W - 2 * UI_MARGIN)                    // 986 (the original: 736)
#define UI_PAGE_H (UI_H - (UI_TAB_Y + UI_TAB_H + 5) - 40)   // 503 (the original: 506)
#define UI_PAGE_INNER_W (UI_PAGE_W - 20)                    // a list or panel inside a page

#endif
