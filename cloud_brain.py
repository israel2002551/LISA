import cv2
import base64
import json
import time
from groq import Groq
import paho.mqtt.client as mqtt

# --- Secure Public MQTT Configuration ---
MQTT_BROKER = "broker.emqx.io"
# IMPORTANT: Keep these topics unique and random! Do not use generic names.
MQTT_COMMAND_TOPIC = "lisa_biped_xyz987_secure/command"
MQTT_STATE_TOPIC = "lisa_biped_xyz987_secure/state"

# Replace with your wireless camera's actual RTSP network stream URL
CAMERA_RTSP_URL = "rtsp://YOUR_WIFI_CAMERA_IP:554/stream1"

# 4-Key Round-Robin rotation array to maximize API rate limits
GROQ_KEYS = [
    "YOUR_GROQ_KEY_1",
    "YOUR_GROQ_KEY_2",
    "YOUR_GROQ_KEY_3",
    "YOUR_GROQ_KEY_4"
]
key_index = 0
robot_current_state = "STATE:UNKNOWN"

# --- MQTT Callbacks ---
def on_connect(client, userdata, flags, rc, properties=None):
    print(f"[MQTT] Connected to EMQX Public Broker with status: {rc}")
    client.subscribe(MQTT_STATE_TOPIC)

def on_message(client, userdata, msg):
    global robot_current_state
    try:
        robot_current_state = msg.payload.decode('utf-8').strip()
        print(f"[TELEMETRY] Robot balance status: {robot_current_state}")
    except Exception as e:
        pass

# Initialize MQTT
mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message

try:
    # Connect to the public EMQX sandbox
    mqtt_client.connect(MQTT_BROKER, 1883, 60)
    mqtt_client.loop_start()
except Exception as e:
    print(f"[MQTT CRITICAL] Broker connection failed: {e}")

# --- API Credential Rotation ---
def get_groq_client():
    global key_index
    selected_key = GROQ_KEYS[key_index]
    key_index = (key_index + 1) % len(GROQ_KEYS)
    return Groq(api_key=selected_key)

# --- AI Core Logic ---
def analyze_environment(base64_frame):
    client = get_groq_client()
    system_prompt = (
        "You are Lisa, a physical bipedal humanoid robot. "
        "You perceive your surroundings through a head-mounted Wi-Fi camera feed. "
        f"The robot's physical balancing subsystem currently reports its status as: '{robot_current_state}'. "
        "If the status is 'STATE:TILT_RECOVERY', favor subtle actions or 'idle' to prevent falling over. "
        "You must ONLY respond in minified, raw valid JSON format containing exactly these four keys: "
        "'thought_process', 'speech_output', 'action_command', and 'movement_command'. "
        "Options for action_command: [idle, wave_right_arm, salute, celebrate, look_around]. "
        "Options for movement_command: [stand_still, move_forward]. "
        "Do not wrap your output in markdown code blocks or triple backticks."
    )

    try:
        response = client.chat.completions.create(
            model="llama-3.2-11b-vision-preview",
            messages=[
                {"role": "system", "content": system_prompt},
                {
                    "role": "user",
                    "content": [
                        {"type": "text", "text": "Analyze the frame. Determine your next reactive expressions."},
                        {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{base64_frame}"}}
                    ]
                }
            ],
            temperature=0.4,
            max_tokens=400
        )
        
        raw_text = response.choices[0].message.content.strip()
        if raw_text.startswith("```json"):
            raw_text = raw_text.replace("```json", "").replace("```", "").strip()
        elif raw_text.startswith("```"):
            raw_text = raw_text.replace("```", "").strip()
            
        return json.loads(raw_text)
    except Exception as e:
        print(f"[AI ERROR] API Inference failed: {e}")
        return None

# --- Main Video Loop ---
def main():
    print("[SYSTEM] Opening RTSP connection to Wi-Fi camera...")
    cap = cv2.VideoCapture(CAMERA_RTSP_URL)
    
    last_inference_time = 0
    inference_interval = 4.0  # Seconds between decisions

    while True:
        ret, frame = cap.read()
        if not ret:
            print("[CAMERA WARNING] Reconnecting...")
            cap.open(CAMERA_RTSP_URL)
            time.sleep(1)
            continue

        cv2.imshow("Lisa's Vision", frame)

        current_time = time.time()
        if current_time - last_inference_time > inference_interval:
            # Compress image for faster cloud transit
            resized = cv2.resize(frame, (480, 360))
            _, buffer = cv2.imencode('.jpg', resized, [cv2.IMWRITE_JPEG_QUALITY, 75])
            b64_string = base64.b64encode(buffer).decode('utf-8')
            
            ai_response = analyze_environment(b64_string)
            if ai_response:
                print(f"\n[THOUGHT] {ai_response.get('thought_process')}")
                
                payload = {
                    "action": ai_response.get('action_command', 'idle'),
                    "movement": ai_response.get('movement_command', 'stand_still')
                }
                mqtt_client.publish(MQTT_COMMAND_TOPIC, json.dumps(payload))
                print("[MQTT] Commands dispatched.")
                
            last_inference_time = time.time()
            
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()
    mqtt_client.loop_stop()

if __name__ == "__main__":
    main()
