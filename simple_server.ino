//
// A simple server implementation showing how to:
//  * serve static messages
//  * read GET and POST parameters
//  * handle missing pages / 404s
//

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static const char *index_page = R"html(
<!DOCTYPE html>
<html>

<head>
<meta name="viewport" content="user-scalable=no" />
<script>

window.onload = _ => {
    if ('ontouchstart' in document.documentElement) {
        document.documentElement.addEventListener("click", _ => {
            if (document.documentElement.webkitRequestFullscreen)
                document.documentElement.webkitRequestFullscreen();
            else if (document.documentElement.requestFullscreen)
                document.documentElement.requestFullscreen();
            else
                return;
            screen.orientation.lock("landscape");
        }, { once: true });
    }

    let speed = {
        left: 0,
        right: 0,
    };

    let motorspeed = {
        left: 0,
        right: 0,
    };

    let updates = 0;
    
    const setInfo = (id, content) => document.getElementById(id).innerHTML = content;
    
    function connect() {
        let ws = new WebSocket("ws://"+window.location.hostname+"/ws");
        ws.onopen = _ => setInfo("ws-status", "connected");
        ws.onclose = _ => setInfo("ws-status", "not connected");
        setInterval(_ => {
            if (ws.readyState == ws.OPEN) {
                let buffer = new Uint8Array([motorspeed.left,motorspeed.right]);
                ws.send(buffer);
                updates++;
                setInfo("ws-updates", updates);
                setInfo("rtt", navigator.connection.rtt)
            }
        }, 500);

        ws.onmessage = ev => {
            let reader = new FileReader();
            reader.onload = _ => {
                let buf = new Uint8Array(reader.result);
                setInfo("left-speed-motor", buf[0]);
                setInfo("right-speed-motor", buf[1]);
                setInfo("n-clients", buf[2]);
                setInfo("battery", buf[3] + "%");
            }
            reader.readAsArrayBuffer(ev.data);
        };

        ws.onclose = () => {
            setInterval(() => {
                connect();
            }, 1000);
        };
    }
    connect();

    const enableSlider = dir => {
        const element = document.getElementById(dir);
        const clamp = (value, min, max) => Math.min(Math.max(value, min), max);
        let value = null;
        element.ontouchstart = ev => value = ev.targetTouches[0].clientY;
        element.ontouchmove = ev => {
            let delta = value - ev.targetTouches[0].clientY; // Finger position delta
            delta /= window.innerHeight; // Normalize to [0, 1]
            delta *= 255; // Normalize to byte
            delta *= 2; // Speed up a bit
            // Clamping bit below 0 to make it easier to full stop
            speed[dir] = clamp(speed[dir] + delta, -10, 255);
            motorspeed[dir] = clamp(Math.round(speed[dir]), 0, 255);
            setInfo(dir+"-speed-client", Math.round(speed[dir]));
            setInfo(dir+"-perc", Math.round(motorspeed[dir]/255*100)+"%")
            value = ev.targetTouches[0].clientY;
        };
        const stop = _ => value = null;
        element.ontouchcancel = stop;
        element.ontouchend = stop;
    };
    enableSlider("left");
    enableSlider("right");


};

</script>
<style>
* {
    margin: 0;
    user-select: none;
}

body {
    background-color: #336699;
}

#info {
    /* position: absolute; */
    margin-left: auto;
    width: 100vh;
    color: white;
}

#left,
#right {
    background-color: #00000020;
    position: absolute;
    width: 45vw;
    height: 100vh;
}

#left {
    left: 0;
}

#right {
    right: 0;
}

#left-perc, #right-perc {
    color: white;
    width: 100%;
    text-align: center;
}
</style>
</head>

<body>
<div id="left"><div id="left-perc">0%</div></div>
<div id="right"><div id="right-perc">0%</div></div>
<pre id="info">
        Web socket: <span id="ws-status">not connected</span>
      Updates sent: <span id="ws-updates">0</span>
 Number of clients: <span id="n-clients">0</span>
    Battery status: <span id="battery">0%</span>
               RTT: <span id="rtt"></span>
 Left speed client: <span id="left-speed-client">0</span>
  Left speed motor: <span id="left-speed-motor">0</span>
Right speed client: <span id="right-speed-client">0</span>
 Right speed motor: <span id="right-speed-motor">0</span>
</pre>
</body>

</html>
)html";

struct __attribute__((packed)) input_packet {
  uint8_t left = 0;
  uint8_t right = 0;
};

struct __attribute__ ((packed)) response_packet {
  input_packet in_packet;
  uint8_t n_clients = 0;
  uint8_t battery_status = 0;
};

uint8_t battery = 0;

void setup() {
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(5, OUTPUT);

  WiFi.softAP("netwerk");
  // if (WiFi.waitForConnectResult() != WL_CONNECTED) {
  //   Serial.printf("WiFi Failed!\n");
  //   return;
  // }

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, [](auto req) {
    req->send(200, "text/html", index_page);
  });

  server.onNotFound([](auto req) {
    req->send(404, "text/plain", "Not found");
  });

  ws.onEvent([](auto, AsyncWebSocketClient *client, auto type,
                void *, uint8_t *data, size_t len) {
    switch (type) {
      case WS_EVT_CONNECT:
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        break;
      case WS_EVT_DISCONNECT:
        {
          Serial.printf("WebSocket client #%u disconnected\n", client->id());
          break;
        }
      case WS_EVT_DATA:
        {
          if (len == sizeof(input_packet)) {
            input_packet *in_packet = reinterpret_cast<input_packet *>(data);
            Serial.printf("%d %d\n", in_packet->left, in_packet->right);

            analogWrite(LED_BUILTIN, in_packet->left);
            analogWrite(5, in_packet->right);
            
            response_packet out_packet;
            out_packet.in_packet = *in_packet;
            out_packet.n_clients = ws.getClients().length();
            out_packet.battery_status = battery;
            client->binary(reinterpret_cast<char*>(&out_packet), sizeof(response_packet));
          } else {
            Serial.println("Got wrong input packet");
          }
          break;
        }
      case WS_EVT_PONG:
      case WS_EVT_ERROR:
        break;
    }
  });
  server.addHandler(&ws);

  server.begin();
}

void loop() {
  static int last = 0;
  if (millis() > last + 1000) {
    last = millis();

    battery = analogRead(A0) / 4;
  }

  ws.cleanupClients();
}