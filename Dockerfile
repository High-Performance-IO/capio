FROM debian:bookworm AS builder

ARG CAPIO_BUILD_TESTS=OFF
ARG CAPIO_LOG=OFF
ARG CMAKE_BUILD_TYPE=Release

RUN apt update                              \
 && apt install -y --no-install-recommends  \
        build-essential                     \
        ca-certificates                     \
        cmake                               \
        git                                 \
        libcapstone-dev                     \
        libopenmpi-dev                      \
        ninja-build                         \
        openmpi-bin                         \
        pkg-config

COPY CMakeLists.txt /opt/capio/
COPY scripts /opt/capio/scripts
COPY src /opt/capio/src
COPY tests /opt/capio/tests

RUN mkdir -p /opt/capio/build                     \
 && cmake                                         \
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}    \
        -DCAPIO_BUILD_TESTS=${CAPIO_BUILD_TESTS}  \
        -DCAPIO_LOG=${CAPIO_LOG}                  \
        -G Ninja                                  \
        -B /opt/capio/build                       \
        -S /opt/capio                             \
 && cmake --build /opt/capio/build -j$(nproc)     \
 && cmake --install /opt/capio/build --prefix /usr/local


FROM debian:bookworm

ENV LD_LIBRARY_PATH="/usr/local/lib"

RUN apt update                              \
 && apt install -y --no-install-recommends  \
        libcapstone4                        \
        openmpi-bin                         \
 && rm -rf /var/lib/apt/lists/*

# Include files
COPY --from=builder                                         \
    /usr/local/include/libsyscall_intercept_hook_point.h    \
    /usr/local/include/simdjson.h                           \
    /usr/local/include/

# Libraries
COPY --from=builder                                         \
    /usr/local/lib/libcapio_posix.so                        \
    /usr/local/lib/libcapio_posix.so.0                      \
    /usr/local/lib/libcapio_posix.so.0.0.1                  \
    /usr/local/lib/libsimdjson.a                            \
    /usr/local/lib/libsyscall_intercept.a                   \
    /usr/local/lib/libsyscall_intercept.so                  \
    /usr/local/lib/libsyscall_intercept.so.0                \
    /usr/local/lib/libsyscall_intercept.so.0.1.0            \
    /usr/local/lib/

# Binaries
COPY --from=builder                                         \
    /usr/local/bin/capio_server                             \
    /usr/local/bin/

# Pkgconfig
COPY --from=builder                                         \
    /usr/local/lib/pkgconfig/args.pc                        \
    /usr/local/lib/pkgconfig/libsyscall_intercept.pc        \
    /usr/local/lib/pkgconfig/simdjson.pc                    \
    /usr/local/lib/pkgconfig/

# CMake files
COPY --from=builder                                         \
    /usr/local/lib/cmake/simdjson/                          \
    /usr/local/lib/cmake/
