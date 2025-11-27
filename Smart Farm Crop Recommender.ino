// Include all necessary libraries
#include "secrets.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include "esp_camera.h"
#include "DHT.h"

// --- I2C LCD LIBRARIES (NEW) ---
#include <Wire.h>
#include <LiquidCrystal_I2C.h>


// --- PIN DEFINITIONS FOR SENSORS ---
#define DHTPIN 14
#define SOIL_MOISTURE_PIN 13
#define PH_SENSOR_PIN 12
#define WATER_LEVEL_PIN 15

#define DHTTYPE DHT11

// --- I2C LCD DEFINITIONS (NEW) ---
// Note: Your LCD address might be 0x3F. If 0x27 doesn't work, try that.
#define LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2
#define I2C_SDA 1 // Remapped SDA Pin (U0TXD)
#define I2C_SCL 3 // Remapped SCL Pin (U0RXD)

// --- PIN DEFINITIONS FOR CAMERA ---
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// --- AWS IOT CORE & MQTT TOPICS ---
#define AWS_IOT_SENSORS_TOPIC "esp32/sensors"
#define AWS_IOT_IMAGE_TOPIC   "esp32/image/chunk"
#define AWS_IOT_SUBSCRIBE_TOPIC "esp32/sub"

// --- SENSOR & CLIENT OBJECTS ---
DHT dht(DHTPIN, DHTTYPE);
WiFiClientSecure net = WiFiClientSecure();
PubSubClient client(net);

// --- LCD OBJECT (NEW) ---
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// --- FUNCTION PROTOTYPES ---
void connectAWS();
void messageHandler(char* topic, byte* payload, unsigned int length);
void publishSensorData();
void captureAndPublishImage();
bool initCamera();

void setup() {
  Serial.begin(115200);
  Serial.println("Waking up...");

  // --- Initialize LCD (NEW) ---
  Wire.begin(I2C_SDA, I2C_SCL); // Initialize I2C communication on remapped pins
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  delay(1000);
  lcd.clear();

  // Initialize Sensors
  dht.begin();

  // Initialize Camera
  if (!initCamera()) {
    Serial.println("FATAL: Camera initialization failed!");
    lcd.setCursor(0,0);
    lcd.print("Camera Failed!");
    // You might want to restart the ESP32 here
    ESP.restart();
  }

  // Connect to WiFi and AWS
  connectAWS();

  Serial.println("Setup complete. Entering loop...");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Setup Complete");
  delay(1500);
}

void loop() {
  // Check connection, reconnect if needed.
  if (!client.connected()) {
    connectAWS();
  }

  // Publish all sensor data in a single JSON message
  publishSensorData();
  delay(2000); // Small delay between sensor and image publishing

  // Capture a new image and publish it in chunks
  captureAndPublishImage();

  // Keep the MQTT client running
  client.loop();

  // Wait for a long interval before sending the next batch of data
  // Sending an image frequently is data-intensive and costly.
  Serial.println("Data sent. Going to sleep for 5 minutes.");
  lcd.clear();
  lcd.print("Sleeping 5 min");
  delay(300000); // 300,000 ms = 5 minutes
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Frame size: SVGA (800x600) is a good balance of quality and size
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 12; // 0-63, lower number means higher quality
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return false;
  }
  return true;
}


void connectAWS() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to Wi-Fi");
  lcd.clear();
  lcd.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.print(".");
  }
  Serial.println("\nWi-Fi Connected!");

  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  client.setServer(AWS_IOT_ENDPOINT, 8883);
  client.setCallback(messageHandler);

  Serial.println("Connecting to AWS IoT Core...");
  lcd.clear();
  lcd.print("Connecting AWS..");
  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    delay(100);
  }

  if (!client.connected()) {
    Serial.println("AWS IoT Timeout! Check credentials and endpoint.");
    lcd.clear();
    lcd.print("AWS Connect Fail");
    return;
  }

  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
  Serial.println("AWS IoT Connected!");
  lcd.clear();
  lcd.print("AWS Connected!");
  delay(1500);
}

void messageHandler(char* topic, byte* payload, unsigned int length) {
  Serial.print("Incoming message from topic: ");
  Serial.println(topic);
  // Handle any incoming messages here if needed
}

void publishSensorData() {
  // Read all sensor values
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  int soilMoistureRaw = analogRead(SOIL_MOISTURE_PIN);
  int phSensorRaw = analogRead(PH_SENSOR_PIN);
  int waterLevelRaw = analogRead(WATER_LEVEL_PIN);

  // Check if reads failed
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    lcd.clear();
    lcd.print("DHT Read Error!");
    return;
  }

  // --- CALIBRATION REQUIRED ---
  float soilMoisturePercent = map(soilMoistureRaw, 4095, 1000, 0, 100);
  float phValue = map(phSensorRaw, 0, 4095, 0, 14);
  float waterLevelPercent = map(waterLevelRaw, 0, 4095, 0, 100);

  // --- DISPLAY DATA ON LCD ---
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(temperature, 1);
  lcd.print("C H:");
  lcd.print(humidity, 1);
  lcd.print("%");

  lcd.setCursor(0, 1);
  lcd.print("Soil:");
  lcd.print((int)soilMoisturePercent);
  lcd.print("%");

  // Create JSON payload for topic publish
  StaticJsonDocument<512> doc;
  doc["deviceId"] = THINGNAME;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["soil_moisture"] = soilMoisturePercent;
  doc["ph_value"] = phValue;
  doc["water_level"] = waterLevelPercent;
  doc["timestamp"] = millis();

  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);

  // Publish to normal IoT topic
  client.publish(AWS_IOT_SENSORS_TOPIC, jsonBuffer);
  Serial.println("Published sensor data to AWS.");
  Serial.println(jsonBuffer);

  // -------------------------------
  StaticJsonDocument<256> shadow;
  shadow["state"]["reported"]["temperature"] = temperature;
  shadow["state"]["reported"]["humidity"] = humidity;
  shadow["state"]["reported"]["soil_moisture"] = soilMoisturePercent;
  shadow["state"]["reported"]["ph_value"] = phValue;
  shadow["state"]["reported"]["water_level"] = waterLevelPercent;

  char shadowBuf[256];
  serializeJson(shadow, shadowBuf);

  client.publish("$aws/things/ESP32farm/shadow/update", shadowBuf);

  Serial.println("Updated Thing Shadow:");
  Serial.println(shadowBuf);
}

void captureAndPublishImage() {
  Serial.println("Capturing image...");
  lcd.clear();
  lcd.print("Capturing Image");
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    lcd.setCursor(0,1);
    lcd.print("Capture Failed!");
    return;
  }
  
  Serial.printf("Image captured! Size: %zu bytes\n", fb->len);
  lcd.clear();
  lcd.print("Uploading Image");

  // --- Chunking and Publishing ---
  // We split the image into small chunks to send over MQTT
  const int chunkSize = 2048; // Size of each chunk in bytes
  int totalChunks = (fb->len + chunkSize - 1) / chunkSize;

  for (int i = 0; i < totalChunks; i++) {
    int offset = i * chunkSize;
    int chunkLen = (i == totalChunks - 1) ? (fb->len - offset) : chunkSize;
    
    // The payload is just the raw bytes of the image chunk
    if(client.publish(AWS_IOT_IMAGE_TOPIC, (uint8_t*)fb->buf + offset, chunkLen)) {
       Serial.printf("Published chunk %d/%d (%d bytes)\n", i + 1, totalChunks, chunkLen);
       // Optional: Update LCD with progress
       lcd.setCursor(0,1);
       lcd.printf("Chunk %d/%d ", i+1, totalChunks);
    } else {
       Serial.printf("Failed to publish chunk %d\n", i + 1);
    }
    // A small delay and client.loop() can help with network stability for large transfers
    delay(10); 
    client.loop();
  }

  // Send a final message to indicate the end of the image transmission
  client.publish(AWS_IOT_IMAGE_TOPIC, "END");
  Serial.println("Finished publishing image chunks.");
  lcd.clear();
  lcd.print("Upload Complete");

  // IMPORTANT: Return the frame buffer to be reused
  esp_camera_fb_return(fb);
}
