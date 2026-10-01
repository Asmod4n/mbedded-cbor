FROM registry.opensuse.org/opensuse/tumbleweed:latest
RUN zypper -n refresh \
 && zypper -n install --no-recommends gcc-c++ clang cmake doctest-devel simdutf-devel libc++-devel \
 && zypper clean -a
