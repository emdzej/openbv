// openbv: replaces the original LinuxHeader.h. The original pulled in the BSD socket, ioctl and
// pthread headers; on gasm there are none (networking is engine/babonet over gasm:net), so this keeps
// only the C library part and the Windows names the code relies on.
#ifndef _LINUX_HEADER_H
#define _LINUX_HEADER_H

	#include <sys/time.h>
	#include <sys/types.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <unistd.h>
	#include <fcntl.h>
	#include <string.h>
	#include <strings.h>
	#include <errno.h>
	#include <time.h>
	#include <ctime>
	#include <stdint.h>

	#include "linux_types.h"
	#include "win_find.h"

	// The window's client area (CMenuManager.cpp): the game's resolution (src/port/dkw_gasm.cpp).
	int GetClientRect(HWND hWnd, RECT *rect);
	#include <iostream>
	#include <algorithm>

	// windows.h's min and max, which the code uses unqualified.
	using std::min;
	using std::max;

	#define stricmp strcasecmp
	#define strnicmp strncasecmp
	#define amax(X,Y) ((X) < (Y) ? (Y) : (X))

	// The paint loop swaps buffers itself on Windows; gasm presents at the end of the frame.
	#define SwapBuffers(dc) ((void)(dc))

#endif
