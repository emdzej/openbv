/*
	openbv: the game's files on gasm.

	The original read and wrote files next to the exe ("main/bv2.cfg", "main/maps/x.bvm", ...). On gasm
	the shipped data are read-only assets (looked up case-insensitively by the runner) and anything the
	game writes goes to gasm:storage. A file the game wrote shadows the asset of the same name, so
	bv2.cfg, edited maps and the like read back as saved.

	fopen and the directory functions are wrapped at link time (-Wl,--wrap=...), so the vendored code
	keeps calling them unchanged.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include "gasm.h"
#include "gasm_vfile.h"
#include "embedded_assets.h"

#define OPENBV_PATH_MAX 512

/* "./main\\maps//x.bvm" -> "main/maps/x.bvm" */
static void normalize(const char *in, char *out, size_t cap)
{
	size_t n = 0;
	while (in[0] == '.' && (in[1] == '/' || in[1] == '\\')) in += 2;
	while (*in == '/' || *in == '\\') in++;
	for (; *in && n + 1 < cap; in++)
	{
		char c = *in == '\\' ? '/' : *in;
		if (c == '/' && n > 0 && out[n - 1] == '/') continue;
		out[n++] = c;
	}
	out[n] = 0;
}

/* Storage keys allow [A-Za-z0-9._-]: '/' becomes "--", the rest is lower-cased (the game's file names
   were case-insensitive, as on Windows). */
static int storage_key(const char *path, char *key, size_t cap)
{
	size_t n = 0;
	for (; *path; path++)
	{
		unsigned char c = (unsigned char)*path;
		if (c == '/')
		{
			if (n + 2 >= cap) return 0;
			key[n++] = '-'; key[n++] = '-';
			continue;
		}
		if (!(isalnum(c) || c == '.' || c == '_' || c == '-')) c = '_';
		if (n + 1 >= cap) return 0;
		key[n++] = (char)tolower(c);
	}
	key[n] = 0;
	return gasm_vfile_valid_key(key);
}

static int storage_has(const char *key)
{
	return gasm_storage_get(key, (uint32_t)strlen(key), 0, 0) >= 0;
}

static int asset_exists(const char *path)
{
	return gasm_asset_size64(path, (uint32_t)strlen(path)) >= 0;
}

FILE *__real_fopen(const char *path, const char *mode);

/* openbv's own UI files (embedded_assets.h), rebuilt into a TGA the first time they are opened:
   the original's header (uncompressed, 32 bits, bottom-up), white pixels with the stored alpha. */
static const unsigned char *embedded_file(const char *path, size_t *size)
{
	static unsigned char *built[16];
	static size_t builtSize[16];
	for (int i = 0; openbv_embedded_assets[i].name && i < 16; i++)
	{
		const struct openbv_embedded *e = &openbv_embedded_assets[i];
		if (strcasecmp(e->name, path) != 0) continue;
		if (!built[i])
		{
			size_t pixels = (size_t)e->width * e->height;
			unsigned char *t = (unsigned char *)malloc(18 + pixels * 4);
			if (!t) return NULL;
			memset(t, 0, 18);
			t[2] = 2;
			t[12] = e->width & 255; t[13] = e->width >> 8;
			t[14] = e->height & 255; t[15] = e->height >> 8;
			t[16] = 32; t[17] = 8;
			unsigned char *p = t + 18;
			size_t n = 0;
			for (unsigned int k = 0; k + 1 < e->rleSize && n < pixels; k += 2)
				for (int r = 0; r < e->alphaRle[k] && n < pixels; r++, n++)
				{
					p[n * 4 + 0] = p[n * 4 + 1] = p[n * 4 + 2] = 255;
					p[n * 4 + 3] = e->alphaRle[k + 1];
				}
			built[i] = t;
			builtSize[i] = 18 + pixels * 4;
		}
		*size = builtSize[i];
		return built[i];
	}
	return NULL;
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
	char p[OPENBV_PATH_MAX], key[256];
	normalize(path, p, sizeof(p));
	int writing = strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+');
	int haveKey = storage_key(p, key, sizeof(key));
	if (writing)
	{
		if (!haveKey) { errno = EINVAL; return NULL; }
		/* "a" and "r+" start from what is there: the stored copy, else the shipped one. */
		if ((strchr(mode, 'a') || (strchr(mode, 'r') && strchr(mode, '+'))) && !storage_has(key) && asset_exists(p))
		{
			FILE *src = gasm_vfile_open(GASM_VFILE_ASSET, p, "rb");
			FILE *dst = src ? gasm_vfile_open(GASM_VFILE_STORAGE, key, "wb") : NULL;
			if (dst)
			{
				char buf[4096];
				size_t got;
				while ((got = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, got, dst);
				fclose(dst);
			}
			if (src) fclose(src);
		}
		return gasm_vfile_open(GASM_VFILE_STORAGE, key, mode);
	}
	size_t esize;
	const unsigned char *e = embedded_file(p, &esize);
	if (e) return fmemopen((void *)e, esize, "rb");
	if (haveKey && storage_has(key)) return gasm_vfile_open(GASM_VFILE_STORAGE, key, mode);
	return gasm_vfile_open(GASM_VFILE_ASSET, p, mode);
}

int __wrap_remove(const char *path)
{
	char p[OPENBV_PATH_MAX], key[256];
	normalize(path, p, sizeof(p));
	if (!storage_key(p, key, sizeof(key)) || !storage_has(key)) { errno = ENOENT; return -1; }
	gasm_storage_delete(key, (uint32_t)strlen(key));
	return 0;
}

/* Whole file in memory, for the FMOD shim and texture loaders. */
void *openbv_read_file(const char *path, size_t *size)
{
	FILE *f = __wrap_fopen(path, "rb");
	if (!f) return NULL;
	size_t cap = 1 << 16, n = 0;
	unsigned char *buf = (unsigned char *)malloc(cap);
	for (;;)
	{
		if (n == cap)
		{
			unsigned char *grown = (unsigned char *)realloc(buf, cap * 2);
			if (!grown) { free(buf); fclose(f); return NULL; }
			buf = grown; cap *= 2;
		}
		size_t got = fread(buf + n, 1, cap - n, f);
		if (got == 0) break;
		n += got;
	}
	fclose(f);
	if (size) *size = n;
	return buf;
}

/* --- directories: the assets under a folder plus the stored files there */

struct openbv_dir
{
	char **names;
	int count, next;
	union { struct dirent e; char raw[sizeof(struct dirent) + 256]; } entry;   /* wasi's d_name is a flexible array */
};

static int add_name(struct openbv_dir *d, const char *name, int *cap)
{
	for (int i = 0; i < d->count; i++) if (strcasecmp(d->names[i], name) == 0) return 1;
	if (d->count == *cap)
	{
		*cap = *cap ? *cap * 2 : 32;
		char **grown = (char **)realloc(d->names, sizeof(char *) * (size_t)*cap);
		if (!grown) return 0;
		d->names = grown;
	}
	d->names[d->count++] = strdup(name);
	return 1;
}

DIR *__wrap_opendir(const char *path)
{
	char p[OPENBV_PATH_MAX], name[OPENBV_PATH_MAX];
	normalize(path, p, sizeof(p));
	size_t plen = strlen(p);
	while (plen && p[plen - 1] == '/') p[--plen] = 0;

	struct openbv_dir *d = (struct openbv_dir *)calloc(1, sizeof(*d));
	int cap = 0;
	uint32_t n = gasm_asset_count();
	for (uint32_t i = 0; i < n; i++)
	{
		int len = gasm_asset_name(i, name, sizeof(name) - 1);
		if (len <= 0 || len >= (int)sizeof(name)) continue;
		name[len] = 0;
		if (plen && (strncasecmp(name, p, plen) != 0 || name[plen] != '/')) continue;
		const char *rest = name + (plen ? plen + 1 : 0);
		char *slash = strchr(rest, '/');
		char entry[256];
		size_t elen = slash ? (size_t)(slash - rest) : strlen(rest);
		if (elen == 0 || elen >= sizeof(entry)) continue;
		memcpy(entry, rest, elen); entry[elen] = 0;
		add_name(d, entry, &cap);
	}
	char key[256], prefix[256];
	if (storage_key(plen ? p : "x", prefix, sizeof(prefix)))
	{
		if (plen) strcat(prefix, "--"); else prefix[0] = 0;
		size_t klen = strlen(prefix);
		uint32_t keys = gasm_storage_count();
		for (uint32_t i = 0; i < keys; i++)
		{
			int len = gasm_storage_key(i, key, sizeof(key) - 1);
			if (len <= 0 || len >= (int)sizeof(key)) continue;
			key[len] = 0;
			if (strncmp(key, prefix, klen) != 0) continue;
			if (strstr(key + klen, "--")) continue;
			add_name(d, key + klen, &cap);
		}
	}
	if (d->count == 0) { free(d); errno = ENOENT; return NULL; }
	return (DIR *)d;
}

struct dirent *__wrap_readdir(DIR *dir)
{
	struct openbv_dir *d = (struct openbv_dir *)dir;
	if (!d || d->next >= d->count) return NULL;
	memset(&d->entry, 0, sizeof(d->entry));
	strncpy(d->entry.e.d_name, d->names[d->next++], 255);
	d->entry.e.d_type = DT_REG;
	return &d->entry.e;
}

int __wrap_closedir(DIR *dir)
{
	struct openbv_dir *d = (struct openbv_dir *)dir;
	if (!d) return -1;
	for (int i = 0; i < d->count; i++) free(d->names[i]);
	free(d->names);
	free(d);
	return 0;
}

int __wrap_stat(const char *path, struct stat *st)
{
	char p[OPENBV_PATH_MAX], key[256];
	normalize(path, p, sizeof(p));
	memset(st, 0, sizeof(*st));
	size_t esize;
	if (embedded_file(p, &esize)) { st->st_mode = S_IFREG | 0444; st->st_size = esize; return 0; }
	if (storage_key(p, key, sizeof(key)))
	{
		int32_t len = gasm_storage_get(key, (uint32_t)strlen(key), 0, 0);
		if (len >= 0) { st->st_mode = S_IFREG | 0644; st->st_size = len; return 0; }
	}
	int64_t size = gasm_asset_size64(p, (uint32_t)strlen(p));
	if (size >= 0) { st->st_mode = S_IFREG | 0444; st->st_size = size; return 0; }
	errno = ENOENT;
	return -1;
}

void openbv_error(const char *msg)
{
	gasm_log_str(msg);
	fprintf(stderr, "%s\n", msg);
}
