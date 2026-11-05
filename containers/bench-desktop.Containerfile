FROM localhost/mbedded-cbor-tw:latest
RUN zypper -n install --no-recommends benchmark-devel flatbuffers-devel capnproto libcapnp-devel nlohmann_json-devel \
 && zypper clean -a
