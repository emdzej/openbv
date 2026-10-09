/*
	openbv: libcurl's easy API for CCurl.cpp.

	BV2 posted account, friends and status requests to the RndLabs account server (gameVar.db_accountServer,
	from bv2.db), which no longer exists. Requests fail as an unreachable host did (CURLE_COULDNT_CONNECT,
	nothing written), so the game takes its no-response paths. Escaping is real.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#include "curl/curl.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace { int handle; }

extern "C" CURL *curl_easy_init(void) { return &handle; }

extern "C" CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...)
{
	(void)curl; (void)option;
	return CURLE_OK;
}

extern "C" CURLcode curl_easy_perform(CURL *curl)
{
	(void)curl;
	return CURLE_COULDNT_CONNECT;
}

extern "C" void curl_easy_cleanup(CURL *curl) { (void)curl; }

// RFC 3986 unreserved characters stay, the rest becomes %XX (curl_easy_escape's rule).
extern "C" char *curl_easy_escape(CURL *curl, const char *string, int length)
{
	(void)curl;
	size_t n = length > 0 ? (size_t)length : strlen(string);
	char *out = (char *)malloc(n * 3 + 1), *o = out;
	static const char hex[] = "0123456789ABCDEF";
	for (size_t i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char)string[i];
		if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') *o++ = (char)c;
		else { *o++ = '%'; *o++ = hex[c >> 4]; *o++ = hex[c & 15]; }
	}
	*o = 0;
	return out;
}

extern "C" char *curl_escape(const char *string, int length) { return curl_easy_escape(0, string, length); }

extern "C" void curl_free(void *p) { free(p); }
