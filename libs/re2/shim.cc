/*
 * lust.re2: the C side. A thin C ABI over RE2 (vendor/re2, linked
 * statically together with Abseil). It knows nothing about Lua: re2.lua
 * binds these functions through the LuaJIT FFI.
 *
 * Why a shim at all: RE2 is a C++ API (RE2 objects, absl::string_view,
 * overloaded templates), which the FFI cannot bind. Everything that crosses
 * the boundary is a plain C type:
 *   - a regexp is an opaque `lust_re2 *`,
 *   - offsets are doubles (exact up to 2^53, and directly usable as Lua
 *     numbers, unlike 64 bit cdata integers),
 *   - no exception ever escapes (they would be undefined behaviour across
 *     the C boundary), failures are reported through return values.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>
#include <string>
#include <vector>

// The Lua headers are only needed for LUST_API.
extern "C" {
#include "lust.h"
}

#include "re2/re2.h"

namespace {

// Keep in sync with re2.lua (the `flags` table).
enum : unsigned {
  F_LITERAL = 1u << 0,
  F_CASELESS = 1u << 1,
  F_LONGEST = 1u << 2,
  F_POSIX = 1u << 3,
  F_NEVER_NL = 1u << 4,
  F_DOT_NL = 1u << 5,
  F_NEVER_CAPTURE = 1u << 6,
  F_PERL_CLASSES = 1u << 7,
  F_WORD_BOUNDARY = 1u << 8,
  F_ONE_LINE = 1u << 9,
  F_LATIN1 = 1u << 10,
};

}  // namespace

struct lust_re2 {
  explicit lust_re2(const RE2::Options &opts, absl::string_view pattern)
      : re(pattern, opts) {}

  RE2 re;
};

// Copy `msg` into the caller's error buffer (truncated, always terminated).
static void set_error(char *err, size_t errsz, const char *msg) {
  if (err == nullptr || errsz == 0) return;
  std::snprintf(err, errsz, "%s", msg);
}

extern "C" {

/*
 * Compile `pat`. Returns NULL on failure, with a message in `err`.
 * `max_mem` is RE2's memory budget in bytes (<= 0: RE2's default, 8 MiB).
 */
LUST_API lust_re2 *lust_re2_new(const char *pat, size_t len, unsigned flags,
                                double max_mem, char *err, size_t errsz) {
  try {
    RE2::Options o;
    // RE2 would log compile errors to stderr; we hand them to the caller.
    o.set_log_errors(false);
    o.set_literal(flags & F_LITERAL);
    o.set_case_sensitive(!(flags & F_CASELESS));
    o.set_longest_match(flags & F_LONGEST);
    o.set_posix_syntax(flags & F_POSIX);
    o.set_never_nl(flags & F_NEVER_NL);
    o.set_dot_nl(flags & F_DOT_NL);
    o.set_never_capture(flags & F_NEVER_CAPTURE);
    o.set_perl_classes(flags & F_PERL_CLASSES);
    o.set_word_boundary(flags & F_WORD_BOUNDARY);
    o.set_one_line(flags & F_ONE_LINE);
    if (flags & F_LATIN1) o.set_encoding(RE2::Options::EncodingLatin1);
    if (max_mem > 0) o.set_max_mem(static_cast<int64_t>(max_mem));

    lust_re2 *r = new lust_re2(o, absl::string_view(pat, len));
    if (!r->re.ok()) {
      set_error(err, errsz, r->re.error().c_str());
      delete r;
      return nullptr;
    }
    return r;
  } catch (const std::exception &e) {
    set_error(err, errsz, e.what());
  } catch (...) {
    set_error(err, errsz, "unknown error");
  }
  return nullptr;
}

LUST_API void lust_re2_free(lust_re2 *r) { delete r; }

/* Number of capturing groups (not counting the whole match). */
LUST_API int lust_re2_ngroups(const lust_re2 *r) {
  return r->re.NumberOfCapturingGroups();
}

/*
 * Names of the capturing groups, as a buffer holding one NUL terminated
 * name per group, in group order (1 .. ngroups); "" for a group without a
 * name. Returns the size the buffer needs; the buffer is filled only if it
 * is large enough, so call it once with (NULL, 0) to find out.
 */
LUST_API size_t lust_re2_group_names(const lust_re2 *r, char *buf,
                                     size_t bufsz) {
  try {
    const int n = r->re.NumberOfCapturingGroups();
    // name -> index, in the order of the names; looked up per group
    const auto &byname = r->re.NamedCapturingGroups();
    size_t need = 0;
    for (int i = 1; i <= n; i++) {
      const std::string *name = nullptr;
      for (const auto &kv : byname)
        if (kv.second == i) name = &kv.first;
      const size_t len = name ? name->size() : 0;
      if (need + len + 1 <= bufsz && buf != nullptr) {
        std::memcpy(buf + need, name ? name->data() : "", len);
        buf[need + len] = '\0';
      }
      need += len + 1;
    }
    return need;
  } catch (...) {
    return 0;
  }
}

/* Size of the compiled program (a measure of the regexp's complexity). */
LUST_API int lust_re2_program_size(const lust_re2 *r) {
  return r->re.ProgramSize();
}

/*
 * Match s[0, len) starting at `startpos`; the text before startpos still
 * counts as context for ^ and \b.
 *
 *   anchor  0: unanchored, 1: anchored at startpos, 2: anchored at both ends
 *   nsub    how many groups to report, 0 .. ngroups + 1 (group 0 is the
 *           whole match); nsub == 0 only tests for a match
 *   out     2 * nsub doubles: the 0-based start and end (exclusive) offset
 *           of each group, both -1 for a group that did not participate
 *
 * Returns 1 on a match, 0 if there is none and -1 on an error (invalid
 * arguments, out of memory).
 */
LUST_API int lust_re2_match(const lust_re2 *r, const char *s, size_t len,
                            size_t startpos, int anchor, int nsub,
                            double *out) {
  if (nsub < 0 || nsub > r->re.NumberOfCapturingGroups() + 1 || startpos > len)
    return -1;
  RE2::Anchor a;
  switch (anchor) {
    case 0: a = RE2::UNANCHORED; break;
    case 1: a = RE2::ANCHOR_START; break;
    case 2: a = RE2::ANCHOR_BOTH; break;
    default: return -1;
  }

  try {
    // Most calls ask for few groups: avoid the heap for those.
    absl::string_view small[8];
    std::vector<absl::string_view> big;
    absl::string_view *sub = small;
    if (nsub > 8) {
      big.resize(nsub);
      sub = big.data();
    }

    if (!r->re.Match(absl::string_view(s, len), startpos, len, a, sub, nsub))
      return 0;
    for (int i = 0; i < nsub; i++) {
      if (sub[i].data() == nullptr) {
        out[2 * i] = out[2 * i + 1] = -1;
      } else {
        out[2 * i] = static_cast<double>(sub[i].data() - s);
        out[2 * i + 1] = static_cast<double>(sub[i].data() - s + sub[i].size());
      }
    }
    return 1;
  } catch (...) {
    return -1;
  }
}

}  // extern "C"
