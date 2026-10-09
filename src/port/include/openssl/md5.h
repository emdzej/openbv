// openbv: CMaster.cpp hashes account passwords with OpenSSL's one-shot MD5(); the game already
// carries an MD5 (XySSL's, src/game/md5_2.c), so this maps the call onto it.
#ifndef OPENBV_OPENSSL_MD5_H
#define OPENBV_OPENSSL_MD5_H
#include <stddef.h>
#include "md5_2.h"
static inline unsigned char *MD5(const unsigned char *d, size_t n, unsigned char *md)
{
	md5((unsigned char *)d, (int)n, md);
	return md;
}
#endif
