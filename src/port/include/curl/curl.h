// openbv: the part of libcurl's easy API that BV2 uses (CCurl.cpp: posts to the account server).
// Implemented by src/port/curl_shim.cpp.
#ifndef OPENBV_CURL_H
#define OPENBV_CURL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void CURL;
typedef int CURLcode;
typedef int CURLoption;

#define CURLE_OK 0
#define CURLE_COULDNT_CONNECT 7

#define CURLOPT_WRITEDATA       10001
#define CURLOPT_URL             10002
#define CURLOPT_POSTFIELDS      10015
#define CURLOPT_WRITEFUNCTION   20011
#define CURLOPT_TIMEOUT         13
#define CURLOPT_CONNECTTIMEOUT  78
#define CURLOPT_NOSIGNAL        99

CURL    *curl_easy_init(void);
CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...);
CURLcode curl_easy_perform(CURL *curl);
void     curl_easy_cleanup(CURL *curl);
char    *curl_easy_escape(CURL *curl, const char *string, int length);
char    *curl_escape(const char *string, int length);
void     curl_free(void *p);

#ifdef __cplusplus
}
#endif

#endif
