FROM docker.io/library/debian:sid
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++-16 cmake ninja-build doctest-dev libboost-dev ccache mold bash \
 && rm -rf /var/lib/apt/lists/*
