/*
	openbv: a stdio file reader for the FMOD shim, for native tests and tools
	(the gasm build reads assets instead). Names are relative to a root;
	when the exact path is missing each component is matched ignoring case,
	as the Windows original found its files.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#include "fmodshim.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char root_dir[1024] = ".";

void fmodshim_stdio_root(const char *root)
{
	snprintf(root_dir, sizeof root_dir, "%s", root && *root ? root : ".");
}

static FILE *open_nocase(const char *name)
{
	char path[2048], part[256];
	const char *p = name;
	FILE *f;
	snprintf(path, sizeof path, "%s/%s", root_dir, name);
	if ((f = fopen(path, "rb")) != 0) return f;
	snprintf(path, sizeof path, "%s", root_dir);
	while (*p)
	{
		size_t n = strcspn(p, "/\\");
		DIR *d;
		struct dirent *e;
		int found = 0;
		if (n == 0 || n >= sizeof part) { p += n ? n : 1; continue; }
		memcpy(part, p, n);
		part[n] = 0;
		p += n;
		if (*p) p++;
		if (!(d = opendir(path))) return 0;
		while ((e = readdir(d)) != 0)
			if (!strcasecmp(e->d_name, part))
			{
				size_t l = strlen(path);
				snprintf(path + l, sizeof path - l, "/%s", e->d_name);
				found = 1;
				break;
			}
		closedir(d);
		if (!found) return 0;
	}
	return fopen(path, "rb");
}

void *fmodshim_stdio_read(const char *name, size_t *size)
{
	FILE *f = open_nocase(name);
	long n;
	void *buf;
	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(n > 0 ? (size_t)n : 1);
	if (buf && n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = 0; }
	fclose(f);
	if (buf && size) *size = (size_t)(n > 0 ? n : 0);
	return buf;
}
