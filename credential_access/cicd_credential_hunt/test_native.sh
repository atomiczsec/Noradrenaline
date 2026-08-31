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
assert_contains "$empty_output" "[i] Summary: credential artifacts=0, config context=0, ssh key candidates=0"
assert_not_contains "$empty_output" "[+] GitHub CLI auth:"
assert_not_contains "$empty_output" "[+] SSH private key:"
assert_not_contains "$empty_output" "[+] SSH config:"
assert_not_contains "$empty_output" "[+] npm config:"

mkdir -p "$POPULATED_HOME/.config/gh"
mkdir -p "$POPULATED_HOME/.config/glab-cli"
mkdir -p "$POPULATED_HOME/.config/gcloud"
mkdir -p "$POPULATED_HOME/.config/containers"
mkdir -p "$POPULATED_HOME/.docker"
mkdir -p "$POPULATED_HOME/.aws/cli/cache"
mkdir -p "$POPULATED_HOME/.kube"
mkdir -p "$POPULATED_HOME/.terraform.d"
mkdir -p "$POPULATED_HOME/.cargo"
mkdir -p "$POPULATED_HOME/.m2"
mkdir -p "$POPULATED_HOME/.gradle"
mkdir -p "$POPULATED_HOME/.gem"
mkdir -p "$POPULATED_HOME/.ssh"
: > "$POPULATED_HOME/.config/gh/hosts.yml"
: > "$POPULATED_HOME/.config/glab-cli/config.yml"
: > "$POPULATED_HOME/.config/gcloud/application_default_credentials.json"
: > "$POPULATED_HOME/.config/containers/auth.json"
: > "$POPULATED_HOME/.npmrc"
: > "$POPULATED_HOME/.pypirc"
: > "$POPULATED_HOME/.docker/config.json"
: > "$POPULATED_HOME/.git-credentials"
: > "$POPULATED_HOME/.gitconfig"
: > "$POPULATED_HOME/.aws/credentials"
: > "$POPULATED_HOME/.aws/config"
: > "$POPULATED_HOME/.aws/cli/cache/session.json"
: > "$POPULATED_HOME/.kube/config"
: > "$POPULATED_HOME/.terraform.d/credentials.tfrc.json"
: > "$POPULATED_HOME/.terraformrc"
: > "$POPULATED_HOME/.cargo/credentials.toml"
: > "$POPULATED_HOME/.cargo/credentials"
: > "$POPULATED_HOME/.m2/settings.xml"
: > "$POPULATED_HOME/.gradle/gradle.properties"
: > "$POPULATED_HOME/.gem/credentials"
: > "$POPULATED_HOME/.netrc"
: > "$POPULATED_HOME/.vault-token"
: > "$POPULATED_HOME/.ssh/config"
: > "$POPULATED_HOME/.ssh/id_rsa"
: > "$POPULATED_HOME/.ssh/id_ed25519"
: > "$POPULATED_HOME/.ssh/id_ecdsa"
: > "$POPULATED_HOME/.ssh/identity"
: > "$POPULATED_HOME/.ssh/deploy.pem"
: > "$POPULATED_HOME/.ssh/deploy.pem.pub"

expected_credentials=21
if [[ "$os" == "Darwin" ]]; then
  mkdir -p "$POPULATED_HOME/Library/Application Support/GitHub CLI"
  : > "$POPULATED_HOME/Library/Application Support/GitHub CLI/hosts.yml"
  expected_credentials=22
fi

populated_output="$(run_module "$POPULATED_HOME")"
assert_contains "$populated_output" "[+] GitHub CLI auth: $POPULATED_HOME/.config/gh/hosts.yml"
assert_contains "$populated_output" "[+] GitLab CLI auth:"
assert_contains "$populated_output" "[+] AWS shared credentials:"
assert_contains "$populated_output" "[+] AWS CLI cached credential:"
assert_contains "$populated_output" "[+] Google Cloud ADC:"
assert_contains "$populated_output" "[+] Kubernetes config:"
assert_contains "$populated_output" "[+] Terraform CLI credentials:"
assert_contains "$populated_output" "[+] Cargo registry credentials:"
assert_contains "$populated_output" "[+] Maven settings:"
assert_contains "$populated_output" "[+] Gradle properties:"
assert_contains "$populated_output" "[+] RubyGems credentials:"
assert_contains "$populated_output" "[+] netrc credentials:"
assert_contains "$populated_output" "[+] Container registry auth:"
assert_contains "$populated_output" "[+] Vault token:"
assert_contains "$populated_output" "[+] npm config:"
assert_contains "$populated_output" "[+] PyPI config:"
assert_contains "$populated_output" "[+] Docker config:"
assert_contains "$populated_output" "[+] Git credential store:"
assert_contains "$populated_output" "[+] Git config context:"
assert_contains "$populated_output" "[+] SSH config context:"
assert_contains "$populated_output" "[+] SSH private key:"
assert_contains "$populated_output" "[+] SSH PEM candidate:"
assert_contains "$populated_output" "[i]   Size: 0 bytes"
assert_not_contains "$populated_output" ".pub"

if [[ "$os" == "Darwin" ]]; then
  assert_contains "$populated_output" "[+] GitHub CLI auth: $POPULATED_HOME/Library/Application Support/GitHub CLI/hosts.yml"
fi

credential_count="$(extract_summary_field "$populated_output" "credential artifacts")"
context_count="$(extract_summary_field "$populated_output" "config context")"
ssh_key_count="$(extract_summary_field "$populated_output" "ssh key candidates")"

if [[ -z "$credential_count" || -z "$context_count" || -z "$ssh_key_count" ]]; then
  echo "failed to parse summary counts from populated-home output" >&2
  exit 1
fi
if [[ "$credential_count" != "$expected_credentials" ]]; then
  echo "expected credential artifacts=$expected_credentials, got $credential_count" >&2
  exit 1
fi
if [[ "$context_count" != "2" ]]; then
  echo "expected config context=2, got $context_count" >&2
  exit 1
fi
if [[ "$ssh_key_count" != "5" ]]; then
  echo "expected ssh key candidates=5, got $ssh_key_count" >&2
  exit 1
fi

trailing_slash_output="$(run_module "$POPULATED_HOME/")"
assert_contains "$trailing_slash_output" "credential artifacts=$expected_credentials"
assert_contains "$trailing_slash_output" "config context=2"

echo "cicd_credential_hunt native_runner tests passed"
