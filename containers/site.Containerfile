FROM docker.io/library/debian:sid
RUN apt-get update \
 && apt-get install -y --no-install-recommends clang-23 libclang-23-dev g++-16 lld-23 libc++-23-dev-wasm32 \
    libc++abi-23-dev-wasm32 libclang-rt-23-dev-wasm32 wasi-libc wabt nodejs binaryen brotli llvm-23 emscripten \
    pandoc lynx libboost-dev doctest-dev \
 && rm -rf /var/lib/apt/lists/*
