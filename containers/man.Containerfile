FROM docker.io/library/debian:sid
RUN apt-get update \
 && apt-get install -y --no-install-recommends pandoc mandoc \
 && rm -rf /var/lib/apt/lists/*
