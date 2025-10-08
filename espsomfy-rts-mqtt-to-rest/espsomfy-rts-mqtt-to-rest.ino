#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <string>
#include <DNSServer.h>
#include <ArduinoJson.h>

const char* REV = "REV0001";

const char *ssidAP = "ConfigureDevice"; //Ap SSID
const char *passwordAP = "";  //Ap Password
char revision[10]; //Read revision 
char ssid[33];     //Read SSID From Web Page
char pass[64];     //Read Password From Web Page
char mqtt[100];    //Read mqtt server From Web Page
char mqttport[6];  //Read mqtt port From Web Page
char mqttuser[64]; //MQTT username
char mqttpass[64]; //MQTT password
char idx[10];      //Read idx From Web Page
char espsomfyhost[64]; //ESPSomfy-RTS hostname
char espsomfyport[6] = "80"; //ESPSomfy-RTS port
char mqtttopic[32] = "root"; //MQTT topic prefix
char apikey[65] = ""; //ESPSomfy-RTS API key
int useMQTTS = 0; //Use MQTTS (SSL/TLS) for MQTT connection
int startServer = 0;
int debug = 1;
int useHttps = 0; //Use HTTPS for ESPSomfy calls
int retryAttempts = 3; //HTTP retry attempts
int mqttRetryInterval = 5000; //MQTT retry interval in ms
ESP8266WebServer server(80);//Specify port 
WiFiClient ESPclient;
WiFiClientSecure ESPclientSecure;
HTTPClient http;

// for the captive network
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
int captiveNetwork = 0;

// for mqtt
int networkConnected = 0;
PubSubClient client(ESPclient);
PubSubClient clientSecure(ESPclientSecure);

// ESPSomfy-RTS API endpoints
const String SHADE_COMMAND_ENDPOINT = "/shadeCommand";
const String GROUP_COMMAND_ENDPOINT = "/groupCommand";
const String TILT_COMMAND_ENDPOINT = "/tiltCommand";
const String SET_POSITIONS_ENDPOINT = "/setPositions";
const String SET_SENSOR_ENDPOINT = "/setSensor";
const String LOGIN_ENDPOINT = "/login";

// Statistics and monitoring
unsigned long httpSuccessCount = 0;
unsigned long httpFailCount = 0;
unsigned long mqttMessageCount = 0;
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 60000; // 1 minute
String lastError = "";
unsigned long lastErrorTime = 0;

// MQTT Logging
int mqttLogging = 0; // 0 = disabled, 1 = enabled
unsigned long logMessageCounter = 0;

////////////////////////////////////////////////////////////////////////////////////////////////
// Setting management
////////////////////////////////////////////////////////////////////////////////////////////////
void dumpEEPROM(){
  char output[10];
  Serial.println("dumpEEPROM start");
  for(int i =0;i<70;i++){
    snprintf(output,10,"%x ",EEPROM.read(i));
    Serial.print(output);
  }
  Serial.println("\ndumpEEPROM stop");
}

// Clear Eeprom
void ClearEeprom(){
  if (debug == 1){
    Serial.println("Clearing Eeprom");
  }
  for (int i = 0; i < 500; ++i) { EEPROM.write(i, 0); }
  if (debug == 1){
    Serial.println("Clearing Eeprom end");
  }
}

void readParam(char* value,int* index){
  char currentChar = '0';
  int i = 0;
  value[0]='\0';
  do{
    currentChar = char(EEPROM.read(*index));
    if(currentChar != '\0'){
      value[i] = currentChar;
    }
    (*index)++;
    i++;
  }while (currentChar != '\0');
  value[i]='\0';
  if (debug == 1){
    Serial.print("readParam (");
    Serial.print(value);
    Serial.println(")");
  }

}
int readRevison(char* value,int* index){
  char currentChar = '0';
  int i = 0;
  value[0]= '\0';
  do{
    currentChar = char(EEPROM.read(*index));
    if(currentChar != '\0'){
      value[i] = currentChar;
    }
    if(*index == 2 && !(value[0] =='R' && value[1] =='E' && value[2] =='V')){
      if (debug == 1){
        Serial.print("Revision read : ");
        Serial.println(value);
      }
      return -1;
    }
    (*index)++;
    i++;
  }while (currentChar != '\0');
  value[i]='\0';
  if (debug == 1){
    Serial.print("Revision read : ");
    Serial.println(value);
  }
  // Compare the revision
  if (strcmp(value,REV) != 0){
    return -2;
  }
  return 0;
}

int getParams(char* revision,char* ssid,char* pass,char* mqtt,char* mqttPort,char* mqttuser,char* mqttpass,char* idx,char* espsomfyhost,char* espsomfyport,char* mqtttopic,char* apikey,int* useMQTTS){
  int i =0;
  int wnReturn = 0;
  wnReturn = readRevison (revision,&i);
  if (debug == 1){
    Serial.print("getParams wnReturn = ");
    Serial.println(wnReturn);
  }
  if (wnReturn <0){
    return -1;
  }

  readParam(ssid,&i);
  readParam(pass,&i);
  readParam(mqtt,&i);
  readParam(mqttPort,&i);
  readParam(mqttuser,&i);
  readParam(mqttpass,&i);
  readParam(idx,&i);
  readParam(espsomfyhost,&i);
  readParam(espsomfyport,&i);
  readParam(mqtttopic,&i);
  readParam(apikey,&i);
  
  // Read MQTTS setting (backward compatibility)
  if (i < 500) {
    char mqttsStr[2];
    readParam(mqttsStr,&i);
    *useMQTTS = atoi(mqttsStr);
  } else {
    *useMQTTS = 0; // Default to non-SSL for old configurations
  }
  
  return 0;
}

void writeParam(const char* value,int* index){
  if (debug == 1){
    Serial.print("writeParam ");
    Serial.print(value);
    Serial.print(" Index = ");
    Serial.println(*index);
  }
  for (int i = 0; i < strlen(value); ++i)
  {
    EEPROM.write(*index, value[i]);
    (*index) ++;
  }
  EEPROM.write(*index, '\0');
  (*index)++;
}
void setParams(char* ssid,char* pass,char* mqtt,char* mqttPort,char* mqttuser,char* mqttpass,char* idx,char* espsomfyhost,char* espsomfyport,char* mqtttopic,char* apikey,int useMQTTS){
  int i =0;
  
  ClearEeprom();//First Clear Eeprom
  delay(10);
  writeParam(REV,&i);
  writeParam(ssid,&i);
  writeParam(pass,&i);
  writeParam(mqtt,&i);
  writeParam(mqttPort,&i);
  writeParam(mqttuser,&i);
  writeParam(mqttpass,&i);
  writeParam(idx,&i);
  writeParam(espsomfyhost,&i);
  writeParam(espsomfyport,&i);
  writeParam(mqtttopic,&i);
  writeParam(apikey,&i);
  
  // Write MQTTS setting
  char mqttsStr[2];
  snprintf(mqttsStr, sizeof(mqttsStr), "%d", useMQTTS);
  writeParam(mqttsStr,&i);
  
  EEPROM.commit();
}
void resetSettings(){
  if (debug == 1){
    Serial.println("");
    Serial.println("Reset EEPROM");  
    Serial.println("Rebooting ESP");
  }
  ClearEeprom();//First Clear Eeprom
  server.send(200, "text/plain", "Reseting settings, ESP will reboot soon");
  delay(100);
  EEPROM.commit();
  ESP.restart();
}

// MQTT Configuration page (accessible when connected to WiFi)
void MQTT_Config_Page() {
  String s = "<!DOCTYPE HTML><html><head><title>MQTT Configuration</title>";
  s += "<style>";
  s += "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f5f5f5; }";
  s += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  s += "h1 { color: #333; text-align: center; }";
  s += ".form-group { margin-bottom: 15px; }";
  s += "label { display: block; margin-bottom: 5px; font-weight: bold; color: #555; }";
  s += "input { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }";
  s += "input:focus { border-color: #007bff; outline: none; }";
  s += ".submit-btn { background-color: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; width: 100%; margin-top: 10px; }";
  s += ".submit-btn:hover { background-color: #0056b3; }";
  s += ".nav-btn { background-color: #6c757d; color: white; padding: 8px 16px; border: none; border-radius: 4px; cursor: pointer; margin-right: 10px; text-decoration: none; display: inline-block; }";
  s += ".nav-btn:hover { background-color: #545b62; }";
  s += ".help-text { font-size: 12px; color: #666; margin-top: 2px; }";
  s += ".current-value { background-color: #e9ecef; padding: 5px; border-radius: 4px; margin-bottom: 5px; font-family: monospace; }";
  s += ".status { padding: 10px; border-radius: 4px; margin-bottom: 15px; }";
  s += ".status.connected { background-color: #d4edda; color: #155724; border: 1px solid #c3e6cb; }";
  s += ".status.disconnected { background-color: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }";
  s += "</style>";
  s += "</head><body>";
  s += "<div class='container'>";
  s += "<h1>MQTT Configuration</h1>";
  
  // Navigation
  s += "<div style='margin-bottom: 20px;'>";
  s += "<a href='/' class='nav-btn'>Home</a>";
  s += "<a href='/status' class='nav-btn'>Status</a>";
  s += "<a href='/stats' class='nav-btn'>Statistics</a>";
  s += "</div>";
  
  // Current MQTT status
  s += "<div class='status ";
  if (networkConnected && client.connected()) {
    s += "connected'>✓ MQTT Connected to " + String(mqtt) + ":" + String(mqttport);
  } else {
    s += "disconnected'>✗ MQTT Disconnected";
  }
  s += "</div>";
  
  s += "<form method='get' action='mqttupdate'>";
  
  // Current values display
  s += "<div class='form-group'>";
  s += "<label>Current MQTT Server:</label>";
  s += "<div class='current-value'>" + String(mqtt) + "</div>";
  s += "<label for='mqtt'>New MQTT Server:</label>";
  s += "<input type='text' id='mqtt' name='mqtt' maxlength='99' placeholder='" + String(mqtt) + "'>";
  s += "<div class='help-text'>IP address or hostname of your MQTT broker (leave empty to keep current)</div>";
  s += "</div>";
  
  s += "<div class='form-group'>";
  s += "<label>Current MQTT Port:</label>";
  s += "<div class='current-value'>" + String(mqttport) + "</div>";
  s += "<label for='mqttport'>New MQTT Port:</label>";
  s += "<input type='number' id='mqttport' name='mqttport' min='1' max='65535' placeholder='" + String(mqttport) + "'>";
  s += "<div class='help-text'>Port number for MQTT broker (leave empty to keep current)</div>";
  s += "</div>";
  
  s += "<div class='form-group'>";
  s += "<label>Current MQTT Username:</label>";
  s += "<div class='current-value'>" + String(strlen(mqttuser) > 0 ? mqttuser : "(not set)") + "</div>";
  s += "<label for='mqttuser'>New MQTT Username:</label>";
  s += "<input type='text' id='mqttuser' name='mqttuser' maxlength='63' placeholder='Enter username or leave empty'>";
  s += "<div class='help-text'>MQTT username (leave empty to keep current, enter 'CLEAR' to remove)</div>";
  s += "</div>";
  
  s += "<div class='form-group'>";
  s += "<label>Current MQTT Password:</label>";
  s += "<div class='current-value'>" + String(strlen(mqttpass) > 0 ? "*** (configured)" : "(not set)") + "</div>";
  s += "<label for='mqttpass'>New MQTT Password:</label>";
  s += "<input type='password' id='mqttpass' name='mqttpass' maxlength='63' placeholder='Enter password or leave empty'>";
  s += "<div class='help-text'>MQTT password (leave empty to keep current, enter 'CLEAR' to remove)</div>";
  s += "</div>";
  
  s += "<div class='form-group'>";
  s += "<label>Current MQTT SSL:</label>";
  s += "<div class='current-value'>" + String(useMQTTS ? "Enabled (MQTTS)" : "Disabled (MQTT)") + "</div>";
  s += "<label for='mqtts'>";
  s += "<input type='checkbox' id='mqtts' name='mqtts' value='1'> Use MQTTS (SSL/TLS)";
  s += "</label>";
  s += "<div class='help-text'>Check to enable secure MQTT connection (typically port 8883)</div>";
  s += "</div>";
  
  s += "<div class='form-group'>";
  s += "<label>Current Topic Prefix:</label>";
  s += "<div class='current-value'>" + String(mqtttopic) + "</div>";
  s += "<label for='mqtttopic'>New Topic Prefix:</label>";
  s += "<input type='text' id='mqtttopic' name='mqtttopic' maxlength='31' placeholder='" + String(mqtttopic) + "'>";
  s += "<div class='help-text'>Prefix for MQTT topics (leave empty to keep current)</div>";
  s += "</div>";
  
  s += "<div class='form-group'>";
  s += "<label>Current API Key:</label>";
  s += "<div class='current-value'>" + String(strlen(apikey) > 0 ? "*** (configured)" : "(not set)") + "</div>";
  s += "<label for='apikey'>New API Key:</label>";
  s += "<input type='text' id='apikey' name='apikey' maxlength='64' placeholder='Enter new API key or leave empty'>";
  s += "<div class='help-text'>ESPSomfy API Key (leave empty to keep current, enter 'CLEAR' to remove)</div>";
  s += "</div>";
  
  s += "<div class='form-group'>";
  s += "<input type='submit' class='submit-btn' value='Update MQTT Configuration'>";
  s += "</div>";
  
  s += "</form>";
  s += "</div>";
  s += "</body></html>";
  
  server.send(200, "text/html", s);
}

// Process MQTT configuration update
void Update_MQTT_Config() {
  bool configChanged = false;
  String message = "MQTT Configuration Updated:<br>";
  
  // Update MQTT server if provided
  if (server.hasArg("mqtt") && server.arg("mqtt").length() > 0) {
    String newMqtt = sanitizeInput(server.arg("mqtt"));
    if (newMqtt.length() <= 99 && newMqtt != String(mqtt)) {
      strcpy(mqtt, newMqtt.c_str());
      message += "- MQTT Server: " + newMqtt + "<br>";
      configChanged = true;
    }
  }
  
  // Update MQTT port if provided
  if (server.hasArg("mqttport") && server.arg("mqttport").length() > 0) {
    String newPort = server.arg("mqttport");
    int portNum = newPort.toInt();
    if (portNum > 0 && portNum <= 65535 && newPort != String(mqttport)) {
      strcpy(mqttport, newPort.c_str());
      message += "- MQTT Port: " + newPort + "<br>";
      configChanged = true;
    }
  }
  
  // Update MQTT username if provided
  if (server.hasArg("mqttuser")) {
    String newUser = server.arg("mqttuser");
    if (newUser == "CLEAR") {
      strcpy(mqttuser, "");
      message += "- MQTT Username: Cleared<br>";
      configChanged = true;
    } else if (newUser.length() > 0 && newUser.length() <= 63) {
      newUser = sanitizeInput(newUser);
      if (newUser != String(mqttuser)) {
        strcpy(mqttuser, newUser.c_str());
        message += "- MQTT Username: Updated<br>";
        configChanged = true;
      }
    }
  }
  
  // Update MQTT password if provided
  if (server.hasArg("mqttpass")) {
    String newPass = server.arg("mqttpass");
    if (newPass == "CLEAR") {
      strcpy(mqttpass, "");
      message += "- MQTT Password: Cleared<br>";
      configChanged = true;
    } else if (newPass.length() > 0 && newPass.length() <= 63) {
      newPass = sanitizeInput(newPass);
      if (newPass != String(mqttpass)) {
        strcpy(mqttpass, newPass.c_str());
        message += "- MQTT Password: Updated<br>";
        configChanged = true;
      }
    }
  }
  
  // Update topic prefix if provided
  if (server.hasArg("mqtttopic") && server.arg("mqtttopic").length() > 0) {
    String newTopic = sanitizeInput(server.arg("mqtttopic"));
    if (newTopic.length() <= 31 && newTopic != String(mqtttopic)) {
      strcpy(mqtttopic, newTopic.c_str());
      message += "- Topic Prefix: " + newTopic + "<br>";
      configChanged = true;
    }
  }
  
  // Update API key if provided
  if (server.hasArg("apikey")) {
    String newApiKey = server.arg("apikey");
    if (newApiKey == "CLEAR") {
      strcpy(apikey, "");
      message += "- API Key: Cleared<br>";
      configChanged = true;
    } else if (newApiKey.length() > 0 && newApiKey.length() <= 64) {
      newApiKey = sanitizeInput(newApiKey);
      if (newApiKey != String(apikey)) {
        strcpy(apikey, newApiKey.c_str());
        message += "- API Key: Updated<br>";
        configChanged = true;
      }
    }
  }
  
  // Update MQTTS setting
  int newUseMQTTS = server.hasArg("mqtts") ? 1 : 0;
  if (newUseMQTTS != useMQTTS) {
    useMQTTS = newUseMQTTS;
    message += "- MQTT SSL: " + String(useMQTTS ? "Enabled" : "Disabled") + "<br>";
    configChanged = true;
  }
  
  if (configChanged) {
    // Save all parameters to EEPROM
    setParams(ssid, pass, mqtt, mqttport, mqttuser, mqttpass, idx, espsomfyhost, espsomfyport, mqtttopic, apikey, useMQTTS);
    
    message += "<br>Settings saved successfully!<br>";
    message += "ESP will restart in 5 seconds to apply changes.";
    
    // Log the configuration change
    publishLog("MQTT configuration updated via web interface", "system");
  } else {
    message = "No changes detected in MQTT configuration.";
  }
  
  // Send response page
  String s = "<!DOCTYPE HTML><html><head><title>MQTT Configuration Updated</title>";
  s += "<style>";
  s += "body { font-family: Arial, sans-serif; text-align: center; margin: 50px; }";
  s += ".success { color: green; }";
  s += ".info { color: blue; }";
  s += ".countdown { font-size: 20px; margin: 20px; }";
  s += ".nav-btn { background-color: #6c757d; color: white; padding: 8px 16px; border: none; border-radius: 4px; cursor: pointer; margin-right: 10px; text-decoration: none; display: inline-block; }";
  s += "</style>";
  
  if (configChanged) {
    s += "<script>";
    s += "var countdown = 5;";
    s += "function updateCountdown() {";
    s += "  document.getElementById('countdown').innerHTML = countdown;";
    s += "  countdown--;";
    s += "  if (countdown < 0) {";
    s += "    document.getElementById('message').innerHTML = 'Restarting now...';";
    s += "  } else {";
    s += "    setTimeout(updateCountdown, 1000);";
    s += "  }";
    s += "}";
    s += "window.onload = updateCountdown;";
    s += "</script>";
  }
  
  s += "</head><body>";
  s += "<h1>MQTT Configuration</h1>";
  s += "<div class='" + String(configChanged ? "success" : "info") + "'>";
  s += message;
  s += "</div>";
  
  if (configChanged) {
    s += "<p>ESP will restart in <span id='countdown' class='countdown'>5</span> seconds...</p>";
    s += "<p id='message'>Please wait for the restart to complete.</p>";
  } else {
    s += "<div style='margin-top: 20px;'>";
    s += "<a href='/mqttconfig' class='nav-btn'>Back to MQTT Config</a>";
    s += "<a href='/' class='nav-btn'>Home</a>";
    s += "</div>";
  }
  
  s += "</body></html>";
  
  server.send(200, "text/html", s);
  
  if (configChanged) {
    delay(1000); // Give time for response to be sent
    if (debug == 1) {
      Serial.println("MQTT configuration updated, restarting ESP");
    }
    ESP.restart();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions
////////////////////////////////////////////////////////////////////////////////////////////////

bool isValidShadeId(int shadeId) {
  return (shadeId >= 1 && shadeId <= 32);
}

bool isValidGroupId(int groupId) {
  return (groupId >= 1 && groupId <= 16);
}

bool isValidPercentage(int value) {
  return (value >= 0 && value <= 100);
}

String sanitizeInput(const String& input) {
  String clean = input;
  clean.replace("<", "&lt;");
  clean.replace(">", "&gt;");
  clean.replace("\"", "&quot;");
  clean.replace("'", "&#x27;");
  clean.replace("&", "&amp;");
  return clean;
}

String buildBaseUrl() {
  String protocol = (useHttps) ? "https://" : "http://";
  return protocol + String(espsomfyhost) + ":" + String(espsomfyport);
}

void logError(const String& error) {
  lastError = error;
  lastErrorTime = millis();
  if (debug == 1) {
    Serial.println("ERROR: " + error);
  }
}

void publishLog(const String& message, const String& logType) {
  // Check if logging is enabled and connection is available
  if (!mqttLogging || !networkConnected) {
    return;
  }
  
  PubSubClient& mqttClient = getMQTTClient();
  if (!mqttClient.connected()) {
    return;
  }
  
  // Avoid excessive memory allocation - limit message size
  if (message.length() > 200) {
    return; // Skip overly long messages to prevent memory issues
  }
  
  // Feed watchdog
  yield();
  
  String logTopic = String(mqtttopic) + "/bridge/log/" + logType;
  logMessageCounter++;
  
  // Create optimized log message with smaller JSON buffer
  DynamicJsonDocument logDoc(256); // Reduced buffer size
  logDoc["t"] = millis(); // Shortened field names
  logDoc["c"] = logMessageCounter;
  logDoc["l"] = logType;
  logDoc["m"] = message;
  
  String logJson;
  logJson.reserve(200); // Pre-allocate string capacity
  serializeJson(logDoc, logJson);
  
  // Feed watchdog before publish
  yield();
  
  bool published = mqttClient.publish(logTopic.c_str(), logJson.c_str());
  
  if (debug == 1) {
    Serial.print("LOG [");
    Serial.print(logType);
    Serial.print("]: ");
    Serial.println(message);
    if (!published) {
      Serial.println("WARNING: Log publish failed");
    }
  }
  
  // Feed watchdog after
  yield();
}

////////////////////////////////////////////////////////////////////////////////////////////////
// ESPSomfy-RTS HTTP REST API calls
////////////////////////////////////////////////////////////////////////////////////////////////

bool authenticateESPSomfy() {
  if (strlen(apikey) == 0) {
    return true; // No authentication needed
  }
  
  String url = buildBaseUrl() + LOGIN_ENDPOINT;
  String payload = "{\"apiKey\":\"" + String(apikey) + "\"}";
  String response;
  
  http.begin(ESPclient, url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000); // 10 second timeout
  
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    response = http.getString();
    http.end();
    
    // Parse JSON response to check success
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error && doc["success"] == true) {
      if (debug == 1) {
        Serial.println("ESPSomfy authentication successful");
      }
      return true;
    }
  }
  
  http.end();
  logError("ESPSomfy authentication failed");
  return false;
}

bool makeHttpRequest(const String& url, String& response) {
  for (int attempt = 1; attempt <= retryAttempts; attempt++) {
    http.begin(ESPclient, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000); // 10 second timeout
    
    // Add API key header if available
    if (strlen(apikey) > 0) {
      http.addHeader("Authorization", "Bearer " + String(apikey));
    }
    
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      response = http.getString();
      if (debug == 1) {
        Serial.print("HTTP Response Code: ");
        Serial.println(httpCode);
        Serial.print("Response (attempt ");
        Serial.print(attempt);
        Serial.print("): ");
        Serial.println(response);
      }
      http.end();
      
      if (httpCode == 200) {
        httpSuccessCount++;
        return true;
      }
    } else {
      if (debug == 1) {
        Serial.print("HTTP Request failed (attempt ");
        Serial.print(attempt);
        Serial.print("), error: ");
        Serial.println(http.errorToString(httpCode).c_str());
      }
    }
    
    http.end();
    
    // Wait before retry (except on last attempt)
    if (attempt < retryAttempts) {
      delay(1000 * attempt); // Progressive delay: 1s, 2s, 3s
    }
  }
  
  httpFailCount++;
  logError("HTTP request failed after " + String(retryAttempts) + " attempts: " + url);
  return false;
}

bool makeHttpPostRequest(const String& url, const String& payload, String& response) {
  http.begin(ESPclient, url);
  http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    response = http.getString();
    if (debug == 1) {
      Serial.print("HTTP POST Response Code: ");
      Serial.println(httpCode);
      Serial.print("Response: ");
      Serial.println(response);
    }
    http.end();
    return (httpCode == 200);
  } else {
    if (debug == 1) {
      Serial.print("HTTP POST Request failed, error: ");
      Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
    return false;
  }
}

void sendShadeCommand(int shadeId, const String& command, int target = -1) {
  if (!isValidShadeId(shadeId)) {
    logError("Invalid shade ID: " + String(shadeId));
    return;
  }
  
  if (target >= 0 && !isValidPercentage(target)) {
    logError("Invalid target percentage: " + String(target));
    return;
  }
  
  String url = buildBaseUrl() + SHADE_COMMAND_ENDPOINT + "?shadeId=" + String(shadeId);
  
  if (target >= 0) {
    url += "&target=" + String(target);
  } else {
    url += "&command=" + command;
  }
  
  // Log the REST API call
  String logMessage = "Shade " + String(shadeId) + " - URL: " + url;
  publishLog(logMessage, "rest_request");
  
  String response;
  bool success = makeHttpRequest(url, response);
  
  // Log the REST API response
  String responseLog = "Shade " + String(shadeId) + " - Success: " + String(success ? "YES" : "NO");
  if (!response.isEmpty()) {
    responseLog += ", Response: " + response;
  }
  publishLog(responseLog, "rest_response");
  
  if (debug == 1) {
    Serial.print("Shade command sent - ID: ");
    Serial.print(shadeId);
    Serial.print(", Command: ");
    Serial.print(command);
    if (target >= 0) {
      Serial.print(", Target: ");
      Serial.print(target);
    }
    Serial.print(", Success: ");
    Serial.println(success ? "YES" : "NO");
  }
  
  // Publish response to MQTT if successful
  if (success && networkConnected && getMQTTClient().connected()) {
    String statusTopic = String(mqtttopic) + "/bridge/shades/" + String(shadeId) + "/status";
    getMQTTClient().publish(statusTopic.c_str(), success ? "OK" : "ERROR");
  }
}

void sendGroupCommand(int groupId, const String& command) {
  if (!isValidGroupId(groupId)) {
    logError("Invalid group ID: " + String(groupId));
    return;
  }
  
  String url = buildBaseUrl() + GROUP_COMMAND_ENDPOINT + "?groupId=" + String(groupId) + "&command=" + command;
  
  // Log the REST API call
  String logMessage = "Group " + String(groupId) + " - URL: " + url;
  publishLog(logMessage, "rest_request");
  
  String response;
  bool success = makeHttpRequest(url, response);
  
  // Log the REST API response
  String responseLog = "Group " + String(groupId) + " - Success: " + String(success ? "YES" : "NO");
  if (!response.isEmpty()) {
    responseLog += ", Response: " + response;
  }
  publishLog(responseLog, "rest_response");
  
  if (debug == 1) {
    Serial.print("Group command sent - ID: ");
    Serial.print(groupId);
    Serial.print(", Command: ");
    Serial.print(command);
    Serial.print(", Success: ");
    Serial.println(success ? "YES" : "NO");
  }
  
  // Publish response to MQTT if successful
  if (success && networkConnected && getMQTTClient().connected()) {
    String statusTopic = String(mqtttopic) + "/bridge/groups/" + String(groupId) + "/status";
    getMQTTClient().publish(statusTopic.c_str(), success ? "OK" : "ERROR");
  }
}

void sendTiltCommand(int shadeId, const String& command, int target = -1) {
  if (!isValidShadeId(shadeId)) {
    logError("Invalid shade ID for tilt: " + String(shadeId));
    return;
  }
  
  if (target >= 0 && !isValidPercentage(target)) {
    logError("Invalid tilt target percentage: " + String(target));
    return;
  }
  
  String url = buildBaseUrl() + TILT_COMMAND_ENDPOINT + "?shadeId=" + String(shadeId);
  
  if (target >= 0) {
    url += "&target=" + String(target);
  } else {
    url += "&command=" + command;
  }
  
  // Log the REST API call
  String logMessage = "Tilt Shade " + String(shadeId) + " - URL: " + url;
  publishLog(logMessage, "rest_request");
  
  String response;
  bool success = makeHttpRequest(url, response);
  
  // Log the REST API response
  String responseLog = "Tilt Shade " + String(shadeId) + " - Success: " + String(success ? "YES" : "NO");
  if (!response.isEmpty()) {
    responseLog += ", Response: " + response;
  }
  publishLog(responseLog, "rest_response");
  
  if (debug == 1) {
    Serial.print("Tilt command sent - ID: ");
    Serial.print(shadeId);
    Serial.print(", Command: ");
    Serial.print(command);
    if (target >= 0) {
      Serial.print(", Target: ");
      Serial.print(target);
    }
    Serial.print(", Success: ");
    Serial.println(success ? "YES" : "NO");
  }
}

void setShadePosition(int shadeId, int position, int tiltPosition = -1) {
  if (!isValidShadeId(shadeId)) {
    logError("Invalid shade ID for position: " + String(shadeId));
    return;
  }
  
  if (position >= 0 && !isValidPercentage(position)) {
    logError("Invalid position percentage: " + String(position));
    return;
  }
  
  if (tiltPosition >= 0 && !isValidPercentage(tiltPosition)) {
    logError("Invalid tilt position percentage: " + String(tiltPosition));
    return;
  }
  
  String url = buildBaseUrl() + SET_POSITIONS_ENDPOINT + "?shadeId=" + String(shadeId);
  
  if (position >= 0) {
    url += "&position=" + String(position);
  }
  
  if (tiltPosition >= 0) {
    url += "&tiltPosition=" + String(tiltPosition);
  }
  
  // Log the REST API call
  String logMessage = "Set Position Shade " + String(shadeId) + " - URL: " + url;
  publishLog(logMessage, "rest_request");
  
  String response;
  bool success = makeHttpRequest(url, response);
  
  // Log the REST API response
  String responseLog = "Set Position Shade " + String(shadeId) + " - Success: " + String(success ? "YES" : "NO");
  if (!response.isEmpty()) {
    responseLog += ", Response: " + response;
  }
  publishLog(responseLog, "rest_response");
  
  if (debug == 1) {
    Serial.print("Position set - ID: ");
    Serial.print(shadeId);
    if (position >= 0) {
      Serial.print(", Position: ");
      Serial.print(position);
    }
    if (tiltPosition >= 0) {
      Serial.print(", Tilt Position: ");
      Serial.print(tiltPosition);
    }
    Serial.print(", Success: ");
    Serial.println(success ? "YES" : "NO");
  }
}

void setSensorStatus(int shadeId, int groupId, int sunny, int windy) {
  if (shadeId >= 0 && !isValidShadeId(shadeId)) {
    logError("Invalid shade ID for sensor: " + String(shadeId));
    return;
  }
  
  if (groupId >= 0 && !isValidGroupId(groupId)) {
    logError("Invalid group ID for sensor: " + String(groupId));
    return;
  }
  
  String url = buildBaseUrl() + SET_SENSOR_ENDPOINT + "?";
  
  if (shadeId >= 0) {
    url += "shadeId=" + String(shadeId);
  } else if (groupId >= 0) {
    url += "groupId=" + String(groupId);
  } else {
    logError("No valid shade or group ID provided for sensor");
    return;
  }
  
  if (sunny >= 0) {
    url += "&sunny=" + String(sunny);
  }
  
  if (windy >= 0) {
    url += "&windy=" + String(windy);
  }
  
  // Log the REST API call
  String logMessage = "Set Sensor";
  if (shadeId >= 0) {
    logMessage += " Shade " + String(shadeId);
  } else {
    logMessage += " Group " + String(groupId);
  }
  logMessage += " - URL: " + url;
  publishLog(logMessage, "rest_request");
  
  String response;
  bool success = makeHttpRequest(url, response);
  
  // Log the REST API response
  String responseLog = "Set Sensor - Success: " + String(success ? "YES" : "NO");
  if (!response.isEmpty()) {
    responseLog += ", Response: " + response;
  }
  publishLog(responseLog, "rest_response");
  
  if (debug == 1) {
    Serial.print("Sensor status set - Success: ");
    Serial.println(success ? "YES" : "NO");
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////
// MQTT management
////////////////////////////////////////////////////////////////////////////////////////////////

// Get the appropriate MQTT client based on SSL setting
PubSubClient& getMQTTClient() {
  return useMQTTS ? clientSecure : client;
}

void publishHeartbeat() {
  PubSubClient& mqttClient = getMQTTClient();
  if (!networkConnected || !mqttClient.connected()) {
    return;
  }
  
  String statusTopic = String(mqtttopic) + "/bridge/status";
  String heartbeatTopic = String(mqtttopic) + "/bridge/heartbeat";
  String statsTopic = String(mqtttopic) + "/bridge/stats";
  
  mqttClient.publish(statusTopic.c_str(), "online", true); // Retained message
  mqttClient.publish(heartbeatTopic.c_str(), String(millis()).c_str());
  
  // Publish statistics
  DynamicJsonDocument stats(512);
  stats["httpSuccess"] = httpSuccessCount;
  stats["httpFails"] = httpFailCount;
  stats["mqttMessages"] = mqttMessageCount;
  stats["uptime"] = millis();
  stats["freeHeap"] = ESP.getFreeHeap();
  stats["lastError"] = lastError;
  stats["lastErrorTime"] = lastErrorTime;
  stats["mqttLogging"] = mqttLogging;
  stats["logMessages"] = logMessageCounter;
  
  String statsJson;
  serializeJson(stats, statsJson);
  mqttClient.publish(statsTopic.c_str(), statsJson.c_str());
}

void callback(char* topic, byte* payload, unsigned int length) {
  mqttMessageCount++;
  
  // Limit payload size to prevent buffer overflow
  if (length > 256) {
    logError("MQTT payload too large: " + String(length) + " bytes");
    return;
  }
  
  String strTopic(topic);
  String strPayload = "";
  
  // Convert payload to string
  for (int i = 0; i < length; i++) {
    strPayload += (char)payload[i];
  }
  
  // Sanitize payload
  strPayload = sanitizeInput(strPayload);
  
  // Log received MQTT message
  publishLog("Topic: " + strTopic + ", Payload: " + strPayload, "mqtt_received");
  
  if (debug == 1){
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.println(strPayload);
  }

  // Parse topic to extract shade/group ID and command type
  // Expected topics use configured prefix (default: somfy):
  // {mqtttopic}/shades/[shadeId]/direction/set
  // {mqtttopic}/shades/[shadeId]/target/set  
  // {mqtttopic}/shades/[shadeId]/tiltTarget/set
  // {mqtttopic}/shades/[shadeId]/position/set
  // {mqtttopic}/shades/[shadeId]/tiltPosition/set
  // {mqtttopic}/shades/[shadeId]/sunFlag/set
  // {mqtttopic}/shades/[shadeId]/sunny/set
  // {mqtttopic}/shades/[shadeId]/windy/set
  // {mqtttopic}/groups/[groupId]/direction/set
  // {mqtttopic}/groups/[groupId]/sunFlag/set
  // {mqtttopic}/groups/[groupId]/sunny/set
  // {mqtttopic}/groups/[groupId]/windy/set
  
  // Verify topic starts with our configured prefix
  String expectedPrefix = String(mqtttopic) + "/";
  if (!strTopic.startsWith(expectedPrefix)) {
    if (debug == 1) {
      Serial.println("Topic doesn't match configured prefix: " + expectedPrefix);
    }
    return;
  }

  if (strTopic.indexOf("/shades/") >= 0) {
    // Extract shade ID
    int shadeStart = strTopic.indexOf("/shades/") + 8;
    int shadeEnd = strTopic.indexOf("/", shadeStart);
    int shadeId = strTopic.substring(shadeStart, shadeEnd).toInt();
    
    if (strTopic.endsWith("/direction/set")) {
      int direction = strPayload.toInt();
      String command;
      if (direction == -1) command = "up";
      else if (direction == 1) command = "down";
      else command = "stop";
      sendShadeCommand(shadeId, command);
    }
    else if (strTopic.endsWith("/target/set")) {
      int target = strPayload.toInt();
      sendShadeCommand(shadeId, "", target);
    }
    else if (strTopic.endsWith("/tiltTarget/set")) {
      int target = strPayload.toInt();
      sendTiltCommand(shadeId, "", target);
    }
    else if (strTopic.endsWith("/position/set")) {
      int position = strPayload.toInt();
      setShadePosition(shadeId, position);
    }
    else if (strTopic.endsWith("/tiltPosition/set")) {
      int tiltPosition = strPayload.toInt();
      setShadePosition(shadeId, -1, tiltPosition);
    }
    else if (strTopic.endsWith("/sunFlag/set")) {
      int sunFlag = strPayload.toInt();
      String command = (sunFlag == 1) ? "sunflag" : "flag";
      sendShadeCommand(shadeId, command);
    }
    else if (strTopic.endsWith("/sunny/set")) {
      int sunny = strPayload.toInt();
      setSensorStatus(shadeId, -1, sunny, -1);
    }
    else if (strTopic.endsWith("/windy/set")) {
      int windy = strPayload.toInt();
      setSensorStatus(shadeId, -1, -1, windy);
    }
  }
  else if (strTopic.indexOf("/groups/") >= 0) {
    // Extract group ID
    int groupStart = strTopic.indexOf("/groups/") + 8;
    int groupEnd = strTopic.indexOf("/", groupStart);
    int groupId = strTopic.substring(groupStart, groupEnd).toInt();
    
    if (strTopic.endsWith("/direction/set")) {
      int direction = strPayload.toInt();
      String command;
      if (direction == -1) command = "up";
      else if (direction == 1) command = "down";
      else command = "stop";
      sendGroupCommand(groupId, command);
    }
    else if (strTopic.endsWith("/sunny/set")) {
      int sunny = strPayload.toInt();
      setSensorStatus(-1, groupId, sunny, -1);
    }
    else if (strTopic.endsWith("/windy/set")) {
      int windy = strPayload.toInt();
      setSensorStatus(-1, groupId, -1, windy);
    }
  }
  else if (strTopic.indexOf("/bridge/") >= 0) {
    // Handle bridge control commands
    if (strTopic.endsWith("/debug/set")) {
      debug = strPayload.toInt();
      if (debug == 1) {
        Serial.println("Debug mode enabled via MQTT");
      }
      
      // Publish confirmation
      String responseTopic = String(mqtttopic) + "/bridge/debug";
      getMQTTClient().publish(responseTopic.c_str(), strPayload.c_str());
    }
    else if (strTopic.endsWith("/logging/set")) {
      mqttLogging = strPayload.toInt();
      String logStatus = (mqttLogging == 1) ? "enabled" : "disabled";
      
      if (debug == 1) {
        Serial.println("MQTT logging " + logStatus + " via MQTT");
      }
      
      // Publish confirmation
      String responseTopic = String(mqtttopic) + "/bridge/logging";
      getMQTTClient().publish(responseTopic.c_str(), logStatus.c_str());
      
      // Log the logging state change
      if (mqttLogging == 1) {
        publishLog("MQTT logging enabled", "system");
      }
    }
  }
}

void reconnect() {
  static unsigned long lastAttempt = 0;
  static int failedAttempts = 0;
  
  // Limit reconnection frequency - minimum 5 seconds between attempts
  if (millis() - lastAttempt < 5000) {
    return;
  }
  
  lastAttempt = millis();
  
  // Check available memory before attempting connection
  uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 8192) { // Less than 8KB free
    if (debug == 1) {
      Serial.println("Low memory (" + String(freeHeap) + " bytes), skipping MQTT attempt");
    }
    return;
  }
  
  // Get the appropriate MQTT client
  PubSubClient& mqttClient = getMQTTClient();
  
  // Configure SSL client if using MQTTS with better error handling
  if (useMQTTS) {
    try {
      ESPclientSecure.setInsecure(); // Skip certificate validation
      ESPclientSecure.setBufferSizes(1024, 1024); // Reduce buffer sizes to save memory
      // Force garbage collection
      yield();
    } catch (...) {
      if (debug == 1) {
        Serial.println("SSL configuration failed");
      }
      return;
    }
  }
  
  // Check if already connected
  if (mqttClient.connected()) {
    return;
  }
  
  failedAttempts++;
  
  // Auto-disable SSL after 5 failed attempts to prevent crashes
  if (failedAttempts == 5 && useMQTTS) {
    if (debug == 1) {
      Serial.println("SSL connection failing, switching to non-SSL MQTT");
    }
    useMQTTS = false;
    // Reconfigure client
    client.setServer(mqtt, atoi(mqttport));
    client.setCallback(callback);
    publishLog("Switched to non-SSL MQTT due to connection failures", "WARN");
  }
  
  // Reset after too many failures
  if (failedAttempts > 20) {
    publishLog("MQTT connection failed 20 times, restarting ESP...", "ERROR");
    delay(1000);
    ESP.restart();
  }
  
  // Feed watchdog
  yield();
  
  if (debug == 1){
    Serial.print("Attempting MQTT");
    Serial.print(useMQTTS ? "S" : "");
    Serial.print(" connection (");
    Serial.print(failedAttempts);
    Serial.print("/20)...");
  }
  
  String clientId = "ESPSomfy-MQTT-Bridge-" + String(idx);
 
  // Set last will and testament
  String lwt = String(mqtttopic) + "/bridge/status";
  
  // Attempt to connect with LWT and authentication
  bool connected = false;
  if (strlen(mqttuser) > 0 && strlen(mqttpass) > 0) {
    // Connect with username and password
    connected = mqttClient.connect(clientId.c_str(), mqttuser, mqttpass, lwt.c_str(), 1, true, "offline");
  } else {
    // Connect without authentication
    connected = mqttClient.connect(clientId.c_str(), lwt.c_str(), 1, true, "offline");
  }
  
  // Feed watchdog
  yield();
  
  if (connected) {
    if (debug == 1){
      Serial.println("connected");
    }
    
    // Reset failed attempts counter
    failedAttempts = 0;
    
    // Once connected, publish status and authenticate ESPSomfy if needed
    String statusTopic = String(mqtttopic) + "/bridge/status";
    mqttClient.publish(statusTopic.c_str(), "online", true);
    
    // Feed watchdog
    yield();
    
    // Try to authenticate with ESPSomfy-RTS
    if (!authenticateESPSomfy()) {
      publishLog("ESPSomfy authentication failed, continuing without auth", "WARN");
    }
    
    // Subscribe to all relevant topics using configured prefix
    String topicPrefix = String(mqtttopic);
    
    // Shade topics
    mqttClient.subscribe((topicPrefix + "/shades/+/direction/set").c_str());
    mqttClient.subscribe((topicPrefix + "/shades/+/target/set").c_str());
    mqttClient.subscribe((topicPrefix + "/shades/+/tiltTarget/set").c_str());
    mqttClient.subscribe((topicPrefix + "/shades/+/position/set").c_str());
    mqttClient.subscribe((topicPrefix + "/shades/+/tiltPosition/set").c_str());
    mqttClient.subscribe((topicPrefix + "/shades/+/sunFlag/set").c_str());
    mqttClient.subscribe((topicPrefix + "/shades/+/sunny/set").c_str());
    mqttClient.subscribe((topicPrefix + "/shades/+/windy/set").c_str());
    
    // Feed watchdog
    yield();
    
    // Group topics
    mqttClient.subscribe((topicPrefix + "/groups/+/direction/set").c_str());
    mqttClient.subscribe((topicPrefix + "/groups/+/sunFlag/set").c_str());
    mqttClient.subscribe((topicPrefix + "/groups/+/sunny/set").c_str());
    mqttClient.subscribe((topicPrefix + "/groups/+/windy/set").c_str());
    
    // Bridge control topics
    mqttClient.subscribe((topicPrefix + "/bridge/debug/set").c_str());
    mqttClient.subscribe((topicPrefix + "/bridge/logging/set").c_str());
    
    // Feed watchdog
    yield();
    
    if (debug == 1){
      Serial.println("MQTT subscriptions completed");
    }
    
    publishLog("MQTT connection established successfully", "INFO");
    
  } else {
    if (debug == 1){
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.print(" - will retry in 5 seconds");
      Serial.println("");
    }
    
    String errorMsg = "MQTT connection failed, state: ";
    errorMsg += mqttClient.state();
    publishLog(errorMsg, "ERROR");
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Wifi Setting management
////////////////////////////////////////////////////////////////////////////////////////////////

// Generate the server page
void D_AP_SER_Page() {
  int Tnetwork=0,i=0,len=0;
  String st="",s="";
  Tnetwork = WiFi.scanNetworks(); // Scan for total networks available
  st = "<select name='ssid' required>";
  for (int i = 0; i < Tnetwork; ++i)
  {
    // Add ssid to the combobox
    st += "<option value='"+WiFi.SSID(i)+"'>"+WiFi.SSID(i)+"</option>";
  }
  st += "</select>";
  IPAddress ip = WiFi.softAPIP(); // Get ESP8266 IP Adress
  
  // Modern HTML with CSS and JavaScript validation
  s = "<!DOCTYPE HTML><html><head><title>ESPSomfy MQTT-to-REST Bridge</title>";
  s += "<style>";
  s += "body { font-family: Arial, sans-serif; margin: 40px; background-color: #f5f5f5; }";
  s += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  s += "h1 { color: #333; text-align: center; }";
  s += ".form-group { margin-bottom: 15px; }";
  s += "label { display: block; margin-bottom: 5px; font-weight: bold; color: #555; }";
  s += "input, select { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }";
  s += "input:focus, select:focus { border-color: #007bff; outline: none; }";
  s += ".submit-btn { background-color: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; width: 100%; }";
  s += ".submit-btn:hover { background-color: #0056b3; }";
  s += ".advanced { border-top: 1px solid #eee; padding-top: 15px; margin-top: 15px; }";
  s += ".advanced-toggle { cursor: pointer; color: #007bff; }";
  s += ".advanced-content { display: none; }";
  s += ".help-text { font-size: 12px; color: #666; margin-top: 2px; }";
  s += "</style>";
  s += "<script>";
  s += "function toggleAdvanced() {";
  s += "  var content = document.getElementById('advanced-content');";
  s += "  var toggle = document.getElementById('advanced-toggle');";
  s += "  if (content.style.display === 'none' || content.style.display === '') {";
  s += "    content.style.display = 'block';";
  s += "    toggle.innerHTML = '- Advanced Settings';";
  s += "  } else {";
  s += "    content.style.display = 'none';";
  s += "    toggle.innerHTML = '+ Advanced Settings';";
  s += "  }";
  s += "}";
  s += "function validateForm() {";
  s += "  var mqttServer = document.getElementById('mqtt').value;";
  s += "  var mqttPort = document.getElementById('mqttport').value;";
  s += "  if (mqttServer && !mqttPort) {";
  s += "    alert('Please specify MQTT port when MQTT server is provided');";
  s += "    return false;";
  s += "  }";
  s += "  if (mqttPort && (mqttPort < 1 || mqttPort > 65535)) {";
  s += "    alert('MQTT port must be between 1 and 65535');";
  s += "    return false;";
  s += "  }";
  s += "  return true;";
  s += "}";
  s += "</script></head><body>";
  s += "<div class='container'>";
  s += "<h1>ESPSomfy MQTT-to-REST Bridge</h1>";
  s += "<form method='get' action='a' onsubmit='return validateForm()'>";
  s += "<div class='form-group'>";
  s += "<label for='ssid'>WiFi Network:</label>";
  s += st;
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<label for='pass'>WiFi Password:</label>";
  s += "<input type='password' id='pass' name='pass' maxlength='63'>";
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<label for='mqtt'>MQTT Server:</label>";
  s += "<input type='text' id='mqtt' name='mqtt' maxlength='99' placeholder='192.168.1.100'>";
  s += "<div class='help-text'>IP address or hostname of your MQTT broker</div>";
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<label for='mqttport'>MQTT Port:</label>";
  s += "<input type='number' id='mqttport' name='mqttport' min='1' max='65535' placeholder='1883'>";
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<label for='mqttuser'>MQTT Username (optional):</label>";
  s += "<input type='text' id='mqttuser' name='mqttuser' maxlength='63' placeholder='mqtt_user'>";
  s += "<div class='help-text'>Leave empty if no authentication required</div>";
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<label for='mqttpass'>MQTT Password (optional):</label>";
  s += "<input type='password' id='mqttpass' name='mqttpass' maxlength='63' placeholder='mqtt_password'>";
  s += "<div class='help-text'>Leave empty if no authentication required</div>";
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<label for='mqtts'>";
  s += "<input type='checkbox' id='mqtts' name='mqtts' value='1'> Use MQTTS (SSL/TLS)";
  s += "</label>";
  s += "<div class='help-text'>Check this box to use secure MQTT connection (typically port 8883)</div>";
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<label for='idx'>Bridge ID:</label>";
  s += "<input type='text' id='idx' name='idx' maxlength='9' placeholder='bridge1' required>";
  s += "<div class='help-text'>Unique identifier for this bridge</div>";
  s += "</div>";
  s += "<div class='advanced'>";
  s += "<div id='advanced-toggle' class='advanced-toggle' onclick='toggleAdvanced()'>+ Advanced Settings</div>";
  s += "<div id='advanced-content' class='advanced-content'>";
  s += "<div class='form-group'>";
  s += "<label for='espsomfyhost'>ESPSomfy-RTS Host:</label>";
  s += "<input type='text' id='espsomfyhost' name='espsomfyhost' maxlength='63' placeholder='espsomfyrts'>";
  s += "<div class='help-text'>Hostname or IP of your ESPSomfy-RTS device</div>";
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<label for='espsomfyport'>ESPSomfy-RTS Port:</label>";
  s += "<input type='number' id='espsomfyport' name='espsomfyport' min='1' max='65535' placeholder='80'>";
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<label for='mqtttopic'>MQTT Topic Prefix:</label>";
  s += "<input type='text' id='mqtttopic' name='mqtttopic' maxlength='31' placeholder='root'>";
  s += "<div class='help-text'>Prefix for MQTT topics (e.g., root/shades/1/direction/set)</div>";
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<label for='apikey'>ESPSomfy API Key (optional):</label>";
  s += "<input type='text' id='apikey' name='apikey' maxlength='64'>";
  s += "<div class='help-text'>Leave empty if no authentication required</div>";
  s += "</div>";
  s += "</div>";
  s += "</div>";
  s += "<div class='form-group'>";
  s += "<input type='submit' class='submit-btn' value='Save Configuration'>";
  s += "</div>";
  s += "</form>";
  s += "</div>";
  s += "</body></html>";
  
  server.send(200, "text/html", s);
}
// Process reply 
void Get_Req(){
  if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("idx")){  
    // Sanitize and validate inputs
    String sanitizedSSID = sanitizeInput(server.arg("ssid"));
    String sanitizedPass = sanitizeInput(server.arg("pass"));
    String sanitizedMqtt = sanitizeInput(server.arg("mqtt"));
    String sanitizedIdx = sanitizeInput(server.arg("idx"));
    String sanitizedESPSomfyHost = sanitizeInput(server.arg("espsomfyhost"));
    
    // Basic validation
    if (sanitizedSSID.length() > 32 || sanitizedPass.length() > 63 || 
        sanitizedMqtt.length() > 99 || sanitizedIdx.length() > 9) {
      server.send(400, "text/html", "Input validation failed: Values too long");
      return;
    }
    
    strcpy(ssid, sanitizedSSID.c_str());
    strcpy(pass, sanitizedPass.c_str());
    strcpy(mqtt, sanitizedMqtt.c_str());
    strcpy(mqttport, server.arg("mqttport").c_str());
    strcpy(mqttuser, sanitizeInput(server.arg("mqttuser")).c_str());
    strcpy(mqttpass, sanitizeInput(server.arg("mqttpass")).c_str());
    strcpy(idx, sanitizedIdx.c_str());
    strcpy(espsomfyhost, sanitizedESPSomfyHost.c_str());
    strcpy(espsomfyport, server.arg("espsomfyport").c_str());
    strcpy(mqtttopic, sanitizeInput(server.arg("mqtttopic")).c_str());
    strcpy(apikey, sanitizeInput(server.arg("apikey")).c_str());
    
    // Handle MQTTS checkbox
    useMQTTS = server.hasArg("mqtts") ? 1 : 0;
    
    // Set defaults for empty values
    if (strlen(espsomfyhost) == 0) {
      strcpy(espsomfyhost, "espsomfyrts");
    }
    if (strlen(espsomfyport) == 0) {
      strcpy(espsomfyport, "80");
    }
    if (strlen(mqtttopic) == 0) {
      strcpy(mqtttopic, "root");
    }
  }
  
  // Write parameters in eeprom
  setParams(ssid,pass,mqtt,mqttport,mqttuser,mqttpass,idx,espsomfyhost,espsomfyport,mqtttopic,apikey,useMQTTS);
  
  String s = R"(
<!DOCTYPE HTML>
<html>
<head>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; margin: 50px; }
        .success { color: green; }
        .countdown { font-size: 20px; margin: 20px; }
    </style>
    <script>
        var countdown = 5;
        function updateCountdown() {
            document.getElementById('countdown').innerHTML = countdown;
            countdown--;
            if (countdown < 0) {
                document.getElementById('message').innerHTML = 'Rebooting now...';
            } else {
                setTimeout(updateCountdown, 1000);
            }
        }
        window.onload = updateCountdown;
    </script>
</head>
<body>
    <h1>Configuration Saved</h1>
    <p class="success">Settings have been saved successfully!</p>
    <p>ESP8266 will reboot in <span id="countdown" class="countdown">5</span> seconds...</p>
    <p id="message">Please reconnect to your WiFi network after reboot.</p>
</body>
</html>
)";
  
  server.send(200,"text/html",s);
  delay(1000); // Give time for response to be sent
  if (debug == 1){
    Serial.println("Configuration saved, rebooting ESP");
  }
  ESP.restart();
}

void setup() {
  // Optimize memory usage and prevent stack overflow
  ESP.wdtDisable(); // Disable watchdog temporarily
  
  startServer = 1;
  revision[0] = '\0';
  ssid[0] = '\0';
  pass[0] = '\0';
  mqtt[0] = '\0';
  mqttport[0] = '\0';
  mqttuser[0] = '\0';
  mqttpass[0] = '\0';
  idx[0] = '\0';
  espsomfyhost[0] = '\0';

  delay(200); //Stable Wifi
  Serial.begin(9600); //Set Baud Rate 
  EEPROM.begin(512);
  
  // Re-enable watchdog with longer timeout
  ESP.wdtEnable(8000); // 8 second timeout
  
  if (debug == 1){
    Serial.println("ESPSomfy MQTT-to-REST Bridge starting...");
    Serial.println("Firmware version: " + String(REV));
    Serial.println("Free heap: " + String(ESP.getFreeHeap()));
    dumpEEPROM();
  }
  
  // Reading EEProm parameters
  if(getParams(revision,ssid,pass,mqtt,mqttport,mqttuser,mqttpass,idx,espsomfyhost,espsomfyport,mqtttopic,apikey,&useMQTTS) !=0 ){
    // Config mode
    startServer = 0;
    if (debug == 1){
      Serial.println("No valid configuration found, entering setup mode");
    }
  }
  else{
    WiFi.mode(WIFI_STA);
    if (debug == 1){
      Serial.println("Stored settings:");
      Serial.println("SSID: " + String(ssid));
      Serial.println("MQTT Server: " + String(mqtt) + ":" + String(mqttport));  
      Serial.println("MQTT Topic Prefix: " + String(mqtttopic));
      Serial.println("Bridge ID: " + String(idx));
      Serial.println("ESPSomfy Host: " + String(espsomfyhost) + ":" + String(espsomfyport));
      Serial.println("API Key configured: " + String(strlen(apikey) > 0 ? "Yes" : "No"));
    }

    // Set defaults for empty values
    if (strlen(espsomfyhost) == 0) {
      strcpy(espsomfyhost, "espsomfyrts");
    }
    if (strlen(espsomfyport) == 0) {
      strcpy(espsomfyport, "80");
    }
    if (strlen(mqtttopic) == 0) {
      strcpy(mqtttopic, "root");
    }

    // Connect to WiFi with timeout
    WiFi.begin(ssid,pass);
    int wifiTimeout = 30; // 30 second timeout
    while (WiFi.status() != WL_CONNECTED && wifiTimeout > 0) {
      delay(1000);
      wifiTimeout--;
      if (debug == 1){
        Serial.print(".");
      }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      if (debug == 1){
        Serial.println("");
        Serial.println("WiFi connected");  
        Serial.println("IP address: " + WiFi.localIP().toString());
        Serial.println("Signal strength: " + String(WiFi.RSSI()) + " dBm");
      }
      
      // Start mDNS service for local domain name resolution
      String hostname = "espsomfyMQTT";
      if (strlen(idx) > 0) {
        hostname += "-" + String(idx);
      }
      
      if (MDNS.begin(hostname)) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("mqtt", "tcp", atoi(mqttport));
        if (debug == 1) {
          Serial.println("mDNS started: http://" + hostname + ".local");
        }
      } else {
        if (debug == 1) {
          Serial.println("mDNS failed to start");
        }
      }
      
      // Connect mqtt server only if configured
      if (strlen(mqtt) > 0 && strlen(mqttport) > 0) {
        // Check memory before configuring MQTT
        uint32_t freeHeap = ESP.getFreeHeap();
        if (debug == 1) {
          Serial.println("Free heap before MQTT setup: " + String(freeHeap) + " bytes");
        }
        
        if (freeHeap < 10240) { // Less than 10KB free
          if (debug == 1) {
            Serial.println("Warning: Low memory, disabling SSL to prevent crashes");
          }
          useMQTTS = false; // Force disable SSL if low memory
        }
        
        if (useMQTTS) {
          if (debug == 1) {
            Serial.println("Configuring MQTTS (SSL) connection...");
          }
          try {
            clientSecure.setServer(mqtt, atoi(mqttport));
            clientSecure.setCallback(callback);
            if (debug == 1) {
              Serial.println("MQTTS configured successfully");
            }
          } catch (...) {
            if (debug == 1) {
              Serial.println("MQTTS configuration failed, falling back to MQTT");
            }
            useMQTTS = false;
            client.setServer(mqtt, atoi(mqttport));
            client.setCallback(callback);
          }
        } else {
          if (debug == 1) {
            Serial.println("Configuring MQTT (non-SSL) connection...");
          }
          client.setServer(mqtt, atoi(mqttport));
          client.setCallback(callback);
        }
        networkConnected = 1;
      } else {
        if (debug == 1){
          Serial.println("MQTT not configured, running in HTTP-only mode");
        }
      }

      WiFi.hostname("ESPSomfy-MQTT-Bridge");
    } else {
      if (debug == 1){
        Serial.println("WiFi connection failed, entering setup mode");
      }
      startServer = 0;
    }
    
    // Start the web server
    server.begin();
    
    // Enhanced web endpoints
    server.on("/", []() {
      // Create modern homepage with status information
      String s = "<!DOCTYPE HTML><html><head><title>ESPSomfy MQTT Bridge</title>";
      s += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
      s += "<meta charset='UTF-8'>";
      s += "<style>";
      s += "body{font-family:Arial,sans-serif;margin:0;padding:20px;background:#667eea;color:white;}";
      s += ".container{max-width:800px;margin:0 auto;}";
      s += ".header{text-align:center;margin-bottom:20px;}";
      s += ".cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin-bottom:20px;}";
      s += ".card{background:white;color:#333;border-radius:8px;padding:15px;box-shadow:0 4px 8px rgba(0,0,0,0.1);}";
      s += ".card-title{font-weight:bold;margin-bottom:10px;}";
      s += ".status-ok{color:#28a745;}";
      s += ".status-error{color:#dc3545;}";
      s += ".status-warning{color:#ffc107;}";
      s += ".nav{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;}";
      s += ".btn{display:block;background:white;color:#333;padding:10px;border-radius:4px;text-decoration:none;text-align:center;font-weight:bold;}";
      s += ".btn:hover{background:#f0f0f0;}";
      s += ".btn-primary{background:#007bff;color:white;}";
      s += ".btn-danger{background:#dc3545;color:white;}";
      s += "</style></head><body>";
      
      s += "<div class='container'>";
      s += "<div class='header'>";
      s += "<h1>ESPSomfy MQTT Bridge</h1>";
      s += "<p>Version " + String(REV) + "</p>";
      s += "</div>";
      s += "<div class='cards'>";
      
      // WiFi Status Card
      s += "<div class='card'>";
      s += "<div class='card-title'>WiFi</div>";
      if (WiFi.status() == WL_CONNECTED) {
        s += "<div class='status-ok'>Connecte</div>";
        s += "<small>IP: " + WiFi.localIP().toString() + "<br>";
        
        // Show mDNS hostname if available
        String hostname = "espsomfyMQTT";
        if (strlen(idx) > 0) {
          hostname += "-" + String(idx);
        }
        s += "mDNS: " + hostname + ".local</small>";
      } else {
        s += "<div class='status-error'>Deconnecte</div>";
      }
      s += "</div>";
      
      // MQTT Status Card  
      s += "<div class='card'>";
      s += "<div class='card-title'>MQTT</div>";
      if (networkConnected && getMQTTClient().connected()) {
        s += "<div class='status-ok'>Connecte</div>";
        s += "<small>" + String(mqtt) + ":" + String(mqttport);
        if (useMQTTS) s += " (SSL)";
        s += "</small>";
      } else if (networkConnected) {
        s += "<div class='status-warning'>Deconnecte</div>";
      } else {
        s += "<div class='status-error'>Non configure</div>";
      }
      s += "</div>";
      
      // ESPSomfy Status Card
      s += "<div class='card'>";
      s += "<div class='card-title'>ESPSomfy</div>";
      if (strlen(espsomfyhost) > 0) {
        s += "<div class='status-ok'>Configure</div>";
        s += "<small>" + String(espsomfyhost) + ":" + String(espsomfyport) + "</small>";
      } else {
        s += "<div class='status-warning'>Non configure</div>";
      }
      s += "</div>";
      
      // System Info Card
      s += "<div class='card'>";
      s += "<div class='card-title'>Systeme</div>";
      s += "<div>RAM: " + String(ESP.getFreeHeap() / 1024) + " KB</div>";
      s += "<small>Uptime: " + String(millis() / 60000) + " min</small>";
      s += "</div>";
      s += "</div>"; // End cards
      
      // Navigation
      s += "<div class='nav'>";
      s += "<a href='/status' class='btn'>Status</a>";
      s += "<a href='/stats' class='btn'>Stats</a>";
      s += "<a href='/mqttconfig' class='btn btn-primary'>MQTT</a>";
      s += "<a href='/debug' class='btn'>Debug</a>";
      s += "<a href='/reset' class='btn btn-danger' onclick='return confirm(\"Reset?\")'>Reset</a>";
      s += "</div>";
      s += "</div></body></html>";
      server.send(200, "text/html", s);
    });
    
    server.on("/status", []() {
      DynamicJsonDocument status(1024);
      status["firmware"] = REV;
      status["uptime"] = millis();
      status["freeHeap"] = ESP.getFreeHeap();
      status["wifiConnected"] = WiFi.status() == WL_CONNECTED;
      status["wifiIP"] = WiFi.localIP().toString();
      status["wifiRSSI"] = WiFi.RSSI();
      status["mqttConnected"] = getMQTTClient().connected();
      status["mqttServer"] = String(mqtt) + ":" + String(mqttport);
      status["mqttSSL"] = useMQTTS;
      status["mqttAuth"] = (strlen(mqttuser) > 0 && strlen(mqttpass) > 0);
      status["espsomfyHost"] = String(espsomfyhost) + ":" + String(espsomfyport);
      status["topicPrefix"] = String(mqtttopic);
      status["lastError"] = lastError;
      status["lastErrorTime"] = lastErrorTime;
      status["mqttLogging"] = mqttLogging;
      status["logMessages"] = logMessageCounter;
      
      String statusJson;
      serializeJson(status, statusJson);
      server.send(200, "application/json", statusJson);
    });
    
    server.on("/stats", []() {
      DynamicJsonDocument stats(512);
      stats["httpSuccess"] = httpSuccessCount;
      stats["httpFails"] = httpFailCount;
      stats["mqttMessages"] = mqttMessageCount;
      stats["uptime"] = millis();
      
      String statsJson;
      serializeJson(stats, statsJson);
      server.send(200, "application/json", statsJson);
    });
    
    server.on("/debug", []() {
      if (server.hasArg("enable")) {
        debug = server.arg("enable").toInt();
        server.send(200, "text/plain", "Debug mode: " + String(debug ? "enabled" : "disabled"));
      } else {
        server.send(200, "text/html", 
          "<h1>Debug Control</h1>"
          "<p>Current: " + String(debug ? "enabled" : "disabled") + "</p>"
          "<p><a href='/debug?enable=1'>Enable</a> | <a href='/debug?enable=0'>Disable</a></p>");
      }
    });
    
    server.on("/mqttconfig", []() {
      MQTT_Config_Page();
    });
    
    server.on("/mqttupdate", []() {
      Update_MQTT_Config();
    });
    
    server.on("/reset",resetSettings);
  }
}

void loop() {
  // Feed watchdog to prevent resets
  yield();
  
  if (startServer == 0){ 
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("ESPSomfy MQTT Bridge Setup");
    delay(100); //Stable AP
 
    dnsServer.start(DNS_PORT, "*", apIP);
    server.onNotFound(D_AP_SER_Page);
    server.on("/",D_AP_SER_Page); 
    server.on("/a",Get_Req); // If submit button is pressed get the new SSID and Password and store it in EEPROM 
    server.begin();
    startServer = 1;
    // captive network is active 
    captiveNetwork = 1;
    
    if (debug == 1){
      Serial.println("Setup mode active - Connect to 'ESPSomfy MQTT Bridge Setup' WiFi");
      Serial.println("Navigate to 192.168.1.1 to configure");
    }
  }
  
  // Handle captive portal DNS
  if (captiveNetwork == 1){
    dnsServer.processNextRequest();
    yield(); // Feed watchdog
  }
  
  // Handle web server requests
  server.handleClient();
  yield(); // Feed watchdog
  
  // Update mDNS if connected to WiFi
  if (WiFi.status() == WL_CONNECTED && captiveNetwork == 0) {
    MDNS.update();
  }
  
  // Handle MQTT if connected
  if(networkConnected == 1){
    PubSubClient& mqttClient = getMQTTClient();
    if (!mqttClient.connected()) {
      reconnect();
    } else {
      mqttClient.loop();
      yield(); // Feed watchdog
      
      // Publish heartbeat periodically
      unsigned long now = millis();
      if (now - lastHeartbeat > HEARTBEAT_INTERVAL) {
        publishHeartbeat();
        lastHeartbeat = now;
      }
    }
  }
  
  // Small delay to prevent tight loop
  delay(10);
  yield(); // Feed watchdog one more time
}