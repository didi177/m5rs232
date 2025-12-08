// hardware:
// M5 Atom lite                            https://shop.m5stack.com/products/atom-lite-esp32-development-kit
// with ATOMIC RS232 Base W/O Atom lite    https://shop.m5stack.com/products/atomic-rs232-base-w-o-atom-lite
//
// description:
// atom receives data from rs232 and publish it at mqtt-server via wifi
// data have ntp-timestamp and are digital signed with ecdsa 
//


#include <M5Atom.h>             // core
#include <Network.h>            // network
#include <WiFi.h>               // wifi
#include <PubSubClient.h>       // mqtt
#include <sntp.h>               // ntp
#include <Preferences.h>        // flash        https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html
#include <ArduinoJson.h>        // json         https://arduinojson.org
#include <uECC.h>               // ecdsa        https://github.com/kmackay/micro-ecc
#include <SHA256.h>             // crypto       https://rweather.github.io/arduinolibs/crypto.html
#include <arduino_base64.hpp>   // base64       https://github.com/dojyorin/arduino_base64
#include <bootloader_random.h>  // trng



class CStopWatch
{
public:
    CStopWatch(){reset();}
    void reset(){startTimeMs_ = millis();}
    double getMillis(){return millis() - startTimeMs_;}
    double getSeconds(){return getMillis()/1000;}
private:
    double startTimeMs_;
};


class CConfig : public JsonDocument
{
public:
    CConfig() : p_() {}
    bool flashWrite()
    {
        if (false == p_.begin("config", false)) return false;
        String jsonStr;
        serializeJson(*this, jsonStr);
        Serial.println("config-write: " + jsonStr);
        p_.putString("json", jsonStr);
        p_.end();
        return true;
    }
    bool flashRead()
    {
        if (false == p_.begin("config", true)) return false;
        if (true == p_.isKey("json"))
        {
            String jsonStr = p_.getString("json");
            Serial.println("config-read: " + jsonStr);
            DeserializationError error = deserializeJson(*this, jsonStr);
            if (error)
            {
                Serial.println("deserializeJson() failed: " + String(error.f_str()));
            }
        }        
        p_.end();
        return true;
    }
private:
    Preferences p_;
};



// defines, globals, statics

#define NTP_TIMEZONE  "UTC-1"
#define NTP_SERVER1   "0.de.pool.ntp.org"
#define NTP_SERVER2   "1.de.pool.ntp.org"
#define NTP_SERVER3   "2.de.pool.ntp.org"

#define MQTT_SERVER   "iot.coreflux.cloud"
#define MQTT_PORT     1883


CConfig config;

HardwareSerial& Bridge = Serial2;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

CStopWatch stopWatchKeepAlive;
CStopWatch stopWatchLed;



static String toBase64(const size_t len, const uint8_t* data)
{
    char base64[base64::encodeLength(len)];
    base64::encode(data, len, base64);
    return String(base64);
}

static size_t fromBase64Len(const String& inBase64)
{
    return base64::decodeLength(inBase64.c_str());
}

static void fromBase64Data(const String& inBase64, uint8_t* outBuffer)
{
    base64::decode(inBase64.c_str(), outBuffer);
}


static String toHex(const size_t len, const uint8_t* data)
{
    String res;
    for (size_t i=0; i<len; i++)
    {
        res += "0123456789ABCDEF"[data[i] / 16];
        res += "0123456789ABCDEF"[data[i] % 16];
    }

    return res;
}


static int ueccRng(uint8_t *dest, unsigned size) 
{
    bootloader_fill_random(dest, size);
    //Serial.println("ueccRng: " + toHex(size, dest));
    return 1;
}


typedef struct SHA256_CTX 
{
    SHA256 hashObj;
    size_t hashLen;
    uint8_t hash[SHA256::HASH_SIZE];
} SHA256_CTX;

typedef struct SHA256_HashContext 
{
    uECC_HashContext uECC;
    SHA256_CTX ctx;
} SHA256_HashContext;

static void init_SHA256(const uECC_HashContext *base) 
{
    SHA256_HashContext *context = (SHA256_HashContext *)base;
    context->ctx.hashLen = SHA256::HASH_SIZE;
    memset(context->ctx.hash, 0x00, sizeof(context->ctx.hash));
    context->ctx.hashObj.reset();
}

static void update_SHA256(const uECC_HashContext *base, const uint8_t *message, unsigned message_size) 
{
    SHA256_HashContext *context = (SHA256_HashContext *)base;
    context->ctx.hashObj.update(message, message_size);
}

static void finish_SHA256(const uECC_HashContext *base, uint8_t *hash_result) 
{
    SHA256_HashContext *context = (SHA256_HashContext *)base;
    context->ctx.hashObj.finalize(hash_result, context->ctx.hashLen);
    //Serial.println("[finish_SHA256] using hash: " + toHex(context->ctx.hashLen, (unsigned char*)&hash_result));
}




String getDateTime(const bool withTimeZone=false)
{
    char buffer[30];
    memset(&buffer, 0x00, sizeof(buffer));
    auto t = time(nullptr);
    auto tm = localtime(&t);  // for local timezone

    if (true == withTimeZone)
        sprintf(buffer, "%04d%02d%02dT%02d%02d%02d %s", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, NTP_TIMEZONE);
    else
        sprintf(buffer, "%04d%02d%02dT%02d%02d%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

    return String(buffer);
}


bool wifiConnect()
{
    bool hasCfgSsids = false;
    String cfgWifiSsid, cfgWifiPwd;
    bool confNetworkScan = true;
    if (true == confNetworkScan)
    {
        Serial.println("Scan start");

        // WiFi.scanNetworks will return the number of networks found.
        int n = WiFi.scanNetworks();
        Serial.println("Scan done");

        if (n == 0) 
        {
            Serial.println("no networks found");
        } 
        else 
        {
            Serial.print(n);
            Serial.println(" networks found");
            Serial.println("Nr | SSID                             | RSSI | CH | Encryption");
            for (int i = 0; i < n; ++i) {
                // Print SSID and RSSI for each network found
                Serial.printf("%2d", i + 1);
                Serial.print(" | ");
                Serial.printf("%-32.32s", WiFi.SSID(i).c_str());
                Serial.print(" | ");
                Serial.printf("%4ld", WiFi.RSSI(i));
                Serial.print(" | ");
                Serial.printf("%2ld", WiFi.channel(i));
                Serial.print(" | ");
                switch (WiFi.encryptionType(i)) {
                  case WIFI_AUTH_OPEN:            Serial.print("open"); break;
                  case WIFI_AUTH_WEP:             Serial.print("WEP"); break;
                  case WIFI_AUTH_WPA_PSK:         Serial.print("WPA"); break;
                  case WIFI_AUTH_WPA2_PSK:        Serial.print("WPA2"); break;
                  case WIFI_AUTH_WPA_WPA2_PSK:    Serial.print("WPA+WPA2"); break;
                  case WIFI_AUTH_WPA2_ENTERPRISE: Serial.print("WPA2-EAP"); break;
                  case WIFI_AUTH_WPA3_PSK:        Serial.print("WPA3"); break;
                  case WIFI_AUTH_WPA2_WPA3_PSK:   Serial.print("WPA2+WPA3"); break;
                  case WIFI_AUTH_WAPI_PSK:        Serial.print("WAPI"); break;
                  default:                        Serial.print("unknown");
                }
                Serial.println();

                for (size_t i=1; i<=9; i++)
                {
                    const String cfgWifiSsidKey = "wifi-" + String(i) + "-ssid";
                    const String cfgWifiPwdKey = "wifi-" + String(i) + "-pwd";
                    if (config[cfgWifiSsidKey].isNull()) continue;

                    if (config[cfgWifiSsidKey] == WiFi.SSID(i))
                    {
                        hasCfgSsids = true;
                        cfgWifiSsid = String(config[cfgWifiSsidKey]);
                        cfgWifiPwd = String(config[cfgWifiPwdKey]);
                        Serial.println("found ssid in configuration: " + cfgWifiSsid);
                        continue;
                    }
                }

                delay(10);
            }
        }

        // Delete the scan result to free memory for code below.
        WiFi.scanDelete();

        Serial.println("");
    }


    WiFi.setHostname((const char*)config["hostname"]);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);


    // check if SSID is in config
    if (false == hasCfgSsids)
    {
        for (size_t i=1; i<=9; i++)
        {
            const String cfgWifiSsidKey = "wifi-" + String(i) + "-ssid";
            const String cfgWifiPwdKey = "wifi-" + String(i) + "-pwd";
            if (config[cfgWifiSsidKey].isNull())
            {
                Serial.setTimeout(1000 * 100);
                Serial.print("enter wlan ssid: ");
                String wlanSsid = Serial.readStringUntil('\n');
                wlanSsid.trim();
                Serial.println("");

                if (true == wlanSsid.isEmpty()) 
                {
                    Serial.println("invalid SSID");
                    break;
                }

                Serial.print("enter wlan pwd: ");
                String wlanPwd = Serial.readStringUntil('\n');
                wlanPwd.trim();
                Serial.println("");

                if (true == wlanPwd.isEmpty()) 
                {
                    Serial.println("invalid password");
                    break;
                }

                Serial.println("write ssid: " + wlanSsid + ", password: " + wlanPwd + " to flash");
                config[cfgWifiSsidKey] = wlanSsid;
                config[cfgWifiPwdKey] = wlanPwd;
                config.flashWrite();

                cfgWifiSsid = wlanSsid;
                cfgWifiPwd = wlanPwd;
                break;
            }
        }
    }

    Serial.println("WiFi MAC address: " + WiFi.macAddress());
    Serial.print("Connecting to ");
    Serial.println(cfgWifiSsid);

    WiFi.begin(cfgWifiSsid.c_str(), cfgWifiPwd.c_str());
    for (;;)
    {
        int wifiState = WiFi.status();
        Serial.print(wifiState);
        Serial.print(",");
        if (WL_CONNECTED == wifiState) break;

        delay(500);
    }

    //        WL_NO_SHIELD = 255,
    //        WL_IDLE_STATUS = 0,
    //        WL_NO_SSID_AVAIL = 1,
    //        WL_SCAN_COMPLETED = 2,
    //        WL_CONNECTED = 3,
    //        WL_CONNECT_FAILED = 4,
    //        WL_CONNECTION_LOST = 5,
    //        WL_DISCONNECTED = 6

    // good-case: 6,0,0,3,
    // bad-case:  6,6,6,6,6,1,1,1,1,1,1,1,1,1,1,1,1,1

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("hostname: ");
    Serial.println(WiFi.getHostname());
    Serial.print("ip-address: ");
    Serial.println(WiFi.localIP());
    Serial.print("network-name: ");
    Serial.println(WiFi.SSID());
    Serial.print("signal-strength: ");
    Serial.println(WiFi.RSSI());

    WiFi.printDiag(Serial);

    Serial.println("getting ntp-time");

    configTzTime(NTP_TIMEZONE, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED)
    {
      Serial.print(".");
      delay(1000);
    }
    Serial.println("");

    return true;
}



bool mqttServerPublish(const char* serialData="")
{
    const String mqttId = WiFi.macAddress(); 
    if (false == client.connected())
    {
        if (false == client.connect(mqttId.c_str()))
        {
            Serial.println("could not connect to mqtt");
            return false;
        }
    }

    Serial.print(getDateTime() + " mqtt connected, clientId: "); Serial.println(mqttId);

    String d = "[" + getDateTime() + "]" + serialData;

    SHA256 hashObj;
    size_t hashLen = SHA256::HASH_SIZE;
    uint8_t hash[hashLen] = {0};
    hashObj.update(d.c_str(), strlen(d.c_str()));
    hashObj.finalize(&hash, hashLen);
    //Serial.println("using hash: " + toHex(sizeof(hash), (unsigned char*)&hash));

    // uECC_secp256k1
    static const String eccKeyPrivateBase64 = (const char*)config["ecdsa-prvk"];

    const struct uECC_Curve_t* curve = uECC_secp256k1();
    uint8_t eccKeyPrivate[uECC_curve_private_key_size(curve)] = {0};
    uint8_t signature[2 * uECC_curve_private_key_size(curve)] = {0};

    fromBase64Data(eccKeyPrivateBase64, eccKeyPrivate);
    //Serial.println("using private-key: " + toHex(uECC_curve_private_key_size(curve), eccKeyPrivate));

    uint8_t tmp[2 * SHA256::HASH_SIZE + SHA256::BLOCK_SIZE];
    SHA256_HashContext ctx = {{
        &init_SHA256,
        &update_SHA256,
        &finish_SHA256,
        SHA256::BLOCK_SIZE,
        SHA256::HASH_SIZE,
        tmp
    }};

    if (!uECC_sign_deterministic(eccKeyPrivate, hash, sizeof(hash), &ctx.uECC, signature, curve))
    //if (!uECC_sign(eccKeyPrivate, hash, hashLen, signature, curve)) 
    {
        Serial.println("uECC_sign() failed");
    }

    //Serial.println("using signature: " + toHex(sizeof(signature), (unsigned char*)&signature));
    //Serial.println("using signature: " + toBase64(sizeof(signature), (unsigned char*)&signature));

    JsonDocument json;
    json["d"] = d;      // rs232 data
    json["s"] = toBase64(sizeof(signature), signature);                                     // signature

    String jsonStr;
    serializeJson(json, jsonStr);
    const String topic = "m5/" + mqttId + "/rs232";
    client.publish(topic.c_str(), jsonStr.c_str());

    return true;
}


void LED(const uint32_t color)
{
    uint32_t grbColor = color;
    M5.dis.drawpix(0, grbColor);
}


void setup() 
{
    M5.begin(true, false, true);
    delay(50);

    LED(CRGB::HTMLColorCode::Blue);

    // setup serials
    Serial.begin(115200);
    Bridge.begin(9600, SERIAL_8N2, 22, 19);

    delay(1500);
    Serial.println(""); Serial.println("setup started");

    // clear serials
    while (Serial.available() > 0) Serial.read();
    while (Bridge.available() > 0) Bridge.read();

    // init trng
    bootloader_random_enable();
    uECC_set_rng(&ueccRng);   

    // read config
    config.flashRead();

    if (config["hostname"].isNull())
    {
        Serial.setTimeout(1000 * 100);
        Serial.print("enter hostname: ");
        String hostname = Serial.readStringUntil('\n');
        hostname.trim();
        Serial.println("");

        if (false == hostname.isEmpty())
        {
            Serial.println("write hostname to flash");
            config["hostname"] = hostname;
            config.flashWrite();
        }
        else
        {
            Serial.println("hostname invalid");
        }

    }
    Serial.print("using hostname: "); Serial.println((const char*)config["hostname"]);

    // check if ecdsa key generation was already done
    if (config["ecdsa-pubk"].isNull())
    {
        CStopWatch stopWatchKeyGeneration;
        Serial.println("starting generating ecdsa keys");

        const struct uECC_Curve_t* curve = uECC_secp256k1();
        uint8_t eccKeyPrivate[32] = {0};
        uint8_t eccKeyPublic[64] = {0};
        uint8_t hash[32] = {0};
        uint8_t sig[64] = {0};
    
        if (!uECC_make_key(eccKeyPublic, eccKeyPrivate, curve)) 
        {
            Serial.println("uECC_make_key() failed");
        }

        memcpy(hash, eccKeyPublic, sizeof(hash));

        Serial.print("generated keys in "); Serial.println(stopWatchKeyGeneration.getMillis());

        if (!uECC_sign(eccKeyPrivate, hash, sizeof(hash), sig, curve)) 
        {
            Serial.println("uECC_sign() failed");
        }

        if (!uECC_verify(eccKeyPublic, hash, sizeof(hash), sig, curve)) 
        {
            Serial.println("uECC_verify() failed");
        }

        Serial.println("write keys to flash");
        config["ecdsa-prvk"] = toBase64(uECC_curve_private_key_size(curve), eccKeyPrivate);
        config["ecdsa-pubk"] = toBase64(uECC_curve_public_key_size(curve), eccKeyPublic);
        config.flashWrite();

        Serial.print("finished signature and verify in "); Serial.println(stopWatchKeyGeneration.getMillis());
    }

    // print public key to console, you can pick it and copy&paste it to your client for verifing the signature
    Serial.print("using eccKeyPublic: "); Serial.println((const char*)config["ecdsa-pubk"]);


    // init wifi/network
    Network.setHostname((const char*)config["hostname"]);
    Network.begin();    // initialize network manager
    wifiConnect();      // start wifi


    LED(CRGB::HTMLColorCode::Green);

    IPAddress ipMqtt;
    if (true == Network.hostByName(MQTT_SERVER, ipMqtt))
    {
        Serial.println("mqtt-server-ip: " + ipMqtt.toString());
        client.setServer(ipMqtt, MQTT_PORT);
        mqttServerPublish();
    }


    Serial.println(getDateTime() + " setup done");
}


void loop() 
{
    if (Bridge.available())
    {
        LED(CRGB::HTMLColorCode::Purple);
        stopWatchLed.reset();

        while (1)
        {
            if (!Bridge.available()) break;

            String serialData = Bridge.readStringUntil('\n');
            serialData.trim();
            if (false == mqttServerPublish(serialData.c_str())) LED(CRGB::HTMLColorCode::Red);


            Serial.print("bridge received: ");
            Serial.println(serialData.c_str());
        }

        stopWatchKeepAlive.reset();
    }
    else
    {
        if (60 <= stopWatchKeepAlive.getSeconds())
        {
            LED(CRGB::HTMLColorCode::Green);
            stopWatchLed.reset();

            if (false == mqttServerPublish())
            {
                LED(CRGB::HTMLColorCode::Red);
                stopWatchLed.reset();
            } 
            
            stopWatchKeepAlive.reset();
        }
    }

    // always turn of leds after 500 ms
    if (500 < stopWatchLed.getMillis()) LED(CRGB::HTMLColorCode::Black);

    M5.update();

    delay(100);
}
