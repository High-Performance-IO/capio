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

RUN apt update                                                \
 && apt install -y --no-install-recommends                    \
        libcapstone4                                          \
        openmpi-bin                                           \
        openssh-server                                        \
 && rm -rf /var/lib/apt/lists/*                               \
 && mkdir -p /run/sshd                                        \
 && adduser                                                   \
        --disabled-password                                   \
        --gecos ""                                            \
        capio                                                 \
 && mkdir -p ~capio/.ssh                                      \
 && ssh-keygen -q                                             \
        -t ed25519                                            \
        -C "capio@hpio"                                       \
            -N ""                                             \
        -f"/home/capio/.ssh/id_ed25519"                       \
 && cp ~capio/.ssh/id_ed25519.pub ~capio/.ssh/authorized_keys \
 && echo "StrictHostKeyChecking no" > ~capio/.ssh/config      \
 && chown -R capio:capio ~capio/.ssh                          \
 && chmod 700 ~capio/.ssh                                     \
 && chmod 600                                                 \
    ~capio/.ssh/authorized_keys                               \
    ~capio/.ssh/config                                        \
    ~capio/.ssh/id_ed25519.pub

# Include files
COPY --from=builder                                         \
    "/usr/local/include/gmoc[k]"                            \
    "/usr/local/include/gtes[t]"                            \
    "/usr/local/include/libsyscall_intercept_hook_point.h"  \
    "/usr/local/include/simdjson.h"                         \
    /usr/local/include/

# Libraries
COPY --from=builder                                         \
    "/usr/local/lib/libcapio_posix.so"                      \
    "/usr/local/lib/libcapio_posix.so.1"                    \
    "/usr/local/lib/libcapio_posix.so.1.0.0"                \
    "/usr/local/lib/libgmock.[a]"                           \
    "/usr/local/lib/libgmock_main.[a]"                      \
    "/usr/local/lib/libgtest.[a]"                           \
    "/usr/local/lib/libgtest_main.[a]"                      \
    "/usr/local/lib/libsimdjson.a"                          \
    "/usr/local/lib/libsyscall_intercept.a"                 \
    "/usr/local/lib/libsyscall_intercept.so"                \
    "/usr/local/lib/libsyscall_intercept.so.0"              \
    "/usr/local/lib/libsyscall_intercept.so.0.1.0"          \
    /usr/local/lib/

# Binaries
COPY --from=builder                                         \
    "/usr/local/bin/capio_posix_unit_test[s]"               \
    "/usr/local/bin/capio_server"                           \
    "/usr/local/bin/capio_server_unit_test[s]"              \
    "/usr/local/bin/capio_syscall_unit_test[s]"             \
    "/usr/local/bin/capio_integration_test[s]"              \
    /usr/local/bin/

# Pkgconfig
COPY --from=builder                                         \
    "/usr/local/lib/pkgconfig/args.pc"                      \
    "/usr/local/lib/pkgconfig/gmock.p[c]"                   \
    "/usr/local/lib/pkgconfig/gmock_main.p[c]"              \
    "/usr/local/lib/pkgconfig/gtest.p[c]"                   \
    "/usr/local/lib/pkgconfig/gtest_main.p[c]"              \
    "/usr/local/lib/pkgconfig/libsyscall_intercept.pc"      \
    "/usr/local/lib/pkgconfig/simdjson.pc"                  \
    /usr/local/lib/pkgconfig/

# CMake files
COPY --from=builder                                         \
    "/usr/local/lib/cmake/GTes[t]"                          \
    "/usr/local/lib/cmake/simdjson"                         \
    /usr/local/lib/cmake/

# Start SSH server
EXPOSE 22
CMD ["/usr/sbin/sshd", "-D"]
