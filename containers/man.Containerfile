FROM docker.io/library/debian:sid
RUN apt-get update \
 && apt-get install -y --no-install-recommends pandoc mandoc doxygen python3 \
 && rm -rf /var/lib/apt/lists/*
