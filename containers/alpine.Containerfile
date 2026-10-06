FROM docker.io/library/alpine:latest
RUN apk add --no-cache g++ clang cmake make doctest-dev
