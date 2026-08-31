#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODULE_DIR="$ROOT/credential_access/cicd_credential_hunt"
RUNNER="$ROOT/tests/native_runner/build/native_runner"

arch="$(uname -m)"
os="$(uname -s)"

case "$os" in
  Darwin)
    case "$arch" in
      arm64) LIB="$MODULE_DIR/build/cicd_credential_hunt-macos-arm64.dylib" ;;
      x86_64) LIB="$MODULE_DIR/build/cicd_credential_hunt-macos-x86_64.dylib" ;;
      *)
        echo "unsupported architecture: $arch" >&2
        exit 1
        ;;
    esac
    make -C "$MODULE_DIR" macos
    ;;
  Linux)
    case "$arch" in
      x86_64|amd64) LIB="$MODULE_DIR/build/cicd_credential_hunt-linux-x86_64.so" ;;
      aarch64|arm64) LIB="$MODULE_DIR/build/cicd_credential_hunt-linux-arm64.so" ;;
      *)
        echo "unsupported architecture: $arch" >&2
        exit 1
        ;;
    esac
    make -C "$MODULE_DIR" linux
    ;;
  *)
    echo "unsupported OS: $os" >&2
    exit 1
    ;;
esac

make -C "$ROOT/tests/native_runner"

run_module() {
  HOME="$1" "$RUNNER" "$LIB" cicd_credential_hunt
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

extract_summary_field() {
  local haystack="$1"
  local field="$2"
  printf '%s\n' "$haystack" | sed -n "s/.*${field}=\\([0-9][0-9]*\\).*/\\1/p" | tail -1
}

EMPTY_HOME="$(mktemp -d "${TMPDIR:-/tmp}/cicd-hunt-empty.XXXXXX")"
POPULATED_HOME="$(mktemp -d "${TMPDIR:-/tmp}/cicd-hunt-populated.XXXXXX")"

cleanup() {
  rm -rf "$EMPTY_HOME" "$POPULATED_HOME"
}
trap cleanup EXIT

empty_output="$(run_module "$EMPTY_HOME")"
assert_contains "$empty_output" "[i] Mode: presence (existence only)"
assert_contains "$empty_output" "[i] Summary: fixed artifacts=0, ssh configs=0, ssh key candidates=0"
assert_not_contains "$empty_output" "[+] GitHub CLI auth:"
assert_not_contains "$empty_output" "[+] SSH private key:"
assert_not_contains "$empty_output" "[+] SSH config:"
assert_not_contains "$empty_output" "[+] npm config:"

mkdir -p "$POPULATED_HOME/.config/gh"
mkdir -p "$POPULATED_HOME/.docker"
mkdir -p "$POPULATED_HOME/.ssh"
: > "$POPULATED_HOME/.config/gh/hosts.yml"
: > "$POPULATED_HOME/.npmrc"
: > "$POPULATED_HOME/.pypirc"
: > "$POPULATED_HOME/.docker/config.json"
: > "$POPULATED_HOME/.git-credentials"
: > "$POPULATED_HOME/.gitconfig"
: > "$POPULATED_HOME/.ssh/config"
: > "$POPULATED_HOME/.ssh/id_rsa"
: > "$POPULATED_HOME/.ssh/id_ed25519"
: > "$POPULATED_HOME/.ssh/id_ecdsa"
: > "$POPULATED_HOME/.ssh/identity"
: > "$POPULATED_HOME/.ssh/deploy.pem"
: > "$POPULATED_HOME/.ssh/deploy.pem.pub"

expected_fixed=6
if [[ "$os" == "Darwin" ]]; then
  mkdir -p "$POPULATED_HOME/Library/Application Support/GitHub CLI"
  : > "$POPULATED_HOME/Library/Application Support/GitHub CLI/hosts.yml"
  expected_fixed=7
fi

populated_output="$(run_module "$POPULATED_HOME")"
assert_contains "$populated_output" "[+] GitHub CLI auth: $POPULATED_HOME/.config/gh/hosts.yml"
assert_contains "$populated_output" "[+] npm config:"
assert_contains "$populated_output" "[+] PyPI config:"
assert_contains "$populated_output" "[+] Docker config:"
assert_contains "$populated_output" "[+] Git credential store:"
assert_contains "$populated_output" "[+] Git config:"
assert_contains "$populated_output" "[+] SSH config:"
assert_contains "$populated_output" "[+] SSH private key:"
assert_contains "$populated_output" "[+] SSH PEM candidate:"
assert_contains "$populated_output" "[i]   Size: 0 bytes"
assert_not_contains "$populated_output" ".pub"

if [[ "$os" == "Darwin" ]]; then
  assert_contains "$populated_output" "[+] GitHub CLI auth: $POPULATED_HOME/Library/Application Support/GitHub CLI/hosts.yml"
fi

fixed_count="$(extract_summary_field "$populated_output" "fixed artifacts")"
ssh_config_count="$(extract_summary_field "$populated_output" "ssh configs")"
ssh_key_count="$(extract_summary_field "$populated_output" "ssh key candidates")"

if [[ -z "$fixed_count" || -z "$ssh_config_count" || -z "$ssh_key_count" ]]; then
  echo "failed to parse summary counts from populated-home output" >&2
  exit 1
fi
if [[ "$fixed_count" != "$expected_fixed" ]]; then
  echo "expected fixed artifacts=$expected_fixed, got $fixed_count" >&2
  exit 1
fi
if [[ "$ssh_config_count" != "1" ]]; then
  echo "expected ssh configs=1, got $ssh_config_count" >&2
  exit 1
fi
if [[ "$ssh_key_count" != "5" ]]; then
  echo "expected ssh key candidates=5, got $ssh_key_count" >&2
  exit 1
fi

echo "cicd_credential_hunt native_runner tests passed"
