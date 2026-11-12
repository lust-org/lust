/*
 * lust.hash.sha256: SHA-256 (Mbed TLS, statically linked from
 * vendor/mbedtls/tf-psa-crypto). The Lua API is in ../mbedtls_hash.h.
 *
 *   local sha256 = require "lust.hash.sha256"
 *   sha256.hex("abc")    --> "ba7816bf...f20015ad"
 *   sha256.digest("abc") --> the same 32 bytes, raw
 *   sha256.new():update("a"):update("bc"):hex() --> the same as hex("abc")
 */
/* TF-PSA-Crypto 1.x calls the low-level hash API private (declared only
 * with this macro); the public one is PSA, which would link its whole
 * core (key store, RNG, driver dispatch) just to hash. The vendored
 * version is pinned, so an upgrade that changes this API fails to build. */
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "mbedtls/private/sha256.h"

#define HASH_NAME "sha256"
#define HASH_SIZE 32
typedef mbedtls_sha256_context HashCtx;

static int hash_start(HashCtx *c) {
  mbedtls_sha256_init(c);
  return mbedtls_sha256_starts(c, 0);
}

static int hash_update(HashCtx *c, const unsigned char *p, size_t n) {
  return mbedtls_sha256_update(c, p, n);
}

static int hash_finish(HashCtx *c, unsigned char *out) {
  return mbedtls_sha256_finish(c, out);
}

#include "../mbedtls_hash.h"

LUST_API int luaopen_lust_hash_sha256(lua_State *L) { return hash_open(L); }
