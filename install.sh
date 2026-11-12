#!/usr/bin/env bash
# Installs the latest pique release:
#
#   curl -fsSL https://raw.githubusercontent.com/jebin2/pique/main/install.sh | bash
#
# Downloads the release package from GitHub, checks it against the SHA-256
# GitHub publishes for it, and installs it with pacman (which asks for your
# password). Nothing else is downloaded or run.

set -euo pipefail

REPO="jebin2/pique"
dir=""

say() { printf '\033[1m%s\033[0m\n' "$*"; }
fail() { printf 'pique install: %s\n' "$*" >&2; exit 1; }

# The latest release's x86_64 package: prints "name url sha256".
latest_package() {
  local json
  json="$(curl -fsSL --max-time 30 "https://api.github.com/repos/$REPO/releases/latest")" ||
    fail "could not reach GitHub to find the latest release"
  if command -v jq >/dev/null 2>&1; then
    jq -r '[.assets[] | select(.name | test("^pique-[0-9][^/]*-x86_64\\.pkg\\.tar\\.zst$"))][0]
           | select(. != null) | "\(.name) \(.browser_download_url) \(.digest // "")"' <<<"$json"
  else
    python3 -c '
import json, re, sys
for a in json.load(sys.stdin).get("assets", []):
    if re.fullmatch(r"pique-[0-9][^/]*-x86_64\.pkg\.tar\.zst", a["name"]):
        print(a["name"], a["browser_download_url"], a.get("digest") or "")
        break' <<<"$json"
  fi
}

main() {
  command -v pacman >/dev/null 2>&1 || fail "pique is packaged for Arch Linux (pacman not found)"
  [ "$(uname -m)" = x86_64 ] ||
    fail "the release package is for x86_64; build from source: https://github.com/$REPO#install"
  command -v jq >/dev/null 2>&1 || command -v python3 >/dev/null 2>&1 ||
    fail "needs jq or python3 to read the release (sudo pacman -S jq)"

  say "Finding the latest pique release…"
  local name url digest
  read -r name url digest < <(latest_package) || true
  [ -n "${name:-}" ] || fail "no x86_64 package in the latest release"
  case "$digest" in
    sha256:*) digest="${digest#sha256:}" ;;
    *) fail "GitHub gave no checksum for $name; not installing an unchecked file" ;;
  esac

  dir="$(mktemp -d)"
  trap 'rm -rf -- "$dir"' EXIT

  say "Downloading $name…"
  curl -fL --max-time 300 --progress-bar -o "$dir/$name" "$url" || fail "download failed"
  echo "$digest  $dir/$name" | sha256sum --check --quiet - ||
    fail "checksum mismatch: the download is not the published file; not installing"

  # A downloaded file counts as local: pacman only demands a signature for
  # packages it fetches from a URL itself.
  say "Installing (pacman asks for your password)…"
  # Under "curl | bash" this script is on stdin: pacman's questions go to
  # the terminal instead.
  { : </dev/tty; } 2>/dev/null || fail "needs a terminal for pacman's questions"
  sudo pacman -U "$dir/$name" </dev/tty

  say "pique is installed. Restart your browser once, then press Super+Shift+L over a link."
}

# Everything runs from main, so a download of this script that stops halfway
# runs nothing.
main "$@"
