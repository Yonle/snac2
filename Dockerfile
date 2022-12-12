FROM alpine
ENV LANG C.UTF-8
ENV LC_ALL C.UTF-8
RUN apk add --no-cache curl-dev build-base
COPY . /build
WORKDIR /build
RUN make
COPY examples/docker-entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh
ENTRYPOINT ["/bin/sh", "/usr/local/bin/entrypoint.sh"]
EXPOSE 8001
CMD /build/snac
