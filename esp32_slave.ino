#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESP32Servo.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// --- ESP-NOW Reflex Layer ---
// REPLACE with the MAC Address of your MASTER ESP32
uint8_t masterAddress[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; 

// MUST MATCH the Wi-Fi channel of your home router!
#define WIFI_CHANNEL 6 

typedef struct struct_message { char payload[32]; } struct_message;
struct_message commandIn;
struct_message stateOut;

Adafruit_MPU6050 mpu;

// --- Leg Hardware Allocations ---
Servo leftHip, rightHip, leftAnkle, rightAnkle;
const int PIN_L_HIP = 18, PIN_R_HIP = 19, PIN_L_ANKLE = 22, PIN_R_ANKLE = 23;
const int MID_L_HIP = 90, MID_R_HIP = 90, MID_L_ANKLE = 90, MID_R_ANKLE = 90;

String currentMovementMode = "stand_still";
unsigned long lastStateUpdate = 0;

// --- ESP-NOW Receive Callback ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    memcpy(&commandIn, incomingData, sizeof(commandIn));
    currentMovementMode = String(commandIn.payload);
}

void setupESPNOW() {
    WiFi.mode(WIFI_STA);
    
    // Bind hardware radio to Master's router frequency
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) return;
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, masterAddress, 6);
    peerInfo.channel = WIFI_CHANNEL;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void setup() {
    Serial.begin(115200);
    setupESPNOW();

    Wire.begin(21, 22); 
    if (!mpu.begin()) { while (1) { delay(10); } }

    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpu.setFilterBandwidth(MPU6050_BANDWIDTH_44_HZ);

    leftHip.attach(PIN_L_HIP); rightHip.attach(PIN_R_HIP);
    leftAnkle.attach(PIN_L_ANKLE); rightAnkle.attach(PIN_R_ANKLE);
    leftHip.write(MID_L_HIP); rightHip.write(MID_R_HIP);
    leftAnkle.write(MID_L_ANKLE); rightAnkle.write(MID_R_ANKLE);
}

void runStabilizationSystem() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float dynamicRoll = a.acceleration.y; 
    float Kp = 4.0; // Tuning modifier
    int adjustmentValue = constrain(int(dynamicRoll * Kp), -22, 22);

    String dynamicState = (abs(adjustmentValue) > 15) ? "STATE:TILT_RECOVERY" : "STATE:STABLE";

    // Beam status back to Master
    if (millis() - lastStateUpdate > 100) {
        dynamicState.toCharArray(stateOut.payload, 32);
        esp_now_send(masterAddress, (uint8_t *) &stateOut, sizeof(stateOut));
        lastStateUpdate = millis();
    }

    if (currentMovementMode == "stand_still") {
        leftAnkle.write(MID_L_ANKLE + adjustmentValue);
        rightAnkle.write(MID_R_ANKLE + adjustmentValue);
        leftHip.write(MID_L_HIP);
        rightHip.write(MID_R_HIP);
    }
}

void executeLocomotionStep() {
    leftAnkle.write(MID_L_ANKLE - 14); rightAnkle.write(MID_R_ANKLE - 14); delay(180);
    rightHip.write(MID_R_HIP + 20); leftHip.write(MID_L_HIP + 20); delay(220);
    
    leftAnkle.write(MID_L_ANKLE + 14); rightAnkle.write(MID_R_ANKLE + 14); delay(180);
    rightHip.write(MID_R_HIP - 20); leftHip.write(MID_L_HIP - 20); delay(220);
}

void loop() {
    if (currentMovementMode == "move_forward") {
        executeLocomotionStep();
    } else {
        runStabilizationSystem();
    }
    delay(5);
}
