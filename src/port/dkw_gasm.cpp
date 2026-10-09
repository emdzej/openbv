/*
	openbv: the dkw module (window, main loop, raw keyboard and mouse) on gasm.

	The original dkw.cpp made a Win32 (or SDL) window with an OpenGL context and pumped its messages;
	WM_PAINT called the game's paint(), WM_CHAR its textWrite(), and DirectInput (dki.cpp) read the
	keyboard and mouse. On gasm the runner owns the window and calls gasm_frame once per frame: that is
	the WM_PAINT. dki.cpp is unchanged; it reads the keyboard and mouse through dkwGetKeysState and
	dkwGetMouseState, which this file fills from gasm's raw input (DirectInput scan codes, mouse deltas
	in pixels, the wheel in DirectInput's units of 120 per notch).
*/

#include "dkw.h"
#include "dki.h"
#include "dik.h"
#include "gasm.h"
#include "fmodshim.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <wasi/api.h>

#include "gl1.h"

namespace
{
	CMainLoopInterface *mainLoopObject = 0;
	char lastError[256] = "";
	int resW = 800, resH = 600;
	bool quitRequested = false;
	bool clipMouse = false;

	unsigned char keysDI[256];       // what dkiUpdate sees (0x80 = down)
	unsigned char latchedDI[256];    // pressed since dkiUpdate last looked, so a tap between two ticks counts
	DIMOUSESTATE2 mouse;
	unsigned int latchedButtons = 0;
	float wheelAccum = 0;
	CVector2i cursor;

	unsigned char gasmToDik[256];

	void initKeyTable()
	{
		memset(gasmToDik, 0, sizeof(gasmToDik));
		struct { int g, d; } map[] = {
			{GASM_KEY_ESCAPE, DIK_ESCAPE}, {GASM_KEY_F1, DIK_F1}, {GASM_KEY_F2, DIK_F2}, {GASM_KEY_F3, DIK_F3},
			{GASM_KEY_F4, DIK_F4}, {GASM_KEY_F5, DIK_F5}, {GASM_KEY_F6, DIK_F6}, {GASM_KEY_F7, DIK_F7},
			{GASM_KEY_F8, DIK_F8}, {GASM_KEY_F9, DIK_F9}, {GASM_KEY_F10, DIK_F10}, {GASM_KEY_F11, DIK_F11},
			{GASM_KEY_F12, DIK_F12}, {GASM_KEY_F13, DIK_F13}, {GASM_KEY_F14, DIK_F14}, {GASM_KEY_F15, DIK_F15},
			{GASM_KEY_BACKQUOTE, DIK_GRAVE}, {GASM_KEY_DIGIT1, DIK_1}, {GASM_KEY_DIGIT2, DIK_2},
			{GASM_KEY_DIGIT3, DIK_3}, {GASM_KEY_DIGIT4, DIK_4}, {GASM_KEY_DIGIT5, DIK_5}, {GASM_KEY_DIGIT6, DIK_6},
			{GASM_KEY_DIGIT7, DIK_7}, {GASM_KEY_DIGIT8, DIK_8}, {GASM_KEY_DIGIT9, DIK_9}, {GASM_KEY_DIGIT0, DIK_0},
			{GASM_KEY_MINUS, DIK_MINUS}, {GASM_KEY_EQUAL, DIK_EQUALS}, {GASM_KEY_BACKSPACE, DIK_BACK},
			{GASM_KEY_TAB, DIK_TAB}, {GASM_KEY_KEY_Q, DIK_Q}, {GASM_KEY_KEY_W, DIK_W}, {GASM_KEY_KEY_E, DIK_E},
			{GASM_KEY_KEY_R, DIK_R}, {GASM_KEY_KEY_T, DIK_T}, {GASM_KEY_KEY_Y, DIK_Y}, {GASM_KEY_KEY_U, DIK_U},
			{GASM_KEY_KEY_I, DIK_I}, {GASM_KEY_KEY_O, DIK_O}, {GASM_KEY_KEY_P, DIK_P},
			{GASM_KEY_BRACKET_LEFT, DIK_LBRACKET}, {GASM_KEY_BRACKET_RIGHT, DIK_RBRACKET},
			{GASM_KEY_ENTER, DIK_RETURN}, {GASM_KEY_CONTROL_LEFT, DIK_LCONTROL}, {GASM_KEY_KEY_A, DIK_A},
			{GASM_KEY_KEY_S, DIK_S}, {GASM_KEY_KEY_D, DIK_D}, {GASM_KEY_KEY_F, DIK_F}, {GASM_KEY_KEY_G, DIK_G},
			{GASM_KEY_KEY_H, DIK_H}, {GASM_KEY_KEY_J, DIK_J}, {GASM_KEY_KEY_K, DIK_K}, {GASM_KEY_KEY_L, DIK_L},
			{GASM_KEY_SEMICOLON, DIK_SEMICOLON}, {GASM_KEY_QUOTE, DIK_APOSTROPHE},
			{GASM_KEY_SHIFT_LEFT, DIK_LSHIFT}, {GASM_KEY_BACKSLASH, DIK_BACKSLASH}, {GASM_KEY_KEY_Z, DIK_Z},
			{GASM_KEY_KEY_X, DIK_X}, {GASM_KEY_KEY_C, DIK_C}, {GASM_KEY_KEY_V, DIK_V}, {GASM_KEY_KEY_B, DIK_B},
			{GASM_KEY_KEY_N, DIK_N}, {GASM_KEY_KEY_M, DIK_M}, {GASM_KEY_COMMA, DIK_COMMA},
			{GASM_KEY_PERIOD, DIK_PERIOD}, {GASM_KEY_SLASH, DIK_SLASH}, {GASM_KEY_SHIFT_RIGHT, DIK_RSHIFT},
			{GASM_KEY_NUMPAD_MULTIPLY, DIK_MULTIPLY}, {GASM_KEY_ALT_LEFT, DIK_LMENU}, {GASM_KEY_SPACE, DIK_SPACE},
			{GASM_KEY_CAPS_LOCK, DIK_CAPITAL}, {GASM_KEY_NUM_LOCK, DIK_NUMLOCK}, {GASM_KEY_SCROLL_LOCK, DIK_SCROLL},
			{GASM_KEY_NUMPAD7, DIK_NUMPAD7}, {GASM_KEY_NUMPAD8, DIK_NUMPAD8}, {GASM_KEY_NUMPAD9, DIK_NUMPAD9},
			{GASM_KEY_NUMPAD_SUBTRACT, DIK_SUBTRACT}, {GASM_KEY_NUMPAD4, DIK_NUMPAD4},
			{GASM_KEY_NUMPAD5, DIK_NUMPAD5}, {GASM_KEY_NUMPAD6, DIK_NUMPAD6}, {GASM_KEY_NUMPAD_ADD, DIK_ADD},
			{GASM_KEY_NUMPAD1, DIK_NUMPAD1}, {GASM_KEY_NUMPAD2, DIK_NUMPAD2}, {GASM_KEY_NUMPAD3, DIK_NUMPAD3},
			{GASM_KEY_NUMPAD0, DIK_NUMPAD0}, {GASM_KEY_NUMPAD_DECIMAL, DIK_DECIMAL},
			{GASM_KEY_INTL_BACKSLASH, DIK_OEM_102}, {GASM_KEY_INTL_RO, DIK_ABNT_C1}, {GASM_KEY_INTL_YEN, DIK_YEN},
			{GASM_KEY_NUMPAD_EQUAL, DIK_NUMPADEQUALS}, {GASM_KEY_NUMPAD_ENTER, DIK_NUMPADENTER},
			{GASM_KEY_CONTROL_RIGHT, DIK_RCONTROL}, {GASM_KEY_NUMPAD_COMMA, DIK_NUMPADCOMMA},
			{GASM_KEY_NUMPAD_DIVIDE, DIK_DIVIDE}, {GASM_KEY_PRINT_SCREEN, DIK_SYSRQ}, {GASM_KEY_ALT_RIGHT, DIK_RMENU},
			{GASM_KEY_PAUSE, DIK_PAUSE}, {GASM_KEY_HOME, DIK_HOME}, {GASM_KEY_ARROW_UP, DIK_UP},
			{GASM_KEY_PAGE_UP, DIK_PRIOR}, {GASM_KEY_ARROW_LEFT, DIK_LEFT}, {GASM_KEY_ARROW_RIGHT, DIK_RIGHT},
			{GASM_KEY_END, DIK_END}, {GASM_KEY_ARROW_DOWN, DIK_DOWN}, {GASM_KEY_PAGE_DOWN, DIK_NEXT},
			{GASM_KEY_INSERT, DIK_INSERT}, {GASM_KEY_DELETE, DIK_DELETE}, {GASM_KEY_META_LEFT, DIK_LWIN},
			{GASM_KEY_META_RIGHT, DIK_RWIN}, {GASM_KEY_CONTEXT_MENU, DIK_APPS},
		};
		for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) gasmToDik[map[i].g] = (unsigned char)map[i].d;
	}

	// WM_CHAR delivered Enter as '\r', and Escape and Tab as characters too; gasm's text input
	// only has the printable text, '\b' and '\n'.
	void deliverText(const uint8_t *events, int nEvents)
	{
		if (!mainLoopObject) return;
		for (int i = 0; i < nEvents; i++)
		{
			const uint8_t *e = events + i * GASM_KEY_EVENT_BYTES;
			if (!e[2]) continue;
			int code = e[0] | (e[1] << 8);
			if (code == GASM_KEY_ESCAPE) mainLoopObject->textWrite(27);
			else if (code == GASM_KEY_TAB) mainLoopObject->textWrite(9);
		}
		char text[256];
		int n = gasm_text_input(text, sizeof(text));
		if (n <= 0 || n > (int)sizeof(text)) return;
		for (int i = 0; i < n; i++)
		{
			unsigned char c = (unsigned char)text[i];
			if (c == '\n') c = '\r';
			if (c >= 0x80)
			{
				// Windows-1252 was the game's charset: map Latin-1 code points, drop the rest.
				unsigned cp = 0; int len = 0;
				if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 1; }
				else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 2; }
				else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 3; }
				for (int k = 0; k < len && i + 1 < n; k++) cp = (cp << 6) | ((unsigned char)text[++i] & 0x3F);
				if (cp < 0xA0 || cp > 0xFF) continue;
				c = (unsigned char)cp;
			}
			mainLoopObject->textWrite(c);
		}
	}

	void pollInput()
	{
		uint8_t held[GASM_KEY_STATE_BYTES];
		memset(keysDI, 0, sizeof(keysDI));
		if (gasm_key_state(held, sizeof(held)) > 0)
		{
			for (int k = 0; k < GASM_KEY_STATE_BYTES * 8; k++)
				if ((held[k / 8] >> (k % 8)) & 1)
					if (gasmToDik[k]) keysDI[gasmToDik[k]] = 0x80;
		}
		static uint8_t events[1024];
		int len = gasm_key_events(events, sizeof(events));
		int nEvents = (len > 0 && len <= (int)sizeof(events)) ? len / GASM_KEY_EVENT_BYTES : 0;
		for (int i = 0; i < nEvents; i++)
		{
			const uint8_t *e = events + i * GASM_KEY_EVENT_BYTES;
			int code = e[0] | (e[1] << 8);
			if (e[2] && code < 256 && gasmToDik[code]) latchedDI[gasmToDik[code]] = 0x80;
		}

		uint8_t p[48];
		if (gasm_pointer(p, sizeof(p)) == 48)
		{
			float x, y, wy; uint32_t buttons, pressed;
			memcpy(&x, p + 0, 4); memcpy(&y, p + 4, 4);
			memcpy(&wy, p + 28, 4);
			memcpy(&buttons, p + 32, 4); memcpy(&pressed, p + 36, 4);
			// The window shows the game's frame scaled and centred (gl1_frame_rect, bottom-left
			// origin); the cursor is in the game's pixels, clamped to the frame as Windows did.
			int fx, fy, fw, fh;
			int w = gasm_gl_width(), h = gasm_gl_height();
			gl1_frame_rect(w, h, &fx, &fy, &fw, &fh);
			if (fw > 0 && fh > 0)
			{
				int top = h - (fy + fh);
				int cx = (int)floorf((x - fx) * resW / fw), cy = (int)floorf((y - top) * resH / fh);
				cursor[0] = cx < 0 ? 0 : cx >= resW ? resW - 1 : cx;
				cursor[1] = cy < 0 ? 0 : cy >= resH ? resH - 1 : cy;
			}
			mouse.rgbButtons[0] = (buttons & GASM_MOUSE_LEFT) ? 0x80 : 0;
			mouse.rgbButtons[1] = (buttons & GASM_MOUSE_RIGHT) ? 0x80 : 0;
			mouse.rgbButtons[2] = (buttons & GASM_MOUSE_MIDDLE) ? 0x80 : 0;
			mouse.rgbButtons[3] = (buttons & GASM_MOUSE_BACK) ? 0x80 : 0;
			mouse.rgbButtons[4] = (buttons & GASM_MOUSE_FORWARD) ? 0x80 : 0;
			latchedButtons |= pressed;
			wheelAccum += wy;
		}
		deliverText(events, nEvents);
	}
}

// --- the dkw API

int dkwInit(int width, int height, int colorDepth, char *title, CMainLoopInterface *mMainLoopObject, bool fullScreen, int refreshRate)
{
	(void)colorDepth; (void)fullScreen; (void)refreshRate;
	resW = width; resH = height;
	mainLoopObject = mMainLoopObject;
	memset(keysDI, 0, sizeof(keysDI));
	memset(latchedDI, 0, sizeof(latchedDI));
	memset(&mouse, 0, sizeof(mouse));
	initKeyTable();
	if (title) gasm_set_title_str(title);
	// The game draws its own cursor and reads every key itself.
	gasm_input_mode(GASM_INPUT_KEYS_RAW | GASM_INPUT_POINTER_HIDDEN);
	return 1;
}

void dkwForceQuit() { quitRequested = true; }
HDC dkwGetDC() { return 1; }
HWND dkwGetHandle() { return 1; }
HINSTANCE dkwGetInstance() { return 1; }
char *dkwGetLastError() { return lastError; }
CVector2i dkwGetCursorPos() { return cursor; }
CVector2i dkwGetResolution() { return CVector2i(resW, resH); }
int dkwMainLoop(bool *) { return !quitRequested; }
void dkwShutDown() {}
void dkwUpdate() {}
void dkwClipMouse(bool abEnabled) { clipMouse = abEnabled; }

// dki's joystick on Windows (DirectInput, axes in -1000..1000, read divided by 1000): an Xbox 360
// pad there has the left stick on X/Y, the triggers together on Z (left positive) and the right stick
// on Rx/Ry. gasm's first gamepad in the standard mapping gives the same. dki.cpp only has this on
// Windows, so it lives here.
static CVector3f readPad(bool right)
{
	uint8_t pad[204];
	if (gasm_gamepad(0, pad, sizeof(pad)) != (int)sizeof(pad)) return CVector3f(0, 0, 0);
	uint32_t flags;
	memcpy(&flags, pad, 4);
	if (!(flags & GASM_GAMEPAD_CONNECTED)) return CVector3f(0, 0, 0);
	float buttons[32], axes[16];
	memcpy(buttons, pad + 12, sizeof(buttons));
	memcpy(axes, pad + 12 + sizeof(buttons), sizeof(axes));
	if (right) return CVector3f(axes[2], axes[3], 0);
	return CVector3f(axes[0], axes[1], buttons[6] - buttons[7]);
}

CVector3f dkiGetJoy() { return readPad(false); }
CVector3f dkiGetJoyR() { return readPad(true); }

int GetClientRect(HWND, RECT *rect)
{
	rect->left = 0; rect->top = 0;
	rect->right = resW; rect->bottom = resH;
	return 1;
}

// DirectInput's mouse is relative; dki adds the deltas up and clamps to the screen. Handing it the
// distance from where it thinks the mouse is to the pointer keeps the two in step.
void dkwGetMouseState(DIMOUSESTATE2 *aMouseState)
{
	CVector2i at = dkiGetMouse();
	mouse.lX = cursor[0] - at[0];
	mouse.lY = cursor[1] - at[1];
	// DirectInput reports 120 per notch, up positive; gasm's wheel is +1 per notch downwards.
	mouse.lZ = (long)lroundf(-wheelAccum * 120.0f);
	wheelAccum = 0;
	DIMOUSESTATE2 out = mouse;
	for (int i = 0; i < 5; i++) if (latchedButtons & (1u << i)) out.rgbButtons[i] = 0x80;
	latchedButtons = 0;
	*aMouseState = out;
}

void dkwGetKeysState(unsigned char *aState, int aSize)
{
	for (int i = 0; i < aSize && i < 256; i++) aState[i] = keysDI[i] | latchedDI[i];
	memset(latchedDI, 0, sizeof(latchedDI));
}

// --- gasm entry points (main.cpp has the game's side: bv2_gasm_init, bv2_gasm_frame)

int bv2_gasm_init();
bool bv2_gasm_frame();
void bv2_gasm_exit();
extern "C" void *openbv_read_file(const char *path, size_t *size);

namespace
{
	bool running = false;
	const int FRAME_RATE = 60;
	int audioRate = 0;          // FSOUND_Init's rate (the s_mixRate cvar)
	float audioBuf[48000 / 10 * 2];
	int audioRemainder = 0;
}

GASM_EXPORT("gasm_abi_version") int32_t openbv_gasm_abi_version(void) { return GASM_ABI_VERSION; }

GASM_EXPORT("gasm_init") int32_t openbv_gasm_init(void)
{
	gasm_set_frame_rate(FRAME_RATE);
	fmodshim_set_reader(openbv_read_file);
	if (bv2_gasm_init() != 0) return 1;
	audioRate = fmodshim_mix_rate();
	if (audioRate < 8000 || audioRate > 48000) audioRate = 22050;
	gasm_audio_config(audioRate, 2);
	running = true;
	return 0;
}

GASM_EXPORT("gasm_frame") void openbv_gasm_frame(void)
{
	if (!running) return;
	pollInput();
	gl1_begin_frame(resW, resH);
	bool alive = bv2_gasm_frame() && !quitRequested;
	gl1_end_frame();

	// The same number of samples every second, whatever the frame (22050 / 60 = 367.5).
	int n = (audioRate + audioRemainder) / FRAME_RATE;
	audioRemainder = (audioRate + audioRemainder) % FRAME_RATE;
	fmodshim_render(audioBuf, n);
	gasm_audio_push(audioBuf, (uint32_t)n);

	if (!alive)
	{
		running = false;
		bv2_gasm_exit();
		__wasi_proc_exit(0);
	}
}

GASM_EXPORT("gasm_exit") void openbv_gasm_exit(void)
{
	if (!running) return;
	running = false;
	bv2_gasm_exit();
}
