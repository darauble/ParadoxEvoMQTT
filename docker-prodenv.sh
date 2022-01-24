#!/bin/bash
TAG=paraevo

# Uncomment for building the image, then comment out and uncomment the running part for loading.
docker build -f docker/Dockerfile.prod -t ${TAG} .

#docker run -it --rm \
#	--device=/dev/serial/by-id/usb-PARADOX_PARADOX_APR-PRT3_a4008936-if00-port0 \
#	-v ${PWD}/etc/paraevo.yaml:/etc/paraevo.yaml \
#    --name=paraevo \
#	${TAG}

