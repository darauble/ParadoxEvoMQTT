# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Paradox EVO MQTT is a Linux daemon that interfaces with Paradox EVO48/92/192 security panels via MQTT using the Paradox PRT3 module. The daemon acts as a bridge between the security panel's serial interface and MQTT, enabling Home Assistant integration.

## Build System

### Development Environment
```bash
# Build Docker development environment (uncomment image building in script first)
./docker-buildenv.sh

# Inside Docker container, build the project
make clean
make
```

### Configuration
- Edit `Config` file to switch between RELEASE and DEBUG builds
- DEBUG build: includes debugging symbols and verbose output
- RELEASE build: optimized, stripped binary

### Dependencies
- `libpaho-mqtt-dev` - MQTT client library
- `libzmq5-dev` - ZeroMQ messaging library  
- `libpthread` - POSIX threads
- `python3-yaml` - For configuration parsing

## Architecture

### Core Components
- **main.c**: Entry point, command line parsing, daemon lifecycle
- **para_mgr.c**: Paradox panel communication manager
- **para_serial.c**: Serial communication with PRT3 module
- **mqtt_mgr.c**: MQTT client management and message handling
- **zmq_helpers.c**: ZeroMQ inter-thread communication utilities

### Key Data Structures (paratypes.h)
- Area and zone status structures
- Event parsing definitions
- Panel state management types

### Threading Model
The daemon uses multiple threads for concurrent operations:
- Serial communication thread (para_serial.c)
- MQTT message handling thread (mqtt_mgr.c)
- Main control loop with periodic status updates

## Running the Daemon

### Configuration-Based Execution
Preferred method using YAML configuration:
```bash
# Edit etc/paraevo.yaml with your settings
python3 start_daemon.py [config_file]
```

### Direct Command Line
```bash
./paraevo -v -d /dev/serial/by-id/usb-PARADOX_PARADOX_APR-PRT3_* -a 1 -z 1,2,3,4 --mqtt_server=192.168.0.100
```

### Production Deployment
```bash
# Build production Docker image
docker build -f docker/Dockerfile.prod -t paraevo-prod .

# Run with mounted config
docker run -v /etc/paraevo.yaml:/etc/paraevo.yaml paraevo-prod
```

## Configuration

### Required Settings
- `device`: Serial device path (prefer `/dev/serial/by-id/` over `/dev/ttyUSB*`)
- `mqtt.server`: MQTT broker address
- `areas`: At least one area with associated zones

### PRT3 Module Setup
- Configure PRT3 to 57600 baud, ASCII mode
- Must be configured via keypad (BabyWare/WinLoad configuration doesn't work)

## MQTT Interface

### Topic Structure
- Base topic: `darauble/paraevo` (configurable via `-t`)
- Area status: `{base}/area/{num}` 
- Zone status: `{base}/area/{area}/zone/{zone}`
- Utility keys: `{base}/utilitykey`
- Daemon status: `{base}/daemon` (LWT)

### Home Assistant Integration
The daemon implements the standard MQTT Alarm Panel interface with states:
- `armed_away`, `armed_home`, `disarmed`, `triggered`

## Development Notes

### Build Commands
```bash
make clean          # Clean build artifacts
make                # Build with current Config settings
make BUILD="RELEASE" # Force release build
make BUILD="DEBUG"   # Force debug build
```

### End-to-End Test Harness (`test/`)
A self-contained Docker image builds the daemon and tests it against a fake PRT3 and a local mosquitto broker, including broker outages.

To run it:
```bash
docker build -f test/Dockerfile -t paraevo-test . && docker run --rm paraevo-test
```

- `test/Dockerfile`: builds the daemon and installs mosquitto. Debian Trixie is the default; add `--build-arg BASE=debian:bullseye` to test on Bullseye (EOL, so it is installed from archive.debian.org).
- `test/fake_prt3.py`: fake PRT3 on a pty (`/tmp/prt3`). Answers RA/AL/RZ/ZL/AA/AQ/AD/UK requests, logs received commands to `/tmp/prt3_cmds.log`, and accepts control lines on the FIFO `/tmp/prt3_ctl` (`ZONE <n> O|C`, `RAW <text>`).
- `test/run_test.py`: acts like Home Assistant. Runs three scenarios (normal startup, broker killed and restarted, daemon started before the broker) and checks "online", the republished area/zone state, zone events, ARM_AWAY/ARM_HOME/DISARM, utility keys and a clean shutdown. Exits non-zero if any check fails. `OUTAGE_SECONDS` and `RECONNECT_TIMEOUT` env vars tune the timing.

### MQTT Reconnection
Subscriptions and state publishing happen in `onConnected` (registered with `MQTTAsync_setConnected`), because Paho calls the connect options' `onSuccess` only for the first explicit connect, not for automatic reconnects. The last known area/zone state is cached in `mqtt_mgr.c` and republished on every connect. If a connect attempt fails (e.g. the broker is not up at boot), the MQTT thread retries every 10 s.

### Serial Device Management
- Use persistent device paths: `/dev/serial/by-id/usb-PARADOX_PARADOX_APR-PRT3_*`
- Ensure user is in `dialout` group for serial access
- PRT3 requires specific baud rate and ASCII mode configuration

### Version Management
Version is defined in main.c (`V_MAJOR`, `V_MINOR`) and should be updated for releases.

### Docker Development Workflow
1. Uncomment image build line in `docker-buildenv.sh`
2. Update user ID/group ID in `docker/Dockerfile.build`
3. Update PRT3 device path in script
4. Run `./docker-buildenv.sh` to enter development environment