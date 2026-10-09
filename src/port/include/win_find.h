// openbv: the Win32 directory search (FindFirstFile, FindNextFile, FindClose) and _getcwd, which the
// map lists use (CHost.cpp, Map.cpp, Server.cpp). src/port/win_find.cpp runs them over the game's
// files (src/port/fs_gasm.c), in the order NTFS returned them: case-insensitive by name.
#ifndef OPENBV_WIN_FIND_H
#define OPENBV_WIN_FIND_H

typedef unsigned int DWORD;
typedef void *HANDLE;

#define MAX_PATH 260
#define _MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define ERROR_NO_MORE_FILES 18
#define ERROR_FILE_NOT_FOUND 2

struct WIN32_FIND_DATA
{
	DWORD dwFileAttributes;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	char cFileName[MAX_PATH];
};

HANDLE FindFirstFile(const char *spec, WIN32_FIND_DATA *data);
int FindNextFile(HANDLE find, WIN32_FIND_DATA *data);
int FindClose(HANDLE find);
DWORD GetLastError();
char *_getcwd(char *buf, int size);

#endif
