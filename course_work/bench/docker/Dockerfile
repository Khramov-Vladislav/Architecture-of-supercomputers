FROM ubuntu:24.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ make openmpi-bin libopenmpi-dev \
    && rm -rf /var/lib/apt/lists/* \
    && install -d -o nobody -g nogroup /work /launcher

COPY launch-rank /usr/local/bin/launch-rank
RUN chmod 0755 /usr/local/bin/launch-rank \
    && useradd --create-home --shell /bin/bash bench \
    && chown bench:bench /work /launcher

WORKDIR /work
USER bench
