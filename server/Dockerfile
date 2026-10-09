# openbv-server: build from the repository's root (the content fetch is a repository script):
#
#   docker build -f server/Dockerfile -t openbv-server .
#   docker run -p 8080:8080 -p 10207:10207 -p 3333:3333 -e OPENBV_ADMIN_TOKEN=change-me \
#     -v openbv-data:/data openbv-server
#
# The image carries the vanilla 2.11 maps (main/maps from the GPL source release, tools/fetch-content.sh);
# mount your own content over /content to serve others (OPENBV_CONTENT). Sessions, bans and the audit log
# live in /data. Each session listens on its own port: publish the ones you create.

FROM golang:1.27-alpine AS build
WORKDIR /src
COPY server/go.mod server/go.sum ./
RUN go mod download
COPY server/ ./
ARG VERSION=dev
RUN CGO_ENABLED=0 go build -trimpath -ldflags "-s -w -X main.version=${VERSION}" -o /openbv-server ./cmd/openbv-server

FROM alpine:3.22 AS content
RUN apk add --no-cache bash curl tar coreutils findutils
WORKDIR /repo
COPY tools/fetch-content.sh tools/
RUN tools/fetch-content.sh /content-full >/dev/null && mkdir -p /content/main && cp -r /content-full/main/maps /content/main/maps \
 && mkdir -p /data

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /openbv-server /openbv-server
COPY --from=content /content /content
# distroless's nonroot user (65532) owns the data folder
COPY --from=content --chown=65532:65532 /data /data
COPY LICENSE /LICENSE
ENV OPENBV_CONTENT=/content OPENBV_DATA=/data OPENBV_LISTEN=:8080
VOLUME /data
EXPOSE 8080 10207 3333
USER nonroot:nonroot
ENTRYPOINT ["/openbv-server"]
