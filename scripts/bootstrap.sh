#!/usr/bin/env bash
# Sets up the workspace in the parent of this repo: Python, west, Zephyr, modules
# and the Arm toolchain. No sudo needed. Safe to re-run.
set -euo pipefail

UV_VERSION=0.13.1
PYTHON_VERSION=3.12
SDK_VERSION=1.0.1

REPO=$(cd "$(dirname "$0")/.." && pwd)
WS=$(dirname "$REPO")
TOOLS=$WS/.tools
VENV=$WS/.venv

export UV_PYTHON_INSTALL_DIR=$TOOLS/python
export UV_CACHE_DIR=$TOOLS/uv-cache

if [ ! -x "$TOOLS/uv/uv" ]; then
    archive=uv-x86_64-unknown-linux-gnu.tar.gz
    url=https://github.com/astral-sh/uv/releases/download/$UV_VERSION/$archive
    tmp=$(mktemp -d)
    wget -q -P "$tmp" "$url" "$url.sha256"
    (cd "$tmp" && sha256sum -c "$archive.sha256")
    mkdir -p "$TOOLS/uv"
    tar -xzf "$tmp/$archive" -C "$TOOLS/uv" --strip-components=1
    rm -rf "$tmp"
fi

if [ ! -x "$VENV/bin/python" ]; then
    "$TOOLS/uv/uv" venv --seed --python "$PYTHON_VERSION" "$VENV"
fi
# cmake and ninja from PyPI, so the build needs no system packages for them.
"$TOOLS/uv/uv" pip install --python "$VENV/bin/python" west cmake ninja

export PATH=$VENV/bin:$PATH
cd "$WS"
if [ ! -d .west ]; then
    west init -l "$REPO"
fi
west update --narrow --fetch-opt=--depth=1
"$TOOLS/uv/uv" pip install --python "$VENV/bin/python" -r zephyr/scripts/requirements.txt

if [ ! -d "$TOOLS/zephyr-sdk-$SDK_VERSION" ]; then
    # The installer looks for existing SDKs in a registry under $HOME and skips the
    # install if it finds one, so give it a private $HOME inside the workspace.
    HOME=$TOOLS west sdk install --version "$SDK_VERSION" --install-base "$TOOLS" --toolchains arm-zephyr-eabi
fi

# The device pack pyOCD needs to flash the MCX A153 (make flash RUNNER=pyocd).
pyocd pack install mcxa153 || echo "pyocd pack install failed; RUNNER=pyocd won't work" >&2

echo "Bootstrap complete. Next: make build"
