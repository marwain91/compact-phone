# Ubuntu 24.04 (Noble) ships a modern libsrtp2 (2.5+) that's compatible
# with OpenSSL 3.x's EVP_MAC API. PJSIP 2.17's bundled libsrtp crashes
# in its init self-test (srtp_auth_type_test -> hmac_dup ->
# EVP_MAC_CTX_new) on both 22.04 (libcrypto 3.0.2) and 24.04 (3.0.13) —
# the libsrtp+OpenSSL3 incompatibility, not the OpenSSL version. The
# fix is to link PJSIP against system libsrtp2 via --with-external-srtp,
# bypassing the broken bundled copy. Ubuntu 24.04's libsrtp2-dev is
# what we want, hence the base bump.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# Build toolchain + Qt 6 / PJSIP system dependencies for vcpkg
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ninja-build \
        git \
        curl \
        wget \
        zip \
        unzip \
        tar \
        pkg-config \
        autoconf \
        autoconf-archive \
        automake \
        libtool \
        python3 \
        python3-pip \
        python3-jinja2 \
        nasm \
        yasm \
        bison \
        flex \
        gperf \
        ca-certificates \
        sudo \
        bash-completion \
        libfontconfig1-dev \
        libfreetype-dev \
        libx11-dev \
        libx11-xcb-dev \
        libxext-dev \
        libxfixes-dev \
        libxi-dev \
        libxrender-dev \
        libxcb1-dev \
        libxcb-glx0-dev \
        libxcb-keysyms1-dev \
        libxcb-image0-dev \
        libxcb-shm0-dev \
        libxcb-icccm4-dev \
        libxcb-sync-dev \
        libxcb-xfixes0-dev \
        libxcb-shape0-dev \
        libxcb-randr0-dev \
        libxcb-render-util0-dev \
        libxcb-util-dev \
        libxcb-xinerama0-dev \
        libxcb-xkb-dev \
        libxcb-cursor-dev \
        libxkbcommon-dev \
        libxkbcommon-x11-dev \
        libegl1-mesa-dev \
        libgl1-mesa-dev \
        libgles2-mesa-dev \
        libglu1-mesa-dev \
        libdbus-1-dev \
        libssl-dev \
        libsrtp2-dev \
        libasound2-dev \
        libpulse-dev \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 22.04 ships CMake 3.22; CMakePresets v6 requires >= 3.25.
ARG CMAKE_VERSION=3.28.3
RUN ARCH=$(uname -m) \
    && case "$ARCH" in \
         x86_64)  CMAKE_ARCH=x86_64 ;; \
         aarch64) CMAKE_ARCH=aarch64 ;; \
         *) echo "Unsupported arch: $ARCH" >&2; exit 1 ;; \
       esac \
    && wget -qO- "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz" \
       | tar -xz --strip-components=1 -C /usr/local

RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

ENV VCPKG_ROOT=/opt/vcpkg
ENV PATH=/opt/vcpkg:$PATH

# Build PJSIP from upstream and install to /opt/pjproject. We do not source
# PJSIP via vcpkg because pjproject is not in the vcpkg registry, its
# experimental CMake build has install-layout bugs (libs in bin/, broken
# IMPORTED_LOCATION), and its mature autotools build does not support
# vcpkg's out-of-source build pattern. Building in-source here once is
# simple, deterministic, and matches the upstream-recommended path.
ARG PJSIP_VERSION=2.17
RUN cd /tmp \
    && curl -sL "https://github.com/pjsip/pjproject/archive/refs/tags/${PJSIP_VERSION}.tar.gz" \
       | tar -xz \
    && cd "pjproject-${PJSIP_VERSION}" \
    && printf '#pragma once\n#define PJSUA_MAX_CALLS 32\n' > pjlib/include/pj/config_site.h \
    && ./aconfigure \
        --prefix=/opt/pjproject \
        --disable-shared \
        --enable-static \
        --with-ssl=yes \
        --with-external-srtp=1 \
        --disable-video \
        --disable-libwebrtc \
        --disable-libyuv \
        --disable-opencore-amr \
        --disable-bcg729 \
        --disable-sdl \
        --disable-ffmpeg \
        --disable-v4l2 \
        --disable-openh264 \
    && make dep \
    && make -j"$(nproc)" \
    && make install \
    && cd / \
    && rm -rf "/tmp/pjproject-${PJSIP_VERSION}"

ENV PKG_CONFIG_PATH=/opt/pjproject/lib/pkgconfig:${PKG_CONFIG_PATH}

WORKDIR /workspace

CMD ["sleep", "infinity"]
