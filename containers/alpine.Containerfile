FROM docker.io/library/alpine:3.24
RUN apk add --no-cache g++ cmake samurai doctest-dev boost-dev ccache mold bash
