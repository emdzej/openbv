/*
	openbv: FindFirstFile and friends over the game's files (see win_find.h).

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#include "win_find.h"
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <string>
#include <vector>
#include <algorithm>

namespace
{
	DWORD lastError = 0;

	struct Search
	{
		std::vector<std::string> names;
		size_t next;
	};

	// Wildcards as Win32 matches file names: '*' any run, '?' one character, case-insensitive.
	bool match(const char *pattern, const char *name)
	{
		if (!*pattern) return !*name;
		if (*pattern == '*') return match(pattern + 1, name) || (*name && match(pattern, name + 1));
		if (!*name) return false;
		if (*pattern != '?' && tolower((unsigned char)*pattern) != tolower((unsigned char)*name)) return false;
		return match(pattern + 1, name + 1);
	}

	// NTFS orders a directory by the upper-cased names.
	bool ntfsLess(const std::string &a, const std::string &b)
	{
		size_t n = std::min(a.size(), b.size());
		for (size_t i = 0; i < n; i++)
		{
			int ca = toupper((unsigned char)a[i]), cb = toupper((unsigned char)b[i]);
			if (ca != cb) return ca < cb;
		}
		return a.size() < b.size();
	}

	void fill(Search *s, WIN32_FIND_DATA *data)
	{
		memset(data, 0, sizeof(*data));
		data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
		strncpy(data->cFileName, s->names[s->next].c_str(), MAX_PATH - 1);
		s->next++;
	}
}

HANDLE FindFirstFile(const char *spec, WIN32_FIND_DATA *data)
{
	std::string path(spec);
	for (size_t i = 0; i < path.size(); i++) if (path[i] == '\\') path[i] = '/';
	size_t slash = path.rfind('/');
	std::string dir = slash == std::string::npos ? "" : path.substr(0, slash);
	std::string pattern = slash == std::string::npos ? path : path.substr(slash + 1);

	Search *s = new Search;
	s->next = 0;
	DIR *d = opendir(dir.c_str());
	if (d)
	{
		struct dirent *e;
		while ((e = readdir(d)) != 0)
			if (match(pattern.c_str(), e->d_name)) s->names.push_back(e->d_name);
		closedir(d);
	}
	if (s->names.empty())
	{
		delete s;
		lastError = ERROR_FILE_NOT_FOUND;
		return INVALID_HANDLE_VALUE;
	}
	std::sort(s->names.begin(), s->names.end(), ntfsLess);
	fill(s, data);
	return (HANDLE)s;
}

int FindNextFile(HANDLE find, WIN32_FIND_DATA *data)
{
	Search *s = (Search *)find;
	if (!s || s->next >= s->names.size()) { lastError = ERROR_NO_MORE_FILES; return 0; }
	fill(s, data);
	return 1;
}

int FindClose(HANDLE find)
{
	delete (Search *)find;
	return 1;
}

DWORD GetLastError() { return lastError; }

// The game's files are relative to its folder: the "current directory" is the root of the assets.
char *_getcwd(char *buf, int size)
{
	if (buf && size > 0) buf[0] = 0;
	return buf;
}
