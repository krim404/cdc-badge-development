# Reproducible build environment for CDC Badge plugins (the container
# alternative to the host-native setup). Version pins match scripts/setup.py.
FROM rust:1-bookworm

ENV BINARYEN_VERSION=version_119

# WebAssembly target + the components the SDK toolchain expects.
RUN rustup target add wasm32-unknown-unknown \
 && rustup component add rustfmt clippy

# Python (for the badge CLI) and git (for submodules).
RUN apt-get update \
 && apt-get install -y --no-install-recommends python3 python3-pip python3-venv git curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Pinned Binaryen wasm-opt (x86_64 linux build).
RUN curl -fsSL "https://github.com/WebAssembly/binaryen/releases/download/${BINARYEN_VERSION}/binaryen-${BINARYEN_VERSION}-x86_64-linux.tar.gz" \
    | tar xz -C /opt \
 && ln -s "/opt/binaryen-${BINARYEN_VERSION}/bin/wasm-opt" /usr/local/bin/wasm-opt

# Host tooling for upload/serial.
RUN pip3 install --no-cache-dir --break-system-packages "pyserial>=3.5"

WORKDIR /workspaces/cdc-badge-development
