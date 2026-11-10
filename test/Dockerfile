# Self-contained test image: builds the daemon and runs an end-to-end
# MQTT reconnection test against a fake PRT3 serial adapter.
#
#   docker build -f test/Dockerfile -t paraevo-test .
#   docker run --rm paraevo-test
#
# Use --build-arg BASE=debian:bullseye to test against the older environment.
ARG BASE=debian:trixie
FROM ${BASE}

# EOL releases (e.g. bullseye) are only available from archive.debian.org.
RUN . /etc/os-release; if [ "$VERSION_CODENAME" = "bullseye" ]; then \
      echo "deb http://archive.debian.org/debian bullseye main" > /etc/apt/sources.list; \
      echo "deb http://archive.debian.org/debian-security bullseye-security main" >> /etc/apt/sources.list; \
      rm -f /etc/apt/sources.list.d/*; \
    fi

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential make gcc \
      libpaho-mqtt-dev libzmq3-dev \
      mosquitto mosquitto-clients \
      python3 python3-paho-mqtt \
      procps \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY Makefile Config ./
COPY src ./src
RUN make clean && make

COPY test/fake_prt3.py test/run_test.py /test/

CMD ["python3", "-u", "/test/run_test.py"]
