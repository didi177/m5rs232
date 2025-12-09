

from collections import Counter
import paho.mqtt.client as mqtt
from ecdsa import VerifyingKey, BadSignatureError, SECP256k1
import json
import base64
import hashlib

lastMessageTimeStamp_g = ""

# callback if successfully connected
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("successfully connected to broker")
        client.subscribe("m5/#") # subscribe topic, add mac-address to filter just one device
    else:
        print(f"failed to connect to broker {reason_code}")

# Callback für empfangene Nachrichten
def on_message(client, userdata, message):
    try:
        # ecdsa public key is from config.json
        eccPubKey = userdata["eccPubKey"]

        msgData = message.payload.decode()
        jsonMessage = json.loads( msgData )
        signature =  base64.b64decode( jsonMessage["s"].encode("ascii") )

        vk = VerifyingKey.from_string(eccPubKey, curve=SECP256k1)
        vk.verify(signature, jsonMessage["d"].encode("ascii"), hashfunc=hashlib.sha256)
    except BadSignatureError:
        print(f"topic: {message.topic}, invalid message or signature: {msgData}")
        return
    except:
        print(f"topic: {message.topic}, invalid message content: {msgData}")
        return

    timeStamp = jsonMessage["d"][1:16]
    global lastMessageTimeStamp_g
    if "" != lastMessageTimeStamp_g:
        if timeStamp <= lastMessageTimeStamp_g:
            print(f"topic: {message.topic}, invalid timestamp: {timeStamp}")
            return
        
    lastMessageTimeStamp_g = timeStamp
    print(f"topic: {message.topic}, message: {msgData}")

    # append line to file
    filename = message.topic.replace(":", "").replace("/", ".")
    with open(filename, "a") as f:
        f.write(jsonMessage["d"] + "\n")


# create a file config.json with content:
# {"eccPubKeyBase64": "[copy&paste public key base64 coded from m5 serial-console]"}

with open("config.json", "r") as f:
    config = json.load(f)
eccPubKeyBase64 = config["eccPubKeyBase64"].encode("ascii")
eccPubKey = base64.b64decode( eccPubKeyBase64 )

# MQTT-Client erstellen und konfigurieren
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message
client.user_data_set({"eccPubKey": eccPubKey})

# Verbindung zum Broker herstellen
broker = "iot.coreflux.cloud"
port = 1883
client.connect(broker, port)

# Netzwerk-Schleife starten
client.loop_forever()
