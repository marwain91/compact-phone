#!/usr/bin/env bash
#
# PJSIP containment gate (phase 6 of the SIP backend abstraction).
#
# Fails if any PJSIP/PJSUA2 usage appears outside the PJSIP adapter. After
# phases 1-5 every pj::/pjsua2/pjsip symbol and every <pjsua2>/<pj*> include
# lives in src/core/sipbackend/pjsip/ (the adapter) or cmake/FindPJSIP.cmake.
# This script enforces that invariant so a future change can't quietly reach
# around ISipBackend to the SIP stack again.
#
# It is a lexer-lite, not a full parser: a small awk state machine removes //
# and /* */ comments AND blanks the contents of "..." string and '...' char
# literals (tracking escapes), so neither a pj:: *mentioned in a comment*
# (legitimate; the codebase does this) nor a comment-marker that happens to sit
# inside a string literal (e.g. "sip://x" or "a /* b") can hide a real symbol
# on the same or a following line. Line numbers are preserved.
#
# Runs in CI (.github/workflows/pjsip-containment.yml) and locally
# (`make check-containment`). Exit 0 = clean, 1 = leak found.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

ADAPTER='src/core/sipbackend/pjsip/'

# Tracked C/C++ sources under src/, excluding the adapter directory itself.
mapfile -t FILES < <(
  git ls-files -- \
    'src/*.h' 'src/*.hpp' 'src/*.hh' 'src/*.inl' 'src/*.ipp' 'src/*.tpp' \
    'src/*.cpp' 'src/*.cc' 'src/*.cxx' 'src/*.c' 'src/*.mm' 'src/*.m' \
    | grep -v "^${ADAPTER}" || true
)

# Strip comments and blank string/char-literal contents, preserving line count
# so reported line numbers stay accurate. inblk (block comment) persists across
# lines; strings/chars do not span lines in normal code, so they reset per line.
strip_noncode() {
  awk '
  {
    line = $0; out = ""; i = 1; n = length(line); instr = 0; inchr = 0
    while (i <= n) {
      c = substr(line, i, 1); two = substr(line, i, 2)
      if (inblk)      { if (two == "*/") { inblk = 0; i += 2 } else { i++ } }
      else if (instr) { if (c == "\\") { i += 2 }                       # escape
                        else if (c == "\"") { instr = 0; i++ } else { i++ } }
      else if (inchr) { if (c == "\\") { i += 2 }
                        else if (c == "'\''") { inchr = 0; i++ } else { i++ } }
      else if (two == "/*") { inblk = 1; i += 2 }
      else if (two == "//") { break }                # rest of line is a comment
      else if (c == "\"")   { instr = 1; i++ }        # enter string; drop body
      else if (c == "'\''") { inchr = 1; i++ }        # enter char; drop body
      else { out = out c; i++ }
    }
    print out
  }' "$1"
}

# A pj-stack header include. PJSIP headers are third-party system headers,
# always angle-bracket includes; our own (quoted) "core/sipbackend/pjsip/..."
# adapter-header includes are NOT pj headers and must not trip this.
INC_RE='^[[:space:]]*#[[:space:]]*include[[:space:]]*<(pjsua2|pjsua-lib/|pjsip|pjmedia|pjnath|pjlib|pj/)'
# A pj-stack symbol in code. CamelCase adapter type names (PjsipBackend, etc.)
# and our namespaces (sip::, sipbackend::) do not match these; PJ[A-Z0-9]*_
# covers PJ_*, PJSUA_*, PJSUA2_*, PJSIP_*, PJMEDIA_*, PJNATH_*, PJLIB_*, ...
SYM_RE='\bpj::|\bpjsua_|\bpjsip_|\bpjmedia_|\bpjnath_|\bpj_[a-z]|\bPJ[A-Z0-9]*_|\busing[[:space:]]+namespace[[:space:]]+pj\b'

fail=0
for f in "${FILES[@]:-}"; do
  [ -n "$f" ] || continue
  hits="$(strip_noncode "$f" | grep -nE "${INC_RE}|${SYM_RE}" || true)"
  if [ -n "$hits" ]; then
    echo "✗ PJSIP usage outside the adapter: $f"
    printf '%s\n' "$hits" | sed 's/^/    /'
    fail=1
  fi
done

if [ "$fail" -ne 0 ]; then
  cat >&2 <<'EOF'

FAIL: PJSIP/PJSUA2 must stay confined to src/core/sipbackend/pjsip/ (the
adapter) and cmake/FindPJSIP.cmake. The file(s) above reach the SIP stack
directly — route them through the ISipBackend interface instead. See CLAUDE.md
("the SIP stack abstraction").
EOF
  exit 1
fi

echo "OK: no PJSIP/PJSUA2 usage outside src/core/sipbackend/pjsip/."
