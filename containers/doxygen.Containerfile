FROM docker.io/library/debian:sid
RUN apt-get update \
 && apt-get install -y --no-install-recommends doxygen xsltproc libclang-common-21-dev g++ \
 && rm -rf /var/lib/apt/lists/*
