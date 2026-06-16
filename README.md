# Noradrenaline Shared Library Modules
<img src="assets/Noradrenaline-logo.png" width="100%">

Native Linux and macOS modules built as small shared libraries for agent workflows. On Poseidon, macOS builds run through `execute_library` today; Linux builds are not yet available on public agents, though other agents may already support them. Linux support for public agents is actively in development.

<div align='center'>

## Table of Contents

[Collection](#collection)  
[Community](#community)  
[Credential Access](#credential-access)  
[Discovery](#discovery)  
[Execution](#execution)  
[Build](#build)  
[Local Testing](#local-testing)  
[Module ABI](docs/module_abi.md)

</div>

## Collection

| **Module** | **Use** |
|------------|---------|
| **[ai_surface](collection/ai_surface/)** | Maps AI tooling, AI agent profiles, and MCP configuration files on macOS and Linux developer endpoints with bounded config previews. |

## Community

| **Module** | **Use** |
|------------|---------|
| _Coming soon_ | Community-inspired modules ported into the Noradrenaline shared library contract. |

## Credential Access

| **Module** | **Use** |
|------------|---------|
| **[cloud_metadata_check](credential_access/cloud_metadata_check/)** | Probes the local cloud Instance Metadata Service for AWS, Azure, and GCP, reporting provider, context, IMDS mode, and bounded identity snippets unless run in `presence` mode. |

## Discovery

| **Module** | **Use** |
|------------|---------|
| **[app_count](discovery/app_count/)** | Counts unique installed application indicators from standard macOS `.app` bundle locations or Linux `.desktop` launcher locations. |
| **[mdm_policy_artifacts](discovery/mdm_policy_artifacts/)** | Scores locally observable macOS MDM and Linux endpoint-management artifacts to assess enrollment or managed posture. |
| **[native_env](discovery/native_env/)** | Lists environment variables or returns a specific environment variable from the agent process. Useful for validating the module ABI and inspecting execution context. |
| **[netjoin_query](discovery/netjoin_query/)** | Reports hostname, OS version, and local macOS/Linux directory, domain, workgroup, or realm evidence. |
| **[os_version](discovery/os_version/)** | Collects macOS and Linux product, version, kernel, architecture, and install-age evidence as the Unix equivalent of `win_version`. |
| **[proxy_enum](discovery/proxy_enum/)** | Enumerates proxy configuration evidence from environment variables, system files, package-manager settings, and browser policy/profile files. |
| **[wallpaper_enum](discovery/wallpaper_enum/)** | Enumerates macOS display wallpaper paths and Linux desktop wallpaper configuration evidence that can reveal internal shares or managed assets. |

## Execution

| **Module** | **Use** |
|------------|---------|
| **[firewall_rule](execution/firewall_rule/)** | Query-only macOS/Linux firewall posture module for readable PF, Application Firewall, nftables, iptables, ufw, and firewalld configuration evidence. |

## Build

Each module directory owns its source, `Makefile`, and module README. The root `Makefile` builds across the module catalog. Build outputs are local-only and are not committed to git.

Build all modules for the current host platform:

```bash
make build
```

Build for a specific platform:

```bash
make build PLATFORM=mac
make build PLATFORM=linux
make build PLATFORM=both
```

Shorthand targets:

```bash
make build-mac
make build-linux
make build-both
```

The `macos` target requires macOS toolchains and `lipo`. The `linux` target uses local Linux cross-compilers when available. The `linux-docker` target builds `.so` artifacts with Debian containers for `linux/amd64` and `linux/arm64`.

## Local Testing

Use [tests/native_runner](tests/native_runner/) to load a built `.dylib` or `.so`, resolve an export, and call it with Poseidon-like arguments without Mythic.

```bash
make -C tests/native_runner
tests/native_runner/build/native_runner \
  discovery/native_env/build/native_env-macos-arm64.dylib \
  get_env HOME
```

The runner passes a simulated agent path as `argv[0]`; user arguments begin at `argv[1]`. See [tests/native_runner/README.md](tests/native_runner/README.md) for more examples.

<h3 align="center">Connect with me:</h3>
<p align="center">
  <a href="https://github.com/atomiczsec" target="_blank"><img src="https://raw.githubusercontent.com/rahuldkjain/github-profile-readme-generator/master/src/images/icons/Social/github.svg" height="30" width="40" /></a>
  <a href="https://instagram.com/atomiczsec" target="_blank"><img src="https://raw.githubusercontent.com/rahuldkjain/github-profile-readme-generator/master/src/images/icons/Social/instagram.svg" height="30" width="40" /></a>
  <a href="https://x.com/atomiczsec" target="_blank"><img src="https://raw.githubusercontent.com/rahuldkjain/github-profile-readme-generator/master/src/images/icons/Social/twitter.svg" height="30" width="40" /></a>
  <a href="https://medium.com/@atomiczsec" target="_blank"><img src="https://raw.githubusercontent.com/rahuldkjain/github-profile-readme-generator/master/src/images/icons/Social/medium.svg" height="30" width="40" /></a>
  <a href="https://youtube.com/@atomiczsec" target="_blank"><img src="https://raw.githubusercontent.com/rahuldkjain/github-profile-readme-generator/master/src/images/icons/Social/youtube.svg" height="30" width="40" /></a>
</p>

DISCLAIMER: The creators and contributors of this repository accept no liability for any loss, damage, or consequences resulting from the use of the information or code contained in this repo. By utilizing this repo, you acknowledge and accept full responsibility for your actions. Use at your own risk.
