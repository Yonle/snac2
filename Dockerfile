ARG ALPINE_VERSION=latest

FROM alpine:${ALPINE_VERSION} AS builder
COPY . /build
RUN apk -U --no-progress --no-cache add curl-dev build-base && \
  cd /build && make && \
  make PREFIX="/build/out/usr/local" PREFIX_MAN="/build/out/usr/local/share/man" install && \
  chmod +x examples/docker-entrypoint.sh && \
  cp examples/docker-entrypoint.sh /build/out/usr/local/bin/entrypoint.sh

FROM alpine:${ALPINE_VERSION}
RUN apk -U --no-progress --no-cache add libcurl
COPY --from=builder /build/out /
EXPOSE 5050
VOLUME [ "/data" ]
ENTRYPOINT [ "/usr/local/bin/entrypoint.sh" ]
