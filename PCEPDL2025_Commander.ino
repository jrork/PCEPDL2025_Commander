#include <TFT_eSPI.h>           // Graphics library for ESP32
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson


const char* ssid ="ESPap";
const char* password = "thereisnospoon";
const char* mqtt_server = "192.168.4.1";

WiFiClient espClient;
PubSubClient client(espClient);

// Define touchscreen chip select and interrupt pins (adjust as needed)
// Touchscreen pins
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

// Initialize touchscreen and TFT display objects
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();  // Configure in User_Setup.h

// Structure for a button
struct Button {
  int x, y, w, h;
  bool selected;
  String label;
};

// Create an array to hold four buttons
Button buttons[4];

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());              // WTF does this do?

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());    // TODO: Check to see why this never works
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32Controller-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("outTopic", "controller is connected");
      // ... and resubscribe
      const char* subTopic = "command/+";
      client.subscribe(subTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  setup_wifi();                         // Connect to the WiFi of the MQTT Broker
  client.setServer(mqtt_server, 1883);  // Connect to the MQTT Broker

  // Initialize display and set rotation (adjust as needed)
  tft.init();
  tft.fillScreen(TFT_BLACK);
  
  // Initialize the touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchscreenSPI);
  
  // Calculate button dimensions based on the screen size
  int btnWidth = tft.width();
  int btnHeight = tft.height() / 4;
  
    // Define four buttons arranged in a 1x4 grid
  buttons[0] = {0,           0, btnWidth, btnHeight, false, "Off"};
  buttons[1] = {0,   btnHeight, btnWidth, btnHeight, false, "Mode 1"};
  buttons[2] = {0, btnHeight*2, btnWidth, btnHeight, false, "Mode 2"};
  buttons[3] = {0, btnHeight*3, btnWidth, btnHeight, false, "Mode 3"};
  // Initially select the first button
  buttons[0].selected = true;
  
  // Draw the buttons on the screen
  drawButtons();
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (ts.touched()) {
    // Get the touch point. Note: Depending on your touchscreen, you might need to remap coordinates.
    TS_Point p = ts.getPoint();
    int touchX = map(p.y, 240, 3800, 1, SCREEN_WIDTH);
    int touchY = map(p.x, 200, 3700, 1, SCREEN_HEIGHT);
    // Debug: print touch coordinates to Serial
    // Serial.print("Touched at: ");
    // Serial.print(touchX);
    // Serial.print(", ");
    // Serial.println(touchY);
    // printButtonXY();
    
    // Determine which button is pressed
    for (int i = 0; i < 4; i++) {
      if (touchX >= buttons[i].x && touchX <= (buttons[i].x + buttons[i].w) &&
          touchY >= buttons[i].y && touchY <= (buttons[i].y + buttons[i].h)) {
        // Set all buttons to unselected, then select the pressed button
        for (int j = 0; j < 4; j++) {
          buttons[j].selected = false;
        }
        buttons[i].selected = true;
        drawButtons();
        sendButtonPressed(i);
        break;
      }
    }
    
    // A small delay to debounce touch input
    delay(200);
  }
}

// Function to draw a single button
void drawButton(Button &btn, int index) {
  // Choose fill color based on whether the button is selected
  uint16_t fillColor = btn.selected ? TFT_BLUE : TFT_DARKGREY;
  uint16_t borderColor = TFT_WHITE;
  
  // Draw the button rectangle
  tft.fillRect(btn.x, btn.y, btn.w, btn.h, fillColor);
  tft.drawRect(btn.x, btn.y, btn.w, btn.h, borderColor);
  
  // Set text attributes and draw a label in the center of the button
  tft.setTextColor(TFT_WHITE, fillColor);
  tft.setTextDatum(MC_DATUM); // Center text
  tft.drawString(btn.label, btn.x + btn.w / 2, btn.y + btn.h / 2, 2);
}

// Function to redraw all buttons
void drawButtons() {
  for (int i = 0; i < 4; i++) {
    drawButton(buttons[i], i);
  }
}

void printButtonXY() {
  for (int i=0; i<4; i++) {
    Serial.print(i);
    Serial.print(" ");
    Serial.print(buttons[i].x);
    Serial.print(" ");
    Serial.print(buttons[i].x + buttons[i].w);
    Serial.print(" - ");
    Serial.print(buttons[i].y);
    Serial.print(" ");
    Serial.println(buttons[i].y + buttons[i].h);
  }
}

void sendButtonPressed(int button) {
  DynamicJsonDocument jsonDoc(64);
  jsonDoc["mode"] = button;
  char msg[64];
  serializeJson(jsonDoc, msg);
  Serial.print("Publish message: ");
  Serial.println(msg);
  client.publish("command/0", msg);
}