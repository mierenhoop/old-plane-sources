#include <ESP8266WiFi.h>
#include <espnow.h>

#define RED D3
#define GREEN D4
#define SPEED_UP D5
#define SPEED_DOWN D6

#define CENTER_DEADZONE
#define OUTER_DEADZONE

#define PACKET_DELAY 10
// 500 packets => 500 * 10ms = 5s window
#define PACKET_LOSS_WINDOW 500

static u8 plane_address[] = { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc };

static struct __attribute__((packed)) {
  u8 left = 0, right = 0;
} motor_packet;

static struct __attribute__((packed)) {
  u8 battery_status;
} response_packet;

static struct {
private:
  u8 ring_buffer[PACKET_LOSS_WINDOW] = { 0 };
  u16 index = 0;
  u16 count = 0;
public:
  void report_loss() {
    count += 1 - ring_buffer[index];
    ring_buffer[index++] = 1;
    if (index >= PACKET_LOSS_WINDOW) index = 0;
  }

  void report_success() {
    count -= ring_buffer[index];
    ring_buffer[index++] = 0;
    if (index >= PACKET_LOSS_WINDOW) index = 0;
  }

  int get_percentage() {
    return count * 100 / PACKET_LOSS_WINDOW;
  }
} packet_loss;

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing Controller...");

  pinMode(RED, OUTPUT);
  pinMode(GREEN, OUTPUT);

  pinMode(RED, OUTPUT);
  pinMode(GREEN, OUTPUT);

  WiFi.persistent(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(plane_address, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

  esp_now_register_send_cb([](u8 *, u8 status) {
    Serial.print("Motor Packet Send Status: ");
    if (status == 0) {
      digitalWrite(GREEN, HIGH);
      digitalWrite(RED, LOW);
      Serial.println("Success");
      packet_loss.report_success();
    } else {
      digitalWrite(GREEN, LOW);
      digitalWrite(RED, HIGH);
      Serial.println("Fail");
      packet_loss.report_loss();
    }
  });

  esp_now_register_recv_cb([](u8 *, u8 *data, u8 len) {
    Serial.print("Response Packet Receive Status: ");
    if (sizeof(response_packet) != len) {
      Serial.println("Fail");
      return;
    }
    Serial.println("Success");
    memcpy(&response_packet, data, len);
  });


  Serial.println("Controller fully initialized");

  pinMode(D1, INPUT_PULLUP);
  pinMode(D2, INPUT_PULLUP);
}

void loop() {
  static unsigned long last = 0;

  if (millis() > last + PACKET_DELAY) {
    last = millis();

    static int speed = 0;

    if (digitalRead(D1) == LOW) speed = min(speed + 1, 265);
    if (digitalRead(D2) == LOW) speed = max(speed - 1, -10);
    Serial.printf("Package loss: %d%\n", packet_loss.get_percentage());

    int v = analogRead(A0);
    // for now assume v between [-128, 128]

    motor_packet.left = min(max(speed - max(-v, 0), 0), 255);
    motor_packet.right = min(max(speed - max(v, 0), 0), 255);

    esp_now_send(plane_address, (uint8_t *)&motor_packet, sizeof(motor_packet));
  }
}
