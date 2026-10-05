# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge

FROM golang:1.24-alpine AS builder

WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download

COPY . .
RUN CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o /innoedge-server .

FROM alpine:3.20

RUN apk --no-cache add ca-certificates tzdata
WORKDIR /app
COPY --from=builder /innoedge-server /app/innoedge-server

EXPOSE 8080
ENTRYPOINT ["/app/innoedge-server"]
CMD ["-addr", ":8080"]
