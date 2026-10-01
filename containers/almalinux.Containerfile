FROM docker.io/library/almalinux:10-minimal
RUN microdnf -y install epel-release \
 && microdnf -y install --setopt=install_weak_deps=0 gcc-c++ clang cmake doctest-devel \
 && microdnf clean all
