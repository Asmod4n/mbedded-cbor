FROM docker.io/library/debian:trixie
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ clang libclang-rt-19-dev cmake ninja-build doctest-dev libboost-dev pkg-config ccache mold ca-certificates \
 && rm -rf /var/lib/apt/lists/*
