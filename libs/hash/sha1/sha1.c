/*
 * lust.hash.sha1: SHA-1 (Mbed TLS, statically linked from
 * vendor/mbedtls/tf-psa-crypto). The Lua API is in ../mbedtls_hash.h.
 *
 *   local sha1 = require "lust.hash.sha1"
 *   sha1.hex("abc")    --> "a9993e364706816aba3e25717850c26c9cd0d89d"
 *   sha1.digest("abc") --> the same 20 bytes, raw
 *   sha1.new():update("a"):update("bc"):hex() --> the same as hex("abc")
 */
/* TF-PSA-Crypto 1.x calls the low-level hash API private (declared only
 * with this macro); the public one is PSA, which would link its whole
 * core (key store, RNG, driver dispatch) just to hash. The vendored
 * version is pinned, so an upgrade that changes this API fails to build. */
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "mbedtls/private/sha1.h"

#define HASH_NAME "sha1"
#define HASH_SIZE 20
typedef mbedtls_sha1_context HashCtx;

static int hash_start(HashCtx *c) {
  mbedtls_sha1_init(c);
  return mbedtls_sha1_starts(c);
}

static int hash_update(HashCtx *c, const unsigned char *p, size_t n) {
  return mbedtls_sha1_update(c, p, n);
}

static int hash_finish(HashCtx *c, unsigned char *out) {
  return mbedtls_sha1_finish(c, out);
}

#include "../mbedtls_hash.h"

LUST_API int luaopen_lust_hash_sha1(lua_State *L) { return hash_open(L); }
