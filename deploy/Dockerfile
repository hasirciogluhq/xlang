# xlang — production image (xlang build + welcome landing server)
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    clang \
    curl \
    git \
    unzip \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://xmake.io/shget.text | bash
ENV PATH="/root/.local/bin:${PATH}"

WORKDIR /src
COPY . .

RUN mkdir -p /out \
    && xmake f -y -m release \
    && xmake -y \
    && ./build/xlang build deploy/server.xlang -o /out/server

FROM debian:bookworm-slim AS runner

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libssl3 \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /out/server ./server
COPY examples/welcome.html ./examples/welcome.html

ENV XLANG_ENV=production
ENV PORT=8080

EXPOSE 8080

CMD ["./server"]
