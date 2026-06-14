#include <stdio.h>
#include <stdlib.h>
#include <WiFiS3.h>
#include <DHT11.h>
#include "wifi_secrets_ap.h"

#define DHT11PIN 2
DHT11 dht11(DHT11PIN);

char wifi_ssid[] = WIFI_SSID;
char wifi_pass[] = WIFI_PASS;

int wifi_status = WL_IDLE_STATUS;

WiFiClient client;

const char* host = "172.16.4.2";
const uint16_t port = 50000;

void print_wifi_status() {
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print where to go in a browser:
  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial) {
    ;
  }

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);
  }

  WiFi.config(IPAddress(172,16,4,1));

  Serial.print("Creating access point named: ");
  Serial.println(wifi_ssid);

  wifi_status = WiFi.beginAP(wifi_ssid, wifi_pass);
  if (wifi_status != WL_AP_LISTENING) {
    Serial.println("Cannot create access point!");
    while (true);
  }

  delay(10000);
  print_wifi_status();
}

void loop() {
  int temperature_reading = 0;
  int humidity_reading = 0;
  while (dht11.readTemperatureHumidity(temperature_reading, humidity_reading) != 0) {
    ;
  }

  if (wifi_status != WiFi.status()) {
    wifi_status = WiFi.status();
  }
  
  if (client.connect(host, port)) {

    char to_send[128];
    memset(to_send, 0, 128);
    sprintf(to_send, "%d,%d\0", temperature_reading, humidity_reading);

    Serial.print("Connected to server ");
    Serial.print(host);
    Serial.print(":");
    Serial.println(port);

    client.println("POST /weather HTTP/1.1");

    client.print("Host: ");
    client.print(host);
    client.print(":");
    client.println(port);

    client.println("Content-Type: text/plain");

    client.print("Content-Length: ");
    client.println(strlen(to_send));

    client.println("Connection: close");
    client.println();

    client.print(to_send);
    // for debug
    Serial.print("Data sent: ");
    Serial.println(to_send);

    unsigned long last_activity = millis();
    // 跟part A差不多
    while (client.connected() || client.available()) {
        if (client.available()) {
            char c = client.read();
            Serial.write(c);
            last_activity = millis();
        }

        if (millis() - last_activity > 3000) {
            Serial.println("\nServer response timeout.");
            break;
        }
    }

    client.stop();
    Serial.println("\nConnection closed.");
  } 
  // 10秒再一次
  delay(10000);
}