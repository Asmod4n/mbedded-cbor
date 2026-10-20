FROM registry.opensuse.org/opensuse/tumbleweed:latest
RUN zypper -n refresh \
 && zypper -n install --no-recommends gcc15-c++ cmake doctest-devel ccache ninja mold libboost_headers-devel \
 && zypper clean -a
