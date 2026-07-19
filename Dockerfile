# syntax=docker/dockerfile:1

# ---------------------------------------------------------------------------
# Build stage: compile BlazeKV with io_uring + mimalloc on Ubuntu 24.04.
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates \
        liburing-dev pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY include ./include
COPY src ./src
COPY tests ./tests
COPY bench ./bench

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBLAZEKV_BUILD_TESTS=OFF \
        -DBLAZEKV_USE_MIMALLOC=ON \
        -DBLAZEKV_USE_URING=ON \
        -DBLAZEKV_NATIVE_ARCH=OFF \
    && cmake --build build --target blazekv-server -j"$(nproc)"

# ---------------------------------------------------------------------------
# Runtime stage: minimal image with just the server and its shared libs.
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        liburing2 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --home /data blazekv \
    && mkdir -p /data && chown blazekv /data

COPY --from=build /src/build/blazekv-server /usr/local/bin/blazekv-server

USER blazekv
WORKDIR /data
VOLUME ["/data"]

# 6380: RESP protocol   9121: Prometheus /metrics + dashboard API
EXPOSE 6380 9121

ENTRYPOINT ["/usr/local/bin/blazekv-server"]
CMD ["--bind", "0.0.0.0", "--port", "6380", "--dir", "/data", \
     "--appendonly", "yes", "--appendfsync", "everysec", "--metrics-port", "9121"]
