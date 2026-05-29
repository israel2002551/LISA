#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <esp_now.h>
#include <driver/i2s.h>

// --- Secure Public MQTT Setup ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "broker.emqx.io"; 
const char* command_topic = "lisa_biped_xyz987_secure/command";
const char* state_topic = "lisa_biped_xyz987_secure/state";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// --- ESP-NOW Reflex Layer ---
// REPLACE with the actual MAC Address of your SLAVE ESP32
uint8_t slaveAddress[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; 

typedef struct struct_message { char payload[32]; } struct_message;
struct_message commandOut;
struct_message stateIn;

String slaveSystemState = "STATE:STABLE";

// --- Hardware Allocations ---
Servo neckPan, leftShoulder, rightShoulder, leftElbow, rightElbow, leftWrist, rightWrist;
const int PIN_NECK=13, PIN_L_SH=12, PIN_R_SH=14, PIN_L_EL=27, PIN_R_EL=26, PIN_L_WR=25, PIN_R_WR=33;

// I2S Audio Bus (Shared Mic & Speaker)
#define I2S_PORT       I2S_NUM_0
#define PIN_I2S_BCLK   4
#define PIN_I2S_LRC    5
#define PIN_MIC_DOUT   7
#define PIN_SPK_DIN    6

void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_LRC,
        .data_out_num = PIN_SPK_DIN,
        .data_in_num = PIN_MIC_DOUT
    };
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
}

// --- ESP-NOW Receive Callback ---
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    memcpy(&stateIn, incomingData, sizeof(stateIn));
    slaveSystemState = String(stateIn.payload);
    // Forward hardware state to Python brain
    mqtt.publish(state_topic, slaveSystemState.c_str());
}

void setupWiFiAndESPNOW() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    
    if (esp_now_init() != ESP_OK) return;
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, slaveAddress, 6);
    peerInfo.channel = 0; 
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void executeUpperBodyAction(String action) {
    if (slaveSystemState == "STATE:TILT_RECOVERY") {
        leftShoulder.write(90); rightShoulder.write(90);
        leftElbow.write(90); rightElbow.write(90);
        return; // Lock arms if falling
    }

    if (action == "wave_right_arm") {
        rightShoulder.write(150); rightElbow.write(45);
        for(int i=0; i<2; i++) { rightWrist.write(45); delay(200); rightWrist.write(135); delay(200); }
    } else if (action == "celebrate") {
        leftShoulder.write(180); rightShoulder.write(180);
        for(int i=0; i<3; i++) { leftWrist.write(120); rightWrist.write(60); delay(150); leftWrist.write(60); rightWrist.write(120); delay(150); }
    } else if (action == "salute") {
        rightShoulder.write(140); rightElbow.write(90); rightWrist.write(90); delay(800);
    } else { 
        neckPan.write(90); leftShoulder.write(90); rightShoulder.write(90);
        leftElbow.write(90); rightElbow.write(90); leftWrist.write(90); rightWrist.write(90);
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload, length)) return;

    String action = doc["action"] | "idle";
    String movement = doc["movement"] | "stand_still";

    executeUpperBodyAction(action);

    // Relay movement to Slave via ESP-NOW
    movement.toCharArray(commandOut.payload, 32);
    esp_now_send(slaveAddress, (uint8_t *) &commandOut, sizeof(commandOut));
}

void reconnectMQTT() {
    while (!mqtt.connected()) {
        if (mqtt.connect("Lisa_Master_Core")) {
            mqtt.subscribe(command_topic);
        } else {
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    setupWiFiAndESPNOW();
    
    mqtt.setServer(mqtt_server, 1883);
    mqtt.setCallback(mqttCallback);

    neckPan.attach(PIN_NECK); leftShoulder.attach(PIN_L_SH); rightShoulder.attach(PIN_R_SH);
    leftElbow.attach(PIN_L_EL); rightElbow.attach(PIN_R_EL); leftWrist.attach(PIN_L_WR); rightWrist.attach(PIN_R_WR);
    
    executeUpperBodyAction("idle");
    setupI2S();
}

void loop() {
    if (!mqtt.connected()) reconnectMQTT();
    mqtt.loop();
}
