#!/usr/bin/python3
import yaml
import os
import sys

binary_path = "/opt/paraevo/paraevo"
config_file = "/etc/paraevo.yaml"

if len(sys.argv) > 1:
    config_file = sys.argv[1]

with open(config_file, "r") as f:
    config = yaml.load(f, Loader=yaml.FullLoader)

if "binary_path" in config:
    binary_path = config["binary_path"]

args = binary_path

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
        args += " --mqtt_port=" + str(config["mqtt"]["port"])
    
    if "login" in config["mqtt"]:
        args += " --mqtt_login=" + config["mqtt"]["login"]
    
    if "password" in config["mqtt"]:
        args += " --mqtt_password=" + config["mqtt"]["password"]
    
    if "retain" in config["mqtt"] and config["mqtt"]["retain"] == True:
        args += " -r"

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

if "user_code" in config and config["user_code"] != "":
    args += " -u " + config["user_code"]

if "status_period" in config:
    args += " -S " + str(config["status_period"])

if "log_file" in config:
    args += " >> " + config["log_file"] + " 2>&1"

print("The final command:")
print(args)
os.system(args)
