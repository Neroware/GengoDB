#!/usr/bin/env bash

set -e

IMAGE_NAME=pi:gengodb-benchmark-0.0.1

DOCKER_BUILDKIT=1 docker build --ssh default -t ${IMAGE_NAME} .