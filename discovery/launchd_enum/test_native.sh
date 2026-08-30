#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODULE_DIR="$ROOT/discovery/launchd_enum"
RUNNER="$ROOT/tests/native_runner/build/native_runner"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "launchd_enum tests require macOS" >&2
  exit 1
fi

arch="$(uname -m)"
case "$arch" in
  arm64) DYLIB="$MODULE_DIR/build/launchd_enum-macos-arm64.dylib" ;;
  x86_64) DYLIB="$MODULE_DIR/build/launchd_enum-macos-x86_64.dylib" ;;
  *)
    echo "unsupported architecture: $arch" >&2
    exit 1
    ;;
esac

make -C "$MODULE_DIR" macos
make -C "$ROOT/tests/native_runner"

run_module() {
  HOME="$1" "$RUNNER" "$DYLIB" launchd_enum
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

EMPTY_HOME="$(mktemp -d "${TMPDIR:-/tmp}/launchd-enum-empty.XXXXXX")"
POPULATED_HOME="$(mktemp -d "${TMPDIR:-/tmp}/launchd-enum-populated.XXXXXX")"

cleanup() {
  rm -rf "$EMPTY_HOME" "$POPULATED_HOME"
}
trap cleanup EXIT

empty_output="$(run_module "$EMPTY_HOME")"
assert_contains "$empty_output" "[i] Indicator: User LaunchAgents directory"
assert_contains "$empty_output" "[i] Indicator: User LaunchDaemons directory"
assert_contains "$empty_output" "Status: absent"
assert_contains "$empty_output" "User persistence surface: absent"
assert_not_contains "$empty_output" "    Modified:"
assert_not_contains "$empty_output" "epoch"

empty_score="$(extract_score "$empty_output")"
if [[ -z "$empty_score" ]]; then
  echo "failed to parse score from empty-home output" >&2
  exit 1
fi
if [[ "$empty_score" != "0" ]]; then
  echo "expected empty-home score 0, got $empty_score" >&2
  exit 1
fi

mkdir -p "$POPULATED_HOME/Library/LaunchAgents"
mkdir -p "$POPULATED_HOME/Library/LaunchDaemons"

both_present_output="$(run_module "$POPULATED_HOME")"
assert_contains "$both_present_output" "[+] Indicator: User LaunchAgents directory"
assert_contains "$both_present_output" "[+] Indicator: User LaunchDaemons directory"
assert_contains "$both_present_output" "LaunchAgents"
assert_contains "$both_present_output" "LaunchDaemons"
assert_contains "$both_present_output" "Status: present"
assert_contains "$both_present_output" "    Modified:"
assert_contains "$both_present_output" "(epoch "
assert_contains "$both_present_output" "User persistence surface: present"
assert_not_contains "$both_present_output" "[i] Indicator: User LaunchAgents directory"
assert_not_contains "$both_present_output" "[i] Indicator: User LaunchDaemons directory"

both_present_score="$(extract_score "$both_present_output")"
if [[ -z "$both_present_score" ]]; then
  echo "failed to parse score from both-present output" >&2
  exit 1
fi
if [[ "$both_present_score" != "8" ]]; then
  echo "expected both-present score 8, got $both_present_score" >&2
  exit 1
fi

echo "launchd_enum native_runner tests passed"
