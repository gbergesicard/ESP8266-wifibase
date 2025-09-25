#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <EEPROM.h>
#include <string>
#include <DNSServer.h>
#include <ArduinoJson.h>

#define REV "REV0001"

const char *ssidAP = "ConfigureDevice"; //Ap SSID
const char *passwordAP = "";  //Ap Password
char revision[10]; //Read revision 
char ssid[33];     //Read SSID From Web Page
char pass[64];     //Read Password From Web Page
char mqtt[100];    //Read mqtt server From Web Page
char mqttport[6];  //Read mqtt port From Web Page
char idx[10];      //Read idx From Web Page
char espsomfyhost[64]; //ESPSomfy-RTS hostname
char espsomfyport[6] = "80"; //ESPSomfy-RTS port
char mqtttopic[32] = "root"; //MQTT topic prefix
char apikey[65] = ""; //ESPSomfy-RTS API key
int startServer = 0;
int debug = 1;
int useHttps = 0; //Use HTTPS for ESPSomfy calls
int retryAttempts = 3; //HTTP retry attempts
int mqttRetryInterval = 5000; //MQTT retry interval in ms
ESP8266WebServer server(80);//Specify port 
WiFiClient ESPclient;
HTTPClient http;

// for the captive network
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
int captiveNetwork = 0;

// for mqtt
int networkConnected = 0;
PubSubClient client(ESPclient);

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

int getParams(char* revision,char* ssid,char* pass,char* mqtt,char* mqttPort,char* idx,char* espsomfyhost,char* espsomfyport,char* mqtttopic,char* apikey){
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
  readParam(idx,&i);
  readParam(espsomfyhost,&i);
  readParam(espsomfyport,&i);
  readParam(mqtttopic,&i);
  readParam(apikey,&i);
  return 0;
}

void writeParam(char* value,int* index){
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
void setParams(char* ssid,char* pass,char* mqtt,char* mqttPort,char* idx,char* espsomfyhost,char* espsomfyport,char* mqtttopic,char* apikey){
  int i =0;
  
  ClearEeprom();//First Clear Eeprom
  delay(10);
  writeParam(REV,&i);
  writeParam(ssid,&i);
  writeParam(pass,&i);
  writeParam(mqtt,&i);
  writeParam(mqttPort,&i);
  writeParam(idx,&i);
  writeParam(espsomfyhost,&i);
  writeParam(espsomfyport,&i);
  writeParam(mqtttopic,&i);
  writeParam(apikey,&i);
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
  
  String response;
  bool success = makeHttpRequest(url, response);
  
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
  if (success && networkConnected && client.connected()) {
    String statusTopic = String(mqtttopic) + "/bridge/shades/" + String(shadeId) + "/status";
    client.publish(statusTopic.c_str(), success ? "OK" : "ERROR");
  }
}

void sendGroupCommand(int groupId, const String& command) {
  if (!isValidGroupId(groupId)) {
    logError("Invalid group ID: " + String(groupId));
    return;
  }
  
  String url = buildBaseUrl() + GROUP_COMMAND_ENDPOINT + "?groupId=" + String(groupId) + "&command=" + command;
  
  String response;
  bool success = makeHttpRequest(url, response);
  
  if (debug == 1) {
    Serial.print("Group command sent - ID: ");
    Serial.print(groupId);
    Serial.print(", Command: ");
    Serial.print(command);
    Serial.print(", Success: ");
    Serial.println(success ? "YES" : "NO");
  }
  
  // Publish response to MQTT if successful
  if (success && networkConnected && client.connected()) {
    String statusTopic = String(mqtttopic) + "/bridge/groups/" + String(groupId) + "/status";
    client.publish(statusTopic.c_str(), success ? "OK" : "ERROR");
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
  
  String response;
  bool success = makeHttpRequest(url, response);
  
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
  
  String response;
  bool success = makeHttpRequest(url, response);
  
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
  
  String response;
  bool success = makeHttpRequest(url, response);
  
  if (debug == 1) {
    Serial.print("Sensor status set - Success: ");
    Serial.println(success ? "YES" : "NO");
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////
// MQTT management
////////////////////////////////////////////////////////////////////////////////////////////////

void publishHeartbeat() {
  if (!networkConnected || !client.connected()) {
    return;
  }
  
  String statusTopic = String(mqtttopic) + "/bridge/status";
  String heartbeatTopic = String(mqtttopic) + "/bridge/heartbeat";
  String statsTopic = String(mqtttopic) + "/bridge/stats";
  
  client.publish(statusTopic.c_str(), "online", true); // Retained message
  client.publish(heartbeatTopic.c_str(), String(millis()).c_str());
  
  // Publish statistics
  DynamicJsonDocument stats(512);
  stats["httpSuccess"] = httpSuccessCount;
  stats["httpFails"] = httpFailCount;
  stats["mqttMessages"] = mqttMessageCount;
  stats["uptime"] = millis();
  stats["freeHeap"] = ESP.getFreeHeap();
  stats["lastError"] = lastError;
  stats["lastErrorTime"] = lastErrorTime;
  
  String statsJson;
  serializeJson(stats, statsJson);
  client.publish(statsTopic.c_str(), statsJson.c_str());
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
      client.publish(responseTopic.c_str(), strPayload.c_str());
    }
  }
}

void reconnect() {
  // Loop until we're reconnected
  int attemptCount = 0;
  while (!client.connected() && attemptCount < 10) { // Limit reconnection attempts
    attemptCount++;
    if (debug == 1){
      Serial.print("Attempting MQTT connection (");
      Serial.print(attemptCount);
      Serial.print("/10)...");
    }
    String clientId = "ESPSomfy-MQTT-Bridge-" + String(idx);
   
    // Set last will and testament
    String lwt = String(mqtttopic) + "/bridge/status";
    
    // Attempt to connect with LWT
    if (client.connect(clientId.c_str(), lwt.c_str(), 1, true, "offline")) {
      if (debug == 1){
        Serial.println("connected");
      }
      
      // Once connected, publish status and authenticate ESPSomfy if needed
      String statusTopic = String(mqtttopic) + "/bridge/status";
      client.publish(statusTopic.c_str(), "online", true);
      
      // Try to authenticate with ESPSomfy-RTS
      if (!authenticateESPSomfy()) {
        logError("ESPSomfy authentication failed, continuing without auth");
      }
      
      // Subscribe to all relevant topics using configured prefix
      String topicPrefix = String(mqtttopic);
      
      // Shade topics
      client.subscribe((topicPrefix + "/shades/+/direction/set").c_str());
      client.subscribe((topicPrefix + "/shades/+/target/set").c_str());
      client.subscribe((topicPrefix + "/shades/+/tiltTarget/set").c_str());
      client.subscribe((topicPrefix + "/shades/+/position/set").c_str());
      client.subscribe((topicPrefix + "/shades/+/tiltPosition/set").c_str());
      client.subscribe((topicPrefix + "/shades/+/sunFlag/set").c_str());
      client.subscribe((topicPrefix + "/shades/+/sunny/set").c_str());
      client.subscribe((topicPrefix + "/shades/+/windy/set").c_str());
      
      // Group topics
      client.subscribe((topicPrefix + "/groups/+/direction/set").c_str());
      client.subscribe((topicPrefix + "/groups/+/sunFlag/set").c_str());
      client.subscribe((topicPrefix + "/groups/+/sunny/set").c_str());
      client.subscribe((topicPrefix + "/groups/+/windy/set").c_str());
      
      // Bridge control topics
      client.subscribe((topicPrefix + "/bridge/debug/set").c_str());
      
      if (debug == 1){
        Serial.println("MQTT subscriptions completed");
      }
      break;
    } else {
      if (debug == 1){
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.print(" try again in ");
        Serial.print(mqttRetryInterval / 1000);
        Serial.println(" seconds");
      }
      // handle web server requests 
      server.handleClient();
      // Wait before retrying
      delay(mqttRetryInterval);
    }
  }
  
  if (!client.connected()) {
    logError("Failed to connect to MQTT after 10 attempts");
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
  s = R"(
<!DOCTYPE HTML>
<html>
<head>
    <title>ESPSomfy MQTT-to-REST Bridge</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; background-color: #f5f5f5; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: bold; color: #555; }
        input, select { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        input:focus, select:focus { border-color: #007bff; outline: none; }
        .submit-btn { background-color: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; width: 100%; }
        .submit-btn:hover { background-color: #0056b3; }
        .advanced { border-top: 1px solid #eee; padding-top: 15px; margin-top: 15px; }
        .advanced-toggle { cursor: pointer; color: #007bff; }
        .advanced-content { display: none; }
        .help-text { font-size: 12px; color: #666; margin-top: 2px; }
    </style>
    <script>
        function toggleAdvanced() {
            var content = document.getElementById('advanced-content');
            var toggle = document.getElementById('advanced-toggle');
            if (content.style.display === 'none' || content.style.display === '') {
                content.style.display = 'block';
                toggle.innerHTML = '▼ Advanced Settings';
            } else {
                content.style.display = 'none';
                toggle.innerHTML = '▶ Advanced Settings';
            }
        }
        function validateForm() {
            var mqttServer = document.getElementById('mqtt').value;
            var mqttPort = document.getElementById('mqttport').value;
            
            if (mqttServer && !mqttPort) {
                alert('Please specify MQTT port when MQTT server is provided');
                return false;
            }
            
            if (mqttPort && (mqttPort < 1 || mqttPort > 65535)) {
                alert('MQTT port must be between 1 and 65535');
                return false;
            }
            
            return true;
        }
    </script>
</head>
<body>
    <div class="container">
        <h1>ESPSomfy MQTT-to-REST Bridge</h1>
        <form method='get' action='a' onsubmit='return validateForm()'>
            <div class="form-group">
                <label for="ssid">WiFi Network:</label>
)";
  s += st;
  s += R"(
            </div>
            <div class="form-group">
                <label for="pass">WiFi Password:</label>
                <input type="password" id="pass" name="pass" maxlength="63">
            </div>
            <div class="form-group">
                <label for="mqtt">MQTT Server:</label>
                <input type="text" id="mqtt" name="mqtt" maxlength="99" placeholder="192.168.1.100">
                <div class="help-text">IP address or hostname of your MQTT broker</div>
            </div>
            <div class="form-group">
                <label for="mqttport">MQTT Port:</label>
                <input type="number" id="mqttport" name="mqttport" min="1" max="65535" placeholder="1883">
            </div>
            <div class="form-group">
                <label for="idx">Bridge ID:</label>
                <input type="text" id="idx" name="idx" maxlength="9" placeholder="bridge1" required>
                <div class="help-text">Unique identifier for this bridge</div>
            </div>
            
            <div class="advanced">
                <div id="advanced-toggle" class="advanced-toggle" onclick="toggleAdvanced()">▶ Advanced Settings</div>
                <div id="advanced-content" class="advanced-content">
                    <div class="form-group">
                        <label for="espsomfyhost">ESPSomfy-RTS Host:</label>
                        <input type="text" id="espsomfyhost" name="espsomfyhost" maxlength="63" placeholder="espsomfyrts">
                        <div class="help-text">Hostname or IP of your ESPSomfy-RTS device</div>
                    </div>
                    <div class="form-group">
                        <label for="espsomfyport">ESPSomfy-RTS Port:</label>
                        <input type="number" id="espsomfyport" name="espsomfyport" min="1" max="65535" placeholder="80">
                    </div>
                    <div class="form-group">
                        <label for="mqtttopic">MQTT Topic Prefix:</label>
                        <input type="text" id="mqtttopic" name="mqtttopic" maxlength="31" placeholder="root">
                        <div class="help-text">Prefix for MQTT topics (e.g., root/shades/1/direction/set)</div>
                    </div>
                    <div class="form-group">
                        <label for="apikey">ESPSomfy API Key (optional):</label>
                        <input type="text" id="apikey" name="apikey" maxlength="64">
                        <div class="help-text">Leave empty if no authentication required</div>
                    </div>
                </div>
            </div>
            
            <div class="form-group">
                <input type="submit" class="submit-btn" value="Save Configuration">
            </div>
        </form>
    </div>
</body>
</html>
)";
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
    strcpy(idx, sanitizedIdx.c_str());
    strcpy(espsomfyhost, sanitizedESPSomfyHost.c_str());
    strcpy(espsomfyport, server.arg("espsomfyport").c_str());
    strcpy(mqtttopic, sanitizeInput(server.arg("mqtttopic")).c_str());
    strcpy(apikey, sanitizeInput(server.arg("apikey")).c_str());
    
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
  setParams(ssid,pass,mqtt,mqttport,idx,espsomfyhost,espsomfyport,mqtttopic,apikey);
  
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
  startServer = 1;
  revision[0] = '\0';
  ssid[0] = '\0';
  pass[0] = '\0';
  mqtt[0] = '\0';
  mqttport[0] = '\0';
  idx[0] = '\0';
  espsomfyhost[0] = '\0';

  delay(200); //Stable Wifi
  Serial.begin(9600); //Set Baud Rate 
  EEPROM.begin(512);
  if (debug == 1){
    Serial.println("ESPSomfy MQTT-to-REST Bridge starting...");
    Serial.println("Firmware version: " + String(REV));
    Serial.println("Free heap: " + String(ESP.getFreeHeap()));
    dumpEEPROM();
  }
  
  // Reading EEProm parameters
  if(getParams(revision,ssid,pass,mqtt,mqttport,idx,espsomfyhost,espsomfyport,mqtttopic,apikey) !=0 ){
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
      
      // Connect mqtt server only if configured
      if (strlen(mqtt) > 0 && strlen(mqttport) > 0) {
        client.setServer(mqtt, atoi(mqttport));
        client.setCallback(callback);
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
      server.send(200, "text/html", "<h1>ESPSomfy MQTT Bridge</h1><p><a href='/status'>Status</a> | <a href='/stats'>Statistics</a> | <a href='/reset'>Reset</a></p>");
    });
    
    server.on("/status", []() {
      DynamicJsonDocument status(1024);
      status["firmware"] = REV;
      status["uptime"] = millis();
      status["freeHeap"] = ESP.getFreeHeap();
      status["wifiConnected"] = WiFi.status() == WL_CONNECTED;
      status["wifiIP"] = WiFi.localIP().toString();
      status["wifiRSSI"] = WiFi.RSSI();
      status["mqttConnected"] = client.connected();
      status["mqttServer"] = String(mqtt) + ":" + String(mqttport);
      status["espsomfyHost"] = String(espsomfyhost) + ":" + String(espsomfyport);
      status["topicPrefix"] = String(mqtttopic);
      status["lastError"] = lastError;
      status["lastErrorTime"] = lastErrorTime;
      
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
    
    server.on("/reset",resetSettings);
  }
}

void loop() {
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
  }
  
  // Handle web server requests
  server.handleClient();
  
  // Handle MQTT if connected
  if(networkConnected == 1){
    if (!client.connected()) {
      reconnect();
    } else {
      client.loop();
      
      // Publish heartbeat periodically
      unsigned long now = millis();
      if (now - lastHeartbeat > HEARTBEAT_INTERVAL) {
        publishHeartbeat();
        lastHeartbeat = now;
      }
    }
  }
  
  // Check for special MQTT commands (like debug toggle)
  if (networkConnected && client.connected()) {
    // Handle bridge control commands
    String debugTopic = String(mqtttopic) + "/bridge/debug/set";
    // This is handled in the callback function
  }
  
  // Small delay to prevent watchdog issues
  delay(50);
}