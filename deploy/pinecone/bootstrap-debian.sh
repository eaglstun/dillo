#!/bin/sh
# One-time (and safe to re-run) package setup for building Dillo on Pinecone.
set -eu

if [ "$(id -u)" -ne 0 ]; then
   exec sudo -- "$0" "$@"
fi

if [ ! -r /etc/os-release ]; then
   echo "Cannot identify this operating system." >&2
   exit 1
fi

. /etc/os-release
if [ "${ID:-}" != "debian" ]; then
   echo "This bootstrap supports Debian only (found ${ID:-unknown})." >&2
   exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
   build-essential \
   autoconf \
   automake \
   ca-certificates \
   git \
   libbrotli-dev \
   libfltk1.3-dev \
   libjpeg-dev \
   libpng-dev \
   libssl-dev \
   libwebp-dev \
   rsync \
   xauth \
   xvfb \
   zlib1g-dev

echo "Pinecone is ready to build and run Dillo under Xvfb."
