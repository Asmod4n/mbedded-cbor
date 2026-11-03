FROM docker.io/library/almalinux:10-minimal
RUN microdnf -y install epel-release \
 && microdnf -y install --enablerepo=crb --setopt=install_weak_deps=0 gcc-toolset-15-gcc-c++ cmake ninja-build doctest-devel boost-devel ccache mold bash \
 && microdnf clean all
ENV PATH=/opt/rh/gcc-toolset-15/root/usr/bin:$PATH
