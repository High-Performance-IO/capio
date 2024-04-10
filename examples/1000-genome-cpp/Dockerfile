FROM debian:bookworm AS builder

RUN apt update && \
    apt install -y  \
        libboost-all-dev \
        libcurl4-openssl-dev \
        libarchive-dev \
        cmake \
        gcc \
        g++
RUN mkdir -p /home/capio
COPY CMakeLists.txt /home/capio
COPY src /home/capio

RUN mkdir /home/capio/build &&\
    cd /home/capio/build &&\
    cmake .. &&\
    make -j


FROM hpio/capio

RUN apt update && \
    apt install -y  \
        libboost-all-dev \
        libcurl4-openssl-dev \
        libarchive-dev

COPY --from=builder \
            "/home/capio/build/download" \
            "/home/capio/build/frequency" \
            "/home/capio/build/individuals" \
            "/home/capio/build/individuals_merge" \
            "/home/capio/build/mutation_overlap" \
            "/home/capio/build/sifting" \
             /home/capio/

ENV HOME=/home/capio
