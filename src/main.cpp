#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <RunningAverage.h>
#include <InfluxDbClient.h>
#include <TelnetStream.h>
#include <SoftwareSerial.h>
#include <CRC16.h>

#include <secrets.h> /*Edit this file to include the following details.

#define SECRET_SSID "your_ssid"
#define SECRET_PASS "your_password"
#define SECRET_INFLUXDB_URL "http://10.x.x.x:8086"
#define SECRET_INFLUXDB_TOKEN "your_token"
#define SECRET_INFLUXDB_ORG "default"
#define SECRET_INFLUXDB_BUCKET "your_bucket"
#define SECRET_MQTT_SERVER "10.x.x.x"
#define SECRET_MQTT_USER "your_MQTT_username"
#define SECRET_MQTT_PASS "your_MQTT_password"
*/

#define MQTT
#define debugEnabled 1

#ifdef MQTT
  #include <PubSubClient.h>
  WiFiClient espClient;
  PubSubClient MQTTclient(espClient);
  const char *MQTTclientId = "Sungrow-S100";
  const char *MQTTtopicPrefix = "home/solar/sungrow";
  char MQTTtopic[70];
  char statString[20];
#endif

InfluxDBClient InfluxDBclient(SECRET_INFLUXDB_URL, SECRET_INFLUXDB_ORG, SECRET_INFLUXDB_BUCKET, SECRET_INFLUXDB_TOKEN);

const char* hostname = "ESP32-C3-S100-Monitor";

int failures = 0; //The number of failed operations. Will automatically restart the ESP if too many failures occurr in a row.

byte currentByte = 0x0;
byte state = 0;
byte data[10];
float watts = 0;
unsigned long lastMODBUS = 0;
RunningAverage averageWatts(120);
unsigned long lastInflux = 0;
unsigned long lastAverage = 0;
Point influxWatts("Feeder power");
CRC16 crc(0x8005, 0xFFFF, 0x0000, true, true);
bool inverterOffline = 0;

EspSoftwareSerial::UART MODBUSSerial;

void setup()
{
  Serial.begin(115200);
  MODBUSSerial.begin(9600, EspSoftwareSerial::SWSERIAL_8N1, 0, 1, false, 128);

  // ****Start ESP32 OTA and Wifi Configuration****
  if (debugEnabled == 1) {
    Serial.println();
    Serial.print("Connecting to "); Serial.println(SECRET_SSID);
  }
  WiFi.setHostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(SECRET_SSID, SECRET_PASS); //Edit include/secrets.h to update this data.

  unsigned long connectTime = millis();
  if (debugEnabled == 1) {
    Serial.print("Waiting for WiFi to connect");
  }
  while (!WiFi.isConnected() && (unsigned long)(millis() - connectTime) < 5000) { //Wait for the wifi to connect for up to 5 seconds.
    delay(100);
    if (debugEnabled == 1) {
      Serial.print(".");
    }
  }
  if (!WiFi.isConnected()) {
    if (debugEnabled == 1) {
      Serial.println();
      Serial.println("WiFi didn't connect, restarting...");
    }
    ESP.restart(); //Restart if the WiFi still hasn't connected.
  }

  if (debugEnabled == 1) {
    Serial.println();
    Serial.print("IP address: "); Serial.println(WiFi.localIP());
  }

  // Port defaults to 3232
  ArduinoOTA.setPort(3232);

  ArduinoOTA.setHostname(hostname);

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  // ****End ESP32 OTA and Wifi Configuration****

  #ifdef MQTT
    MQTTclient.setServer(SECRET_MQTT_SERVER, 1883);
  #endif

  // Telnet log is accessible at port 23
  TelnetStream.begin();

  // Check the InfluxDB connection
  if (InfluxDBclient.validateConnection()) {
    if (debugEnabled == 1) {
      Serial.print("Connected to InfluxDB: ");
      Serial.println(InfluxDBclient.getServerUrl());
    }
  } else {
    if (debugEnabled == 1) {
      Serial.print("InfluxDB connection failed: ");
      Serial.println(InfluxDBclient.getLastErrorMessage());
      Serial.print("Restarting...");
    }
    ESP.restart();
  }
}


void sendInflux() {
  influxWatts.clearFields();
  influxWatts.clearTags();
  influxWatts.addTag("sensor", "Sungrow-S100");
  influxWatts.addField("value", averageWatts.getAverage());

  TelnetStream.print("Sending data to InfluxDB: ");
  TelnetStream.println(InfluxDBclient.pointToLineProtocol(influxWatts));
  
  if (!InfluxDBclient.writePoint(influxWatts)) { //Write the data point to InfluxDB
    failures++;
    TelnetStream.print("InfluxDB write failed: ");
    TelnetStream.println(InfluxDBclient.getLastErrorMessage());
  }
  else if (failures >= 1) failures --;
}

void sendMQTT() {
  sprintf(MQTTtopic, "%s/Feeder_power", MQTTtopicPrefix);
  if (MQTTclient.connect(MQTTclientId, SECRET_MQTT_USER, SECRET_MQTT_PASS)) {
    dtostrf(watts, 1, 1, statString);
    TelnetStream.printf("Posting %s to MQTT topic %s \r\n", statString, MQTTtopic);
    MQTTclient.publish(MQTTtopic, statString, (bool)1);
    
    if (failures >= 1) failures--;
  }
  else {
    TelnetStream.print("MQTT connection failed: "); TelnetStream.println(MQTTclient.state());
    failures++;
  }
}

void sendMODBUS() {
  MODBUSSerial.write((byte) 0x20);
  MODBUSSerial.write((byte) 0x03);
  MODBUSSerial.write((byte) 0x00);
  MODBUSSerial.write((byte) 0x09);
  MODBUSSerial.write((byte) 0x00);
  MODBUSSerial.write((byte) 0x02);
  MODBUSSerial.write((byte) 0x12);
  MODBUSSerial.write((byte) 0xB8);
}

void processMODBUS() {
  currentByte = MODBUSSerial.read();
  if (currentByte == 0x20 && state == 0) {
    data[0] = currentByte;
    crc.restart();
    crc.add(currentByte);
    state = 1; //We potentially found the start of a packet.
  }
  else if (currentByte == 0x03 && state == 1) {
    data[1] = currentByte;
    crc.add(currentByte);
    state = 2; //Second byte confirmed.
  }
  else if (currentByte == 0x04 && state == 2) {
    data[2] = currentByte;
    crc.add(currentByte);
    state = 3; //Third byte confirmed.
  }
  else if (state == 3) {
    data[3] = currentByte;
    crc.add(currentByte);
    state = 4;
  }
  else if (state == 4) {
    data[4] = currentByte;
    crc.add(currentByte);
    state = 5;
  }
  else if (state == 5) {
    data[5] = currentByte;
    crc.add(currentByte);
    state = 6;
  }
  else if (state == 6) {
    data[6] = currentByte;
    crc.add(currentByte);
    state = 7;
  }
  else if (state == 7) {
    data[7] = currentByte;
    state = 8;
  }
  else if (state == 8) {
    data[8] = currentByte;
    //TelnetStream.printf("Received CRC: %x\r\n", (data[8] << 8 | data[7]));
    //TelnetStream.printf("Computed CRC: %x\r\n", crc.calc());

    if ((data[8] << 8 | data[7]) == crc.calc()) { //Check the MODBUS CRC is correct.

      watts = (((data[3] << 24) + (data[4] << 16) + (data[5] << 8) + data[6])) * 0.1;
    
      if (watts > 20000 || watts < -20000) {
        TelnetStream.printf("Received data outside range: %.1fW\r\n", watts);
        watts = 0.0;
      }
      else {
        TelnetStream.printf("Power consumption is: %.1fW\r\n", watts);
        lastMODBUS = millis();
      }

      if ((unsigned long)(millis() - lastAverage) >= 1000 && inverterOffline == 1) { //Only calculate the average and send data to MQTT if the inverter appears to be offline.
        averageWatts.addValue(watts);
        sendMQTT();
        lastAverage = millis();
      }
    }
    else {
      TelnetStream.printf("Received CRC (%x) doesn't match calculated CRC (%x)\r\n", (data[8] << 8 | data[7]), crc.calc());
    }
    state = 0;
    inverterOffline = 0;
  }
  else state = 0;
}

void loop()
{
  ArduinoOTA.handle();

  #ifdef MQTT
    MQTTclient.loop();
    if (!MQTTclient.connected()) {
      TelnetStream.println("MQTT disconnected. Attempting to reconnect..."); 
      if (MQTTclient.connect(MQTTclientId, SECRET_MQTT_USER, SECRET_MQTT_PASS)) {
        if (failures >= 1) failures--;
        TelnetStream.println("MQTT Connected.");
      }
      else {
        TelnetStream.print("MQTT connection failed: "); TelnetStream.println(MQTTclient.state());
        failures++;
      }
      delay(1000);
    }
  #endif

  if (WiFi.status() != WL_CONNECTED) {
    if (debugEnabled == 1) {
      Serial.println("WiFi disconnected. Attempting to reconnect... ");
    }
    failures++;
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    delay(1000);
  }


  if (MODBUSSerial.available()) {
    processMODBUS();
  }

  if ((unsigned long)(millis() - lastMODBUS) >= 2000) { //Send the S100 a MODBUS query if nothing has been received for more than 2 seconds.
    TelnetStream.println("Sending MODBUS request...");
    sendMODBUS();
    inverterOffline = 1;
    lastMODBUS = millis();
  }

  if ((unsigned long)(millis() - lastInflux) >= 120000 && averageWatts.getCount() >= 50 && inverterOffline == 1) { //Send averaged data to InfluxDB every 2 minutes
    sendInflux();
    lastInflux = millis();
    averageWatts.clear();
  }

  if (failures >= 20) {
    if (debugEnabled == 1) {
      Serial.print("Too many failures, rebooting...");
    }
    TelnetStream.print("Failure counter has reached: "); TelnetStream.print(failures); TelnetStream.println(". Rebooting...");
    delay(1);
    ESP.restart(); //Reboot the ESP if too many problems have been counted.
  }
}