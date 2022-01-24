#!/usr/bin/python3
import yaml
import os

with open("/etc/paraevo.yaml", "r") as f:
    config = yaml.load(f)

args = "/opt/paraevo/paraevo"

if "verbose" in config and config["verbose"] == True:
    args += " -v"

if "daemon" in config and config["daemon"] == True:
    args += " -D"

if "device" in config:
    args += " -d " + config["device"]
else:
    print("Device not present in config!")
    exit(-1)

if "mqtt" in config:
    if "server" in config["mqtt"]:
        args += " --mqtt_server=" + config["mqtt"]["server"]
    else:
        print("No MQTT server in config!")
    
    if "port" in config["mqtt"]:
        args += " --mqtt_port=" + config["mqtt"]["port"]
else:
    print("No MQTT settings in config!")
    exit(-1)

if "areas" in config:
    for area in config["areas"]:
        if "num" in area:
            args += " -a " + str(area["num"])
        else:
            print("Area config does not have \"num\"!")
            exit(-1)
        
        if "zones" in area:
            args += " -z " + ",".join([str(zone) for zone in area["zones"]])
        else:
            print("Aarea config dose not have zones!")
            exit(-1)
else:
    print("No area config!")
    exit(-1)

print("The final command:")
print(args)
os.system(args)
