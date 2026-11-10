#!/usr/bin/env python3
"""
End-to-end test of paraevo MQTT behaviour across broker outages.

Processes started here:
  mosquitto   - the MQTT broker (stopped/started to simulate an outage)
  fake_prt3   - fake PRT3 serial adapter on a pty
  paraevo     - the daemon under test
  observer    - a paho client acting like Home Assistant

Scenarios:
  1. normal startup
  2. broker goes down and comes back while the daemon runs
  3. daemon starts while the broker is down, broker comes up later

Each scenario checks: daemon online, full area/zone state published,
zone events, ARM_AWAY / ARM_HOME / DISARM commands and utility keys.
"""
import os
import signal
import subprocess
import sys
import threading
import time

import paho.mqtt.client as mqtt

BASE = "darauble/paraevo"
DAEMON = os.environ.get("PARAEVO_BIN", "/src/paraevo")
USER_CODE = "1234"
ZONES = [1, 2, 3]
OUTAGE_SECONDS = int(os.environ.get("OUTAGE_SECONDS", "15"))
RECONNECT_TIMEOUT = int(os.environ.get("RECONNECT_TIMEOUT", "90"))

CMD_LOG = "/tmp/prt3_cmds.log"
CTL_FIFO = "/tmp/prt3_ctl"

results = []


def say(msg):
    print(f"\n[TEST {time.strftime('%H:%M:%S')}] {msg}", flush=True)


# --------------------------------------------------------------------------
# Observer ("Home Assistant")
# --------------------------------------------------------------------------
class Observer:
    def __init__(self):
        self.lock = threading.Lock()
        self.msgs = []  # (time, topic, payload)
        if hasattr(mqtt, "CallbackAPIVersion"):
            self.c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1, client_id="observer")
        else:
            self.c = mqtt.Client(client_id="observer")
        self.c.on_connect = self.on_connect
        self.c.on_message = self.on_message
        self.c.reconnect_delay_set(1, 2)

    def on_connect(self, c, u, flags, rc):
        c.subscribe(BASE + "/#")

    def on_message(self, c, u, m):
        with self.lock:
            self.msgs.append((time.time(), m.topic, m.payload.decode(errors="replace")))

    def start(self):
        while True:
            try:
                self.c.connect("127.0.0.1", 1883, 10)
                break
            except OSError:
                time.sleep(0.5)
        self.c.loop_start()

    def wait_connected(self, timeout=30):
        end = time.time() + timeout
        while time.time() < end:
            if self.c.is_connected():
                return True
            time.sleep(0.2)
        return False

    def wait_for(self, topic, payload=None, since=0.0, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                for t, tp, pl in self.msgs:
                    if t >= since and tp == topic and (payload is None or pl == payload):
                        return pl
            time.sleep(0.1)
        return None

    def publish(self, topic, payload):
        self.c.publish(topic, payload, qos=1)


# --------------------------------------------------------------------------
# Process helpers
# --------------------------------------------------------------------------
class Procs:
    broker = None
    fake = None
    daemon = None


def start_broker():
    Procs.broker = subprocess.Popen(
        ["mosquitto", "-p", "1883"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    time.sleep(0.5)
    say(f"broker UP (pid {Procs.broker.pid})")


def stop_broker():
    Procs.broker.send_signal(signal.SIGKILL)
    Procs.broker.wait()
    say("broker DOWN")


def start_fake():
    Procs.fake = subprocess.Popen(["python3", "-u", "/test/fake_prt3.py"])
    for _ in range(50):
        if os.path.exists("/tmp/prt3") and os.path.exists(CTL_FIFO):
            break
        time.sleep(0.1)


def start_daemon():
    Procs.daemon = subprocess.Popen(
        [DAEMON, "-v", "-d", "/tmp/prt3", "-a", "1", "-z", ",".join(map(str, ZONES)),
         "-u", USER_CODE, "--mqtt_server=127.0.0.1"],
    )
    say(f"daemon started (pid {Procs.daemon.pid})")


def stop_daemon():
    if Procs.daemon and Procs.daemon.poll() is None:
        Procs.daemon.send_signal(signal.SIGTERM)
        try:
            Procs.daemon.wait(10)
        except subprocess.TimeoutExpired:
            Procs.daemon.kill()
            Procs.daemon.wait()


def panel(line):
    with open(CTL_FIFO, "w") as f:
        f.write(line + "\n")


def wait_cmd(expected, since_line, timeout=5.0):
    end = time.time() + timeout
    while time.time() < end:
        with open(CMD_LOG) as f:
            lines = f.read().splitlines()[since_line:]
        if expected in lines:
            return True
        time.sleep(0.1)
    return False


def cmd_count():
    with open(CMD_LOG) as f:
        return len(f.read().splitlines())


# --------------------------------------------------------------------------
# Checks
# --------------------------------------------------------------------------
def check(scenario, name, ok):
    results.append((scenario, name, bool(ok)))
    print(f"[TEST]   {'PASS' if ok else 'FAIL'}  {scenario}: {name}", flush=True)


def interact(obs, scenario, since, online_timeout):
    say(f"=== interacting: {scenario} ===")
    alive = Procs.daemon.poll() is None
    check(scenario, "daemon process alive", alive)
    if not alive:
        return

    check(scenario, "daemon reports online",
          obs.wait_for(f"{BASE}/daemon", "online", since, online_timeout))
    check(scenario, "area 1 state published after connect",
          obs.wait_for(f"{BASE}/area/1", None, since, 5))
    for z in ZONES:
        check(scenario, f"zone {z} state published after connect",
              obs.wait_for(f"{BASE}/area/1/zone/{z}", None, since, 5))

    # Zone open/close event from the panel
    t = time.time()
    panel("ZONE 2 O")
    check(scenario, "zone 2 open -> 'on'", obs.wait_for(f"{BASE}/area/1/zone/2", "on", t))
    t = time.time()
    panel("ZONE 2 C")
    check(scenario, "zone 2 closed -> 'off'", obs.wait_for(f"{BASE}/area/1/zone/2", "off", t))

    # Arm away
    n, t = cmd_count(), time.time()
    obs.publish(f"{BASE}/area/1/set", "ARM_AWAY")
    check(scenario, "ARM_AWAY reaches panel", wait_cmd(f"AA001A{USER_CODE}", n))
    check(scenario, "area 1 -> armed_away", obs.wait_for(f"{BASE}/area/1", "armed_away", t))

    # Disarm
    n, t = cmd_count(), time.time()
    obs.publish(f"{BASE}/area/1/set", "DISARM")
    check(scenario, "DISARM reaches panel", wait_cmd(f"AD001{USER_CODE}", n))
    check(scenario, "area 1 -> disarmed", obs.wait_for(f"{BASE}/area/1", "disarmed", t))

    # Arm home (stay)
    n, t = cmd_count(), time.time()
    obs.publish(f"{BASE}/area/1/set", "ARM_HOME")
    check(scenario, "ARM_HOME reaches panel", wait_cmd("AQ001S", n))
    check(scenario, "area 1 -> armed_home", obs.wait_for(f"{BASE}/area/1", "armed_home", t))

    n, t = cmd_count(), time.time()
    obs.publish(f"{BASE}/area/1/set", "DISARM")
    wait_cmd(f"AD001{USER_CODE}", n)
    obs.wait_for(f"{BASE}/area/1", "disarmed", t)

    # Utility key
    n = cmd_count()
    obs.publish(f"{BASE}/utilitykey", "7")
    check(scenario, "utility key 7 reaches panel", wait_cmd("UK007", n))


# --------------------------------------------------------------------------
def main():
    start_fake()

    # Scenario 1: normal startup
    start_broker()
    obs = Observer()
    obs.start()
    obs.wait_connected()
    t0 = time.time()
    start_daemon()
    interact(obs, "1-startup", t0, 15)

    # Scenario 2: broker outage while running
    stop_broker()
    say(f"waiting {OUTAGE_SECONDS}s with broker down...")
    time.sleep(OUTAGE_SECONDS)
    # Something happens at the panel during the outage
    panel("ZONE 3 O")
    time.sleep(1)
    start_broker()
    obs.wait_connected()
    t1 = time.time() - 1
    interact(obs, "2-broker-restart", t1, RECONNECT_TIMEOUT)
    check("2-broker-restart", "zone 3 change during outage is visible",
          obs.wait_for(f"{BASE}/area/1/zone/3", "on", t1, 1))
    panel("ZONE 3 C")

    # Scenario 3: daemon starts while broker is down
    stop_daemon()
    stop_broker()
    start_daemon()
    say(f"daemon running without broker; waiting {OUTAGE_SECONDS}s...")
    time.sleep(OUTAGE_SECONDS)
    start_broker()
    obs.wait_connected()
    t2 = time.time() - 1
    interact(obs, "3-broker-late", t2, RECONNECT_TIMEOUT)

    # Clean shutdown
    say("stopping daemon")
    stop_daemon()
    check("4-shutdown", "daemon exits cleanly on SIGTERM",
          Procs.daemon.returncode == 0)

    obs.c.loop_stop()
    Procs.broker.kill()
    Procs.fake.kill()

    say("SUMMARY")
    failed = [r for r in results if not r[2]]
    for s, n, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {s}: {n}")
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed", flush=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
