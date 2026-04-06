#!/usr/bin/env bash

# macOS ARM64 helper for building this xv6-style repo with cross tools.
# Usage:
#   source ./setup-macos-arm64.sh
# Then use:
#   xv6check && xv6build

# Detect whether this script is sourced.
__xv6_is_sourced=0
if [ -n "${ZSH_VERSION:-}" ]; then
  case "$ZSH_EVAL_CONTEXT" in
    *:file) __xv6_is_sourced=1 ;;
  esac
elif [ -n "${BASH_VERSION:-}" ]; then
  if [ "${BASH_SOURCE[0]}" != "$0" ]; then
    __xv6_is_sourced=1
  fi
else
  (return 0 2>/dev/null) && __xv6_is_sourced=1
fi

if [ "$__xv6_is_sourced" -ne 1 ]; then
  echo "This script must be sourced, not executed."
  echo "Run: source ./setup-macos-arm64.sh"
  exit 1
fi

# Resolve script path in bash or zsh.
if [ -n "${BASH_SOURCE:-}" ] && [ -n "${BASH_SOURCE[0]:-}" ]; then
  __xv6_script_path="${BASH_SOURCE[0]}"
elif [ -n "${ZSH_VERSION:-}" ]; then
  __xv6_script_path="${(%):-%N}"
else
  __xv6_script_path="./setup-macos-arm64.sh"
fi

export XV6_ROOT
XV6_ROOT="$(cd "$(dirname "$__xv6_script_path")" && pwd)"

# Prefer a dedicated cross toolchain in /opt/cross when present.
XV6_CROSS_ROOT="${XV6_CROSS_ROOT:-/opt/cross}"
XV6_CROSS_BIN="$XV6_CROSS_ROOT/bin"
if [ -d "$XV6_CROSS_BIN" ]; then
  PATH="$XV6_CROSS_BIN:$PATH"
  export PATH
fi

# Clear inherited values so this script defines a clean environment.
unset TOOLPREFIX
unset QEMU

# Add likely Homebrew tool locations to PATH for this shell.
__xv6_brew_prefix=""
if command -v brew >/dev/null 2>&1; then
  __xv6_brew_prefix="$(brew --prefix 2>/dev/null)"
fi

if [ -n "$__xv6_brew_prefix" ]; then
  for __xv6_dir in \
    "$__xv6_brew_prefix/bin" \
    "$__xv6_brew_prefix/sbin" \
    "$__xv6_brew_prefix/opt"/*/bin \
    "$__xv6_brew_prefix/Cellar"/*/*/bin; do
    if [ -d "$__xv6_dir" ]; then
      PATH="$__xv6_dir:$PATH"
    fi
  done
  export PATH
fi

# Pick a cross toolchain prefix for 32-bit x86 builds.
if [ -x "$XV6_CROSS_BIN/i386-jos-elf-gcc" ]; then
  export TOOLPREFIX="$XV6_CROSS_BIN/i386-jos-elf-"
elif [ -x "$XV6_CROSS_BIN/i386-elf-gcc" ]; then
  export TOOLPREFIX="$XV6_CROSS_BIN/i386-elf-"
elif [ -x "$XV6_CROSS_BIN/i686-elf-gcc" ]; then
  export TOOLPREFIX="$XV6_CROSS_BIN/i686-elf-"
elif command -v i386-jos-elf-gcc >/dev/null 2>&1; then
  export TOOLPREFIX="i386-jos-elf-"
elif command -v i386-elf-gcc >/dev/null 2>&1; then
  export TOOLPREFIX="i386-elf-"
elif command -v i686-elf-gcc >/dev/null 2>&1; then
  export TOOLPREFIX="i686-elf-"
else
  echo "Warning: no known cross-gcc prefix found in PATH/Homebrew scan."
  echo "Set TOOLPREFIX manually before building."
fi

# Pick QEMU binary (prefer 32-bit system target for this kernel).
if command -v qemu-system-i386 >/dev/null 2>&1; then
  export QEMU="qemu-system-i386"
elif command -v qemu >/dev/null 2>&1; then
  export QEMU="qemu"
else
  echo "Warning: no QEMU executable found in PATH."
fi

xv6make() {
  (cd "$XV6_ROOT" && TOOLPREFIX="${TOOLPREFIX:-}" QEMU="${QEMU:-}" make "$@")
}

xv6check() {
  if [ -z "${TOOLPREFIX:-}" ]; then
    echo "TOOLPREFIX is not set."
    return 1
  fi

  if ! command -v "${TOOLPREFIX}objdump" >/dev/null 2>&1; then
    echo "Missing ${TOOLPREFIX}objdump in PATH"
    return 1
  fi

  if ! "${TOOLPREFIX}objdump" -i 2>/dev/null | grep -q "elf32-i386"; then
    echo "${TOOLPREFIX}objdump does not report elf32-i386 support"
    return 1
  fi

  if ! command -v "${TOOLPREFIX}gcc" >/dev/null 2>&1; then
    echo "Missing ${TOOLPREFIX}gcc in PATH"
    return 1
  fi

  if ! "${TOOLPREFIX}gcc" -m32 -x c -c /dev/null -o /tmp/xv6check.o >/dev/null 2>&1; then
    echo "${TOOLPREFIX}gcc -m32 compile test failed"
    return 1
  fi

  rm -f /tmp/xv6check.o

  if [ -z "${QEMU:-}" ] || ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "QEMU not found (QEMU=${QEMU:-unset})"
    return 1
  fi

  echo "xv6 toolchain check passed"
}

alias xv6build='xv6make -j"$(sysctl -n hw.ncpu)"'
alias xv6run='xv6make qemu'
alias xv6nox='xv6make qemu-nox'
alias xv6gdb='xv6make qemu-gdb'
alias xv6clean='xv6make clean'

echo "Configured xv6 environment:"
echo "  XV6_ROOT=$XV6_ROOT"
echo "  XV6_CROSS_ROOT=$XV6_CROSS_ROOT"
echo "  TOOLPREFIX=${TOOLPREFIX:-unset}"
echo "  QEMU=${QEMU:-unset}"
echo "Commands: xv6check, xv6build, xv6run, xv6nox, xv6gdb, xv6clean"

unset __xv6_is_sourced
unset __xv6_script_path
unset __xv6_brew_prefix
unset __xv6_dir
unset XV6_CROSS_BIN
