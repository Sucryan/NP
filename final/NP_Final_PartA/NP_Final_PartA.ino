#include <stdio.h>
#include <stdlib.h>
#include <WiFiS3.h>
#include <ArduinoGraphics.h>
#include <Arduino_LED_Matrix.h>
#include "wifi_secrets_client.h"
#include "animation.h"

char wifi_ssid[] = WIFI_SSID;
char wifi_pass[] = WIFI_PASS;

int wifi_status = WL_IDLE_STATUS;
WiFiClient client;

const char* host = "nycu.waynewolf.tw";
const uint16_t port = 80;
const char* path = "/weather/weather.do";
ArduinoLEDMatrix matrix;

// 從PictureAnimation貼過來魔改。
void tempAnimate(const char* raw_info) {
  /* Obtain temperature */
  char time_info[32];
  char temp_info[16];
  char humid_info[16];
  // 把raw data用逗號抓出來。
  sscanf(
    raw_info,
    "%31[^,],%15[^,],%15s",
    time_info,
    temp_info,
    humid_info
  );

  float temp = atof(temp_info);
  int humid = atoi(humid_info);

  int mood = 0;

  if (temp > 30 && humid >= 70) {
    mood = 3;  // 死亡：又熱又濕
  } else if (temp < 15 && humid >= 70) {
    mood = 4;  // 哭臉：濕冷
  } else if (temp > 30) {
    mood = 5;  // 超熱
  } else if (temp < 15) {
    mood = 1;  // 發抖
  } else if (humid >= 70) {
    mood = 2;  // 傻眼臉：有點悶濕
  } else {
    mood = 0;  // 笑臉：舒服
  }
  // 這裡加：先顯示表情
  matrix.loadFrame(animation[mood]);
  delay(1200);

  char display_info[128];
  memset(display_info, 0, 128);
  snprintf(
    display_info,
    sizeof(display_info),
    "Time: %s Temp. %s C Humid. %s%%",
    time_info,
    temp_info,
    humid_info
  );
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textScrollSpeed(50);
  matrix.textFont(Font_4x6);
  matrix.beginText(12, 1, 0xFFFFFF);
  matrix.println(display_info);
  matrix.endText(SCROLL_LEFT);
  matrix.endDraw();

}

void printWifiData() {
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  byte mac[6];
  WiFi.macAddress(mac);
  Serial.print("MAC Address: ");
  printMacAddress(mac);
}

void printMacAddress(byte mac[]) {
  for (int i = 0; i < 6; i++) {
    if (i > 0) {
      Serial.print(":");
    }
    if (mac[i] < 16) {
      Serial.print("0");
    }
    Serial.print(mac[i], HEX);
  }
  Serial.println();
}

void printCurrentNet() {
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  byte bssid[6];
  WiFi.BSSID(bssid);
  Serial.print("BSSID: ");
  printMacAddress(bssid);

  long rssi = WiFi.RSSI();
  Serial.print("RSSI: ");
  Serial.println(rssi);

  byte encryption = WiFi.encryptionType();
  Serial.print("Encryption Type:");
  Serial.println(encryption, HEX);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  matrix.begin();
  while (!Serial) {
    ;
  }

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);
  }

  while (wifi_status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(wifi_ssid);
    wifi_status = WiFi.begin(wifi_ssid, wifi_pass);

    delay(10000);
  }

  Serial.println("You're connected to the network.");
  printCurrentNet();
  printWifiData();
}


void loop() {
  if (wifi_status != WiFi.status()) {
    wifi_status = WiFi.status();
  }

  if (client.connect(host, port)) {
    char serial_verbose[128];
    memset(serial_verbose, 0, 128);
    sprintf(serial_verbose, "Connect to server %s at %d", host, port);
    Serial.println(serial_verbose);
    const char* body = "time=now&location=hsinchu";

    client.print("POST ");
    client.print(path);
    client.println(" HTTP/1.1");

    client.print("Host: ");
    client.println(host);

    client.println("Content-Type: application/x-www-form-urlencoded");

    client.print("Content-Length: ");
    client.println(strlen(body));

    client.println("Connection: close");
    client.println();

    client.print(body);

    Serial.println("HTTP POST sent.");
    String header;
    String raw_info;
    bool is_body = false;
    unsigned long start = millis();
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        // write跟println不一樣，write(65)會輸出'A'，println(65)會輸出65
        // Serial.write(c); // for debug，他會print出整個包含header, body的內容
        // Serial.println()
        if (!is_body) {
          header += c;
          if (header.endsWith("\r\n\r\n")) {
            is_body = true;
          }
        }
        else {
          raw_info += c;
        }
      }
      // 確認有沒有timeout
      if (millis() - start > 5000) {
        Serial.println("\nTimeout while reading response.");
        break;
      }
    }
    Serial.println(raw_info);
    tempAnimate(raw_info.c_str());
  } else {
    char serial_verbose[128];
    memset(serial_verbose, 0, 128);
    sprintf(serial_verbose, "Cannot reach server %s at %d", host, port);
    Serial.println(serial_verbose);
    free(serial_verbose);
  }

  delay(10000);
}

