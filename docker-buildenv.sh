#!/bin/bash
TAG=build-paraevo

mkdir -p ${PWD}/home

# Uncomment before first use! Then comment it out to load faster.
#docker build -f docker/Dockerfile.build -t ${TAG} .

docker run -it --rm \
	--device=/dev/serial/by-id/usb-PARADOX_PARADOX_APR-PRT3_a4008936-if00-port0 \
	-v ${PWD}:/PARA_EVO_MQTT \
	-v ${PWD}/home:/home/darauble \
	-v ${PWD}/etc/paraevo.yaml:/etc/paraevo.yaml \
	-w /PARA_EVO_MQTT \
	--name=paraevo-build \
	${TAG} /bin/bash
