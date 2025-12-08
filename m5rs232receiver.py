

from collections import Counter
import paho.mqtt.client as mqtt
from ecdsa import VerifyingKey, BadSignatureError, SECP256k1
import json
import base64
import hashlib


# callback if successfully connected
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("successfully connected to broker")
        client.subscribe("m5/#") # subscribe topic
    else:
        print(f"failed to connect to broker {reason_code}")

# Callback für empfangene Nachrichten
def on_message(client, userdata, message):
    msgData = message.payload.decode()

    # copy&paste public key from m5 serial-console 
    eccPubKeyBase64  = "PZkuDf5kRiefirGN23+rNnbg293f0liaVZRC6OC32J4SS2hJxLz6vPOMGRBHwGC8Iw6P3hkE/twCAG0nPWNLnw=="
    eccPubKey = base64.b64decode( eccPubKeyBase64.encode("ascii") )

    try:
        jsonMessage = json.loads(msgData)
        signature =  base64.b64decode( jsonMessage["s"].encode("ascii") )

        vk = VerifyingKey.from_string(eccPubKey, curve=SECP256k1)
        vk.verify(signature, jsonMessage["d"].encode("ascii"), hashfunc=hashlib.sha256)
    except BadSignatureError:
        print(f"topic: {message.topic}, invalid message or signature: {msgData}")
        return
    except:
        print(f"topic: {message.topic}, invalid message content: {msgData}")
        return

    print(f"topic: {message.topic}, message: {msgData}")



# MQTT-Client erstellen und konfigurieren
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message
client.user_data_set([])

# Verbindung zum Broker herstellen
broker = "iot.coreflux.cloud"
port = 1883
client.connect(broker, port)

# Netzwerk-Schleife starten
client.loop_forever()
