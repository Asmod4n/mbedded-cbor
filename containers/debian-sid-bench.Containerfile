FROM docker.io/library/debian:sid
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++-16 clang-23 libbenchmark-dev libtinycbor-dev libjsoncons-dev \
    libcbor-dev libflatbuffers-dev flatbuffers-compiler libmsgpack-cxx-dev python3 procps \
 && rm -rf /var/lib/apt/lists/*
