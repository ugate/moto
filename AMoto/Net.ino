// ----------------- ESP8266 Server -----------------------------

void handleNotFound() {
  String msg = "File Not Found\n\n";
  msg += "URI: " + http.uri() + "\nMethod: " + (http.method() == HTTP_GET) ? "GET" : "POST";
  msg += "\nArguments: " + http.args();
  msg += "\n";
  for (uint8_t i = 0; i < http.args(); i++) msg += " " + http.argName(i) + ": " + http.arg(i) + "\n";
  http.send(404, "text/plain", msg);
}

void postLightFlags() { //Serial.println("POST LIGHTING FLAGS JSON");
  String val; bool state;
  if (http.hasArg("brakeOn")) {
    val = http.arg("brakeOn");
    state = val == "true" ? brakeOn() : brakeOff();
    val = val == "true" ? "ON: " : "OFF: ";
    http.send(200, "application/json", "{\"message\":\"Brake lights turned " + val + String(state) + "\"}");
  } else if (http.hasArg("turnLeftOn")) {
    val = http.arg("turnLeftOn");
    state = val == "true" ? turnLeftOn() : turnLeftOff();
    val = val == "true" ? "ON: " : "OFF: ";
    http.send(200, "application/json", "{\"message\":\"Left turn signal turned " + val + String(state) + "\"}");
  } else if (http.hasArg("turnRightOn")) {
    val = http.arg("turnRightOn");
    state = val == "true" ? turnRightOn() : turnRightOff();
    val = val == "true" ? "ON: " : "OFF: ";
    http.send(200, "application/json", "{\"message\":\"Right turn signal turned " + val + String(state) + "\"}");
  } else http.send(400, "application/json", "{\"message\":\"Invalid payload for setting lighting flags\",\"isError\":true}");
}

void getSetup() { //Serial.println("GET SETUP/ANIM JSON");
  if (net_flags & NET_NEEDS_SETUP) {
    http.send(200, "application/json", "{\"setup\":{\"ssid\":{\"value\":\"" + String(wifi.ssid) + "\",\"maxlength\":" + WL_SSID_MAX_LENGTH + "},\"password\":{\"value\":\"\",\"maxlength\":" + WL_WPA_KEY_MAX_LENGTH + "}}}");
  } else http.send(200, "application/json", "{\"anim\":" + getAnimJson() + "}");
}

void postSetup() { //Serial.println("POST SETUP JSON");
  if (net_flags & NET_NEEDS_SETUP) {
    if (!http.hasArg("ssid") || !http.hasArg("password")) {
      http.send(400, "application/json", "{\"message\":\"Missing POST SSID and/or Password\",\"isError\":true}"); return;
    }
    String ssidWeb = http.arg("ssid"); String passWeb = http.arg("password");
    if (!write_wifi_eeprom(ssidWeb, passWeb)/*!write_wifi_spiffs(ssidWeb, passWeb)*/) {
      http.send(400, "application/json", "{\"message\":\"Invalid POST SSID/Password size(s)\",\"isError\":true}"); return;
    }
    if (sizeof(wifi.ssid) == 0 || sizeof(wifi.pass) == 0) {
      http.send(500, "application/json", "{\"message\":\"Failed to write SSID/Password\",\"isError\":true}"); return;
    }
    net_flags &= ~NET_NEEDS_SETUP; // remove
    http.send(200, "application/json", "{\"message\":\"Setup complete for SSID: " + String(wifi.ssid) + ". Reconnect to WiFi with new credentials to proceed!\",\"reconnect\":{}}");
    delay(1000); // allow time to send response
    netStop(NET_RESTART);
  } else http.send(200, "application/json", "{\"message\":\"POST SSID/Password are already set\"}");
}

void clear_wifi_eeprom() {
  for (int i = 0; i < sizeof(wifi); i++) EEPROM.write(i, 0); // clear
  EEPROM.commit();
}

bool write_wifi_eeprom(String& ssidNew, String& passNew) {
  if (ssidNew.length() < 3 || passNew.length() < 8 || ssidNew.length() > WL_SSID_MAX_LENGTH || passNew.length() > WL_WPA_KEY_MAX_LENGTH) return false;
  clear_wifi_eeprom(); Serial.printf("Writting NEW WiFi SSID to EEPROM: \"%s\", PASS: \"%s\"\n", ssidNew.c_str(), passNew.c_str());
  byte hasSetup = 1;
  strcpy(wifi.ssid, ssidNew.c_str());
  strcpy(wifi.pass, passNew.c_str());
  EEPROM.put(0, hasSetup);
  EEPROM.put(1, wifi);
  EEPROM.commit();
  read_wifi_eeprom(); Serial.printf("Wrote NEW WiFi SSID: \"%s\", PASS: \"%s\"\n", wifi.ssid, wifi.pass);
  return true;
}

void read_wifi_eeprom() {
  byte hasSetup = 0;
  EEPROM.get(0, hasSetup); Serial.printf("Read WiFi setup flag \"%u\"\n", hasSetup);
  if (hasSetup != 1) {
    net_flags |= NET_NEEDS_SETUP; // add
    strcpy(wifi.ssid, ssidDefault);
    strcpy(wifi.pass, passDefault); Serial.printf("Using default SSID: \"%s\"\nUsing default password \"%s\"\n", wifi.ssid, wifi.pass);
  } else {
    EEPROM.get(1, wifi); Serial.printf("Reading Saved SSID: \"%s\"\nReading Saved password \"%s\"\n", wifi.ssid, wifi.pass);
  }
  if (sizeof(wifi.ssid) == 0 || sizeof(wifi.pass) == 0) net_flags |= NET_NEEDS_SETUP; // add
}

bool write_wifi_spiffs(String& ssidNew, String& passNew) {
  if (ssidNew.length() < 3 || passNew.length() < 8 || ssidNew.length() > WL_SSID_MAX_LENGTH || passNew.length() > WL_WPA_KEY_MAX_LENGTH) return false;
  File file = SPIFFS.open(SSID_FILE, "w+");
  if (!file) return false;
  file.println(ssidNew);
  file.println(passNew);
  file.close();
  //byte ssidData[ssidNew.length()]; byte passData[passNew.length()];
  //ssidNew.toCharArray((char*) ssidData, ssidNew.length());
  //passNew.toCharArray((char*) passData, passNew.length());
  //ssidFile.write((const uint8_t*) ssidData, ssidNew.length());
  //passFile.write((const uint8_t*) passData, passNew.length());
  read_wifi_spiffs();//Serial.printf("Wrote NEW SSID:%s,PASS:%s\n", wifi.ssid, wifi.pass);
  return true;
}

void read_wifi_spiffs() {
  File file = SPIFFS.open(SSID_FILE, "r+");
  if (file && file.available()) {
    bool isPass = false;
    char r;
    int i = 0;
    while (file.available()) {
      r = char(file.read());
      if (r == '\0') {
        if (isPass) wifi.pass[i] = r;
      } else wifi.ssid[i] = r;
    } //Serial.printf("Using Saved SSID: %s (size: %d)\nUsing Saved password **** (size: %d)\n", wifi.ssid, sizeof(wifi.ssid), sizeof(wifi.pass));
    //wifi.ssid = (char*) file.readStringUntil('\n').c_str();
    //wifi.pass = (char*) file.readStringUntil('\n').c_str();
  } else { //Serial.printf("Reading default SSID: %s\nUsing default password: %s\n", wifi.ssid, wifi.pass);
    net_flags |= NET_NEEDS_SETUP; // add
    strcpy(wifi.ssid, ssidDefault);
    strcpy(wifi.pass, passDefault);
    //wifi.ssid = (char*) ssidDefault; 
    //wifi.pass = (char*) passDefault;
  }
  if (sizeof(wifi.ssid) == 0 || sizeof(wifi.pass) == 0) net_flags |= NET_NEEDS_SETUP; // add
  file.close();
}

// gets the comma delimited list of files on SPIFFS
String get_file_names_spiffs() {
  String str = "";
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    str += dir.fileName();
    str += ":";
    str += dir.fileSize();
    str += ",";
  }
  return str;
}

void getManifest() { //Serial.println("Sending manifest for Web App Install Banner");
  http.send(200, "application/json",
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

String getAnimJson() {
  return String(
  "{"
  " \"ssid\": {"
  "   \"value\": \"" + String(wifi.ssid) + "\","
  "   \"maxlength\": " + WL_SSID_MAX_LENGTH + ""
  " },"
  " \"animations\": ["
  "   {"
  "     \"label\": \"Noise\","
  "     \"value\": " + ANIM_NOISE + ""
  "   }"
  " ]"
  "}");
}

// stops/restarts net services
void netStop(const uint8_t type) {
  if (type == NET_RESTART || type == NET_SOFT_RESTART || type == NET_SOFT_STOP) {
    dns.stop();
    http.close();
    if (domain != "") WiFi.disconnect(true);
    else WiFi.softAPdisconnect(true);
    statusIndicator(SETUP_STAT_STOPPED, WL_DISCONNECTED); 
  }
  if (type == NET_SOFT_RESTART) {
    delay(1000);
    netSetup();
    delay(1000);
  } else if (type == NET_RESTART) {
    delay(1000);
    ESP.restart(); // reseting the CPU will loose current flags
  }
}

// should be called in the main loop
void netLoop() {
  if (domain != "") dns.processNextRequest();
  http.handleClient();
}

// should be called in the main setup
IPAddress netSetup() {
  SPIFFS.begin();
  //read_wifi_spiffs();
  EEPROM.begin(sizeof(wifi)); //clear_wifi_eeprom();
  read_wifi_eeprom();
  uint8_t wstat = WL_CONNECTED;
  IPAddress ipLoc;
  if (domain != "") {
    WiFi.mode(WIFI_AP); // need both to serve the webpage and take commands via TCP
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(wifi.ssid, wifi.pass); // password can be removed if the AP should be open
    dns.setTTL(300); // TTL seconds with domain name (default 60 seconds)
    dns.setErrorReplyCode(DNSReplyCode::ServerFailure); // reduce #of queries sent by client (default DNSReplyCode::NonExistentDomain)
    dns.start(DNS_PORT, domain, ip);
    ipLoc = WiFi.softAPIP(); //Serial.printf("Access Point started for %s", wifi.ssid);
    statusIndicator(SETUP_STAT_NONE, wstat);//WiFi.status() // status will initially be disconnected
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi.ssid, wifi.pass);
    uint8_t wstatp = WL_CONNECTED;
    do {
      WiFi.status();
      delay(500);
      if ((wstat = WiFi.status()) != wstatp) statusIndicator(SETUP_STAT_NONE, wstat);
      wstatp = wstat;
    } while (WiFi.status() != WL_CONNECTED);
    ipLoc = WiFi.localIP();
  }
  http.onNotFound(handleNotFound);
  http.serveStatic("/favicon.ico", SPIFFS, "/favicon.png", "public, max-age=31536000");
  http.serveStatic("/favicon.png", SPIFFS, "/favicon.png", "public, max-age=31536000");
  http.serveStatic("/", SPIFFS, "/index.htm");
  http.serveStatic("/home", SPIFFS, "/index.htm");
  http.on("/manifest.json", HTTP_GET, getManifest);
  /*http.on("/", HTTP_GET, []() {
    http.send(200, "text/html", html);
  });*/
  http.on("/setup", HTTP_GET, getSetup);
  http.on("/setup", HTTP_POST, postSetup);
  http.on("/apply/brake", HTTP_POST, postLightFlags);
  http.on("/apply/turn/left", HTTP_POST, postLightFlags);
  http.on("/apply/turn/right", HTTP_POST, postLightFlags);
  http.begin();
  statusIndicator(SETUP_STAT_NONE, wstat, ipLoc.toString().c_str(), get_file_names_spiffs().c_str());
  return ip;
}
