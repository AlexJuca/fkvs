FROM ubuntu:24.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update; \
    apt-get install -y --no-install-recommends \
      ca-certificates wget gnupg software-properties-common lsb-release \
      build-essential ninja-build pkg-config; \
    \
    # Add Kitware APT repo so we use a modern version of CMAKE
    wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor -o /usr/share/keyrings/kitware-archive-keyring.gpg; \
    echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" \
      > /etc/apt/sources.list.d/kitware.list; \
    apt-get update; \
    apt-get install -y cmake; \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; \
    cmake --build build -j;

FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update; \
    apt-get install -y --no-install-recommends ca-certificates netcat-openbsd; \
    rm -rf /var/lib/apt/lists/*

ARG UID=10001
ARG GID=10001
RUN groupadd -g "${GID}" fkvs && useradd -u "${UID}" -g "${GID}" -m -s /usr/sbin/nologin fkvs
RUN mkdir -p /var/run/fkvs && \
    chmod 777 /var/run/fkvs && \
    chown fkvs:fkvs /var/run/fkvs
USER fkvs

ENV PATH="/usr/local/bin:${PATH}"

COPY --from=builder /src/logo.txt /usr/local/bin/logo.txt
COPY --from=builder /src/server.conf /home/fkvs/server.conf
COPY --from=builder /src/client.conf /home/fkvs/client.conf
COPY --from=builder /src/build/fkvs-server /usr/local/bin/fkvs-server
COPY --from=builder /src/build/fkvs-benchmark /usr/local/bin/fkvs-benchmark
COPY --from=builder /src/build/fkvs-cli    /usr/local/bin/fkvs-cli

EXPOSE 5995
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD /usr/local/bin/fkvs-cli -h 127.0.0.1 -p 5995 --non-interactive | grep 'PONG' || exit 1

USER fkvs
WORKDIR /app
CMD ["fkvs-server", "-c", "/home/fkvs/server.conf"]