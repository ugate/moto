// ----------------- ESP8266 Server -----------------------------

void handleNotFound() {
  String msg = "File Not Found\n\n";
  msg += "URI: " + server.uri() + "\nMethod: " + (server.method() == HTTP_GET) ? "GET" : "POST";
  msg += "\nArguments: " + server.args();
  msg += "\n";
  for (uint8_t i = 0; i < server.args(); i++) msg += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  server.send(404, "text/plain", msg);
}

void getData() {
  //Serial.println("GET JSON DATA");
  if (net_flags & NET_NEEDS_SETUP) {
    return server.send(200, "application/json", "{\"setup\":{\"ssid\":{\"value\":\"" + String(ssid) + "\",\"maxlength\":" + WL_SSID_MAX_LENGTH + "},\"password\":{\"value\":\"\",\"maxlength\":" + WL_WPA_KEY_MAX_LENGTH + "}}}");
  }
  server.send(200, "application/json", "{\"anim\":{}}");
}

void postData() {
  //Serial.println("POST JSON");
  if (net_flags & NET_NEEDS_SETUP) {
    if (!server.hasArg("ssid") || !server.hasArg("password")) return server.send(400, "text/plain", "Missing POST SSID and/or Password");
    String ssidWeb = server.arg("ssid"); String passWeb = server.arg("password");
    if (!writeSSID(ssidWeb, passWeb)) return server.send(400, "text/plain", "Invalid POST SSID/Password size(s)");
    if (sizeof(ssid) == 0 || sizeof(password) == 0) return server.send(500, "text/plain", "Failed to write SSID/Password");
    net_flags &= ~NET_NEEDS_SETUP; // remove
    return server.send(200, "text/plain", "Saved SSID/Password");
  } else server.send(400, "text/plain", "POST SSID/Password are already set");
}

bool writeSSID(String& ssidNew, String& passNew) {
  if (ssidNew.length() < 3 || passNew.length() < 8 || ssidNew.length() > WL_SSID_MAX_LENGTH || passNew.length() > WL_WPA_KEY_MAX_LENGTH) return false;
  File file = SPIFFS.open(SSID_FILE, "r+");
  file.println(ssidNew); file.println(passNew);
  file.close();
  //byte ssidData[ssidNew.length()]; byte passData[passNew.length()];
  //ssidNew.toCharArray((char*) ssidData, ssidNew.length());
  //passNew.toCharArray((char*) passData, passNew.length());
  //ssidFile.write((const uint8_t*) ssidData, ssidNew.length());
  //passFile.write((const uint8_t*) passData, passNew.length());
  readSSID();
  //Serial.printf("Wrote NEW SSID:%s,PASS:%s\n", ssid, password);
  return true;
}

void readSSID() {
  File file = SPIFFS.open(SSID_FILE, "r+");
  if (file.available()) {
    ssid = (char*) file.readStringUntil('\n').c_str(); //Serial.printf("Using Saved SSID: %s (size: %d)\n", ssid, sizeof(&ssid));
  } else {
    net_flags |= NET_NEEDS_SETUP; // add
    ssid = (char*) ssidDefault; //Serial.printf("Using default SSID: %s\n", ssid);
  }
  if (file.available()) {
    password = (char*) file.readStringUntil('\n').c_str(); //Serial.printf("Using Saved password **** (size: %d)\n", sizeof(&password));
  } else {
    net_flags |= NET_NEEDS_SETUP; // add
    password = (char*) passwordDefault; //Serial.printf("Using default password: %s\n", password);
  }
  if (sizeof(&ssid) == 0 || sizeof(&password) == 0) net_flags |= NET_NEEDS_SETUP; // add
  file.close();
}

void getManifest() {
  //Serial.println("Sending manifest for Web App Install Banner");
  server.send(200, "application/json",
  "{"
  "  \"short_name\": \"Moto Moon\","
  "  \"name\": \"Moto Moon Lighting\","
  "  \"icons\": ["
  "    {"
  "      \"src\": \"favicon.png\","
  "      \"type\": \"image/png\","
  "      \"sizes\": \"48x48\""
  "    },"
  "    {"
  "      \"src\": \"favicon.png\","
  "      \"type\": \"image/png\","
  "      \"sizes\": \"96x96\""
  "    },"
  "    {"
  "      \"src\": \"favicon.png\","
  "      \"type\": \"image/png\","
  "      \"sizes\": \"192x192\""
  "    }"
  "  ],"
  "  \"start_url\": \"/?launcher=true\","
  "  \"background_color\": \"#00BCD4\","
  "  \"theme_color\": \"#00BCD4\","
  "  \"display\": \"standalone\""
  "}");
}

// should be called in the main loop
void netLoop() {
  if (domain != "") dnsServer.processNextRequest();
  server.handleClient();
}

// should be called in the main setup
IPAddress netSetup() {
  SPIFFS.begin();
  readSSID();
  uint8_t wstat = WL_CONNECTED;
  IPAddress ipLoc;
  if (domain != "") {
    WiFi.mode(WIFI_AP); // need both to serve the webpage and take commands via TCP
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid, password); // password can be removed if the AP should be open
    dnsServer.setTTL(300); // TTL seconds with domain name (default 60 seconds)
    dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure); // reduce #of queries sent by client (default DNSReplyCode::NonExistentDomain)
    dnsServer.start(DNS_PORT, domain, ip);
    ipLoc = WiFi.softAPIP(); //Serial.printf("Access Point started for %s", ssid);
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    uint8_t wstatp = WL_CONNECTED;
    do {
      WiFi.status();
      delay(500);
      if ((wstat = WiFi.status()) != wstatp) statusIndicator(SETUP_STAT_NONE, wstat);
      wstatp = wstat;
    } while (WiFi.status() != WL_CONNECTED);
    ipLoc = WiFi.localIP();
  }
  server.onNotFound(handleNotFound);
  server.serveStatic("/favicon.ico", SPIFFS, "/favicon.png", "public, max-age=31536000");
  server.serveStatic("/favicon.png", SPIFFS, "/favicon.png", "public, max-age=31536000");
  server.serveStatic("/", SPIFFS, "/index.htm");
  /*server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", html);
  });*/
  server.on("/data", HTTP_GET, getData);
  server.on("/data", HTTP_POST, postData);
  server.on("/manifest.json", HTTP_GET, getManifest);
  server.begin();
  statusIndicator(SETUP_STAT_NONE, wstat, ipLoc.toString().c_str());
  return ip;
}
