#!/usr/bin/env bash

cd ../..
docker build -t alphaunito/capio --build-arg CAPIO_LOG=ON --build-arg CMAKE_BUILD_TYPE=Debug .
cd scripts/docker-development
( sleep 3; docker ps )&
docker compose up