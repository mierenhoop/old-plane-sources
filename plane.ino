#include <ESP8266WiFi.h>
#include <espnow.h>

#define DEBUG

#ifdef DEBUG
#define DEBUGF(...) Serial.printf(__VA_ARGS__)
#define DEBUGLN(...) Serial.println(__VA_ARGS__)
#else
#define DEBUGF(...)
#define DEBUGLN(...)
#endif

static u8 controller_address[] = { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc };

#define LEFT_MOTOR_PIN D4
#define RIGHT_MOTOR_PIN D5
#define BATTERY_PIN A0

#define DEFAULT_RESPONSE_DELAY 2000
#define DISCONNECT_TIMEOUT 1000

static int response_delay = DEFAULT_RESPONSE_DELAY;


static struct __attribute__((packed)) motor_packet {
  u8 left = 0, right = 0;
  u8 new_response_delay; // 0 => keep old, 1..255 => pow(2,new_response_delay)
} motor;

static struct __attribute__((packed)) {
  motor_packet current_motor_status;
  u8 battery_status;
} response_packet;

static void applyMotor() {
  analogWrite(LEFT_MOTOR_PIN, (int)motor.left);
  analogWrite(RIGHT_MOTOR_PIN, (int)motor.right);
}

static unsigned long last_packet_received = 0;

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
#endif
  DEBUGLN("Initializing Plane...");

  pinMode(LEFT_MOTOR_PIN, OUTPUT);
  pinMode(RIGHT_MOTOR_PIN, OUTPUT);

  WiFi.persistent(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != 0) {
    DEBUGLN("Error initializing ESP-NOW");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_add_peer(controller_address, ESP_NOW_ROLE_CONTROLLER, 1, NULL, 0);

#ifdef DEBUG
  esp_now_register_send_cb([](u8 *, u8 status) {
    DEBUGLN("Response Packet Send Status: ");
    DEBUGLN(status == 0 ? "Delivery success" : "Delivery fail");
  });
#endif

  esp_now_register_recv_cb([](u8 *mac_addr, u8 *data, u8 len) {
    DEBUGLN("Motor Packet Receive Status: ");
    if (len != sizeof(motor)) {
      DEBUGLN("Delivery fail");
      return;
    }
    memcpy(&motor, data, len);
    if (motor.new_response_delay)
      response_delay = pow(2, (int) motor.new_response_delay);
    applyMotor();
    last_packet_received = millis();
    DEBUGLN("Delivery success");
    DEBUGF("Packet: %d %d\n", motor.left, motor.right);
  });

  DEBUGLN("Plane fully initialized");
}

static unsigned long last_response_sent = 0;

void loop() {
  unsigned long current_time = millis();

  if (current_time > last_packet_received + DISCONNECT_TIMEOUT) {
    // turn off motors
    motor.left = 0;
    motor.right = 0;
    applyMotor();
  }

  if (current_time > last_response_sent + response_delay) {

    response_packet.battery_status = analogRead(BATTERY_PIN);
    memcpy(&response_packet.current_motor_status, &motor, sizeof(motor_packet));
    esp_now_send(controller_address, (uint8_t *)&response_packet, sizeof(response_packet));
    last_response_sent = current_time;
  }
}
