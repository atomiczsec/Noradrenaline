#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODULE_DIR="$ROOT/discovery/tcc_privacy_surface"
RUNNER="$ROOT/tests/native_runner/build/native_runner"
FIXTURE_ROOT="$MODULE_DIR/fixtures"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "tcc_privacy_surface tests require macOS" >&2
  exit 1
fi

arch="$(uname -m)"
case "$arch" in
  arm64) DYLIB="$MODULE_DIR/build/tcc_privacy_surface-macos-arm64.dylib" ;;
  x86_64) DYLIB="$MODULE_DIR/build/tcc_privacy_surface-macos-x86_64.dylib" ;;
  *)
    echo "unsupported architecture: $arch" >&2
    exit 1
    ;;
esac

make -C "$MODULE_DIR" macos
make -C "$ROOT/tests/native_runner"

run_module() {
  HOME="$1" "$RUNNER" "$DYLIB" tcc_privacy_surface
}

assert_contains() {
  local haystack="$1"
  local needle="$2"
  if ! printf '%s' "$haystack" | grep -Fq "$needle"; then
    echo "expected output to contain: $needle" >&2
    printf '%s\n' "$haystack" >&2
    exit 1
  fi
}

assert_not_contains() {
  local haystack="$1"
  local needle="$2"
  if printf '%s' "$haystack" | grep -Fq "$needle"; then
    echo "expected output not to contain: $needle" >&2
    printf '%s\n' "$haystack" >&2
    exit 1
  fi
}

extract_score() {
  printf '%s\n' "$1" | sed -n 's/.*Score: \([0-9][0-9]*\)\/10.*/\1/p' | tail -1
}

EMPTY_HOME="$(mktemp -d "${TMPDIR:-/tmp}/tcc-privacy-empty.XXXXXX")"
POPULATED_HOME="$(mktemp -d "${TMPDIR:-/tmp}/tcc-privacy-populated.XXXXXX")"

cleanup() {
  rm -rf "$EMPTY_HOME" "$POPULATED_HOME"
}
trap cleanup EXIT

mkdir -p "$EMPTY_HOME/Library/Application Support/com.apple.TCC"
mkdir -p "$EMPTY_HOME/Library/Preferences"

mkdir -p "$POPULATED_HOME/Library/Application Support/com.apple.TCC"
mkdir -p "$POPULATED_HOME/Library/Preferences"
: > "$POPULATED_HOME/Library/Application Support/com.apple.TCC/TCC.db"
: > "$POPULATED_HOME/Library/Preferences/com.apple.security.FDERecoveryKeyEscrow.plist"
: > "$POPULATED_HOME/Library/Preferences/com.apple.ScreenTimeAgent.plist"

empty_output="$(run_module "$EMPTY_HOME")"
assert_contains "$empty_output" "[i] Indicator: User TCC database"
assert_contains "$empty_output" "Status: absent"
assert_contains "$empty_output" "[i] Indicator: FileVault FDE escrow preferences"
assert_contains "$empty_output" "[i] Indicator: Screen Time preferences"
assert_contains "$empty_output" "Privacy posture: Permissive"

empty_score="$(extract_score "$empty_output")"
if [[ -z "$empty_score" ]]; then
  echo "failed to parse score from empty-home output" >&2
  exit 1
fi
if [[ "$empty_score" -gt 4 ]]; then
  echo "expected empty-home score <= 4, got $empty_score" >&2
  exit 1
fi

populated_output="$(run_module "$POPULATED_HOME")"
assert_contains "$populated_output" "[+] Indicator: User TCC database"
assert_contains "$populated_output" "TCC.db"
assert_contains "$populated_output" "Status: present"
assert_contains "$populated_output" "[+] Indicator: FileVault FDE escrow preferences"
assert_contains "$populated_output" "FDERecoveryKeyEscrow.plist"
assert_contains "$populated_output" "[+] Indicator: Screen Time preferences"
assert_not_contains "$populated_output" "[i] Indicator: User TCC database"

populated_score="$(extract_score "$populated_output")"
if [[ -z "$populated_score" ]]; then
  echo "failed to parse score from populated-home output" >&2
  exit 1
fi
if [[ "$populated_score" -lt 6 ]]; then
  echo "expected populated-home score >= 6, got $populated_score" >&2
  exit 1
fi

if printf '%s\n' "$empty_output" | grep -Fq "[!] Indicator: System Integrity Protection"; then
  assert_not_contains "$empty_output" "    Score: +0"
else
  assert_contains "$empty_output" "[+] Indicator: System Integrity Protection"
fi

echo "tcc_privacy_surface native_runner tests passed"
