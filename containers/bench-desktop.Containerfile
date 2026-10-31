FROM localhost/mbedded-cbor-tw:latest
RUN zypper -n install --no-recommends benchmark-devel \
 && zypper clean -a
