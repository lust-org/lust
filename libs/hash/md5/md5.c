/*
 * lust.hash.md5: MD5 (Mbed TLS, statically linked from
 * vendor/mbedtls/tf-psa-crypto). The Lua API is in ../mbedtls_hash.h.
 *
 *   local md5 = require "lust.hash.md5"
 *   md5.hex("abc")    --> "900150983cd24fb0d6963f7d28e17f72"
 *   md5.digest("abc") --> the same 16 bytes, raw
 *   md5.new():update("a"):update("bc"):hex() --> the same as hex("abc")
 */
/* TF-PSA-Crypto 1.x calls the low-level hash API private (declared only
 * with this macro); the public one is PSA, which would link its whole
 * core (key store, RNG, driver dispatch) just to hash. The vendored
 * version is pinned, so an upgrade that changes this API fails to build. */
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "mbedtls/private/md5.h"

#define HASH_NAME "md5"
#define HASH_SIZE 16
typedef mbedtls_md5_context HashCtx;

static int hash_start(HashCtx *c) {
  mbedtls_md5_init(c);
  return mbedtls_md5_starts(c);
}

static int hash_update(HashCtx *c, const unsigned char *p, size_t n) {
  return mbedtls_md5_update(c, p, n);
}

static int hash_finish(HashCtx *c, unsigned char *out) {
  return mbedtls_md5_finish(c, out);
}

#include "../mbedtls_hash.h"

LUST_API int luaopen_lust_hash_md5(lua_State *L) { return hash_open(L); }
