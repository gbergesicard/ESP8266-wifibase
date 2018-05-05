#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <string>
#include <DNSServer.h>

#define REV "REV0001"

const char *ssidAP = "ConfigureDevice"; //Ap SSID
const char *passwordAP = "";  //Ap Password
char revision[10]; //Read revision 
char ssid[33];     //Read SSID From Web Page
char pass[64];     //Read Password From Web Page
char mqtt[100];    //Read mqtt server From Web Page
char mqttport[6];  //Read mqtt port From Web Page
char idx[10];      //Read idx From Web Page
int startServer = 0;
int debug = 0;
ESP8266WebServer server(80);//Specify port 
WiFiClient client;

// for the captive network
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
int captiveNetwork = 0;

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

int getParams(char* revision,char* ssid,char* pass,char* mqtt,char* mqttPort,char* idx){
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
void setParams(char* ssid,char* pass,char* mqtt,char* mqttPort,char* idx){
  int i =0;
  
  ClearEeprom();//First Clear Eeprom
  delay(10);
  writeParam(REV,&i);
  writeParam(ssid,&i);
  writeParam(pass,&i);
  writeParam(mqtt,&i);
  writeParam(mqttPort,&i);
  writeParam(idx,&i);
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

void setOff(){
 Serial.write("\xa0\x01"); // OPEN RELAY
 Serial.write(0x00); // null terminates a string so it has to be sent on its own
 Serial.write(0xa1);
}

void setOn(){
 Serial.write("\xa0\x01\x01\xa2"); // CLOSE RELAY
}

// Generate the server page
void D_AP_SER_Page() {
  int Tnetwork=0,i=0,len=0;
  String st="",s="";
  Tnetwork = WiFi.scanNetworks(); // Scan for total networks available
  st = "<select name='ssid'>";
  for (int i = 0; i < Tnetwork; ++i)
  {
    // Add ssid to the combobox
    st += "<option value='"+WiFi.SSID(i)+"'>"+WiFi.SSID(i)+"</option>";
  }
  st += "</select>";
  IPAddress ip = WiFi.softAPIP(); // Get ESP8266 IP Adress
  // Generate the html setting page
  s = "\n\r\n<!DOCTYPE HTML>\r\n<html><h1>Device settings</h1> ";
  s += "<p>";
  s += "<form method='get' action='a'>"+st+"<label>Paswoord: </label><input name='pass' length=64><br><label>MQTT Server : </label><input name='mqtt' length=64><br><label>MQTT port : </label><input name='mqttport' length=6><br><label>Idx: </label><input name='idx' length=6><br><input type='submit'></form>";
  s += "</html>\r\n\r\n";
  server.send( 200 , "text/html", s);
}
// Process reply 
void Get_Req(){
  if (server.hasArg("ssid") && server.hasArg("pass")){  
    strcpy(ssid,server.arg("ssid").c_str());//Get SSID
    strcpy(pass,server.arg("pass").c_str());//Get Password
    strcpy(mqtt,server.arg("mqtt").c_str());
    strcpy(mqttport,server.arg("mqttport").c_str());
    strcpy(idx,server.arg("idx").c_str());
  }
  // Write parameters in eeprom
  setParams(ssid,pass,mqtt,mqttport,idx);
  String s = "\r\n\r\n<!DOCTYPE HTML>\r\n<html><h1>Device settings</h1> ";
  s += "<p>Settings Saved... Reset to boot into new wifi</html>\r\n\r\n";
  server.send(200,"text/html",s);
  if (debug == 1){
    Serial.println("Rebooting ESP");
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

  delay(200); //Stable Wifi
  Serial.begin(9600); //Set Baud Rate 
  EEPROM.begin(512);
  if (debug == 1){
    Serial.println("Configuring access point...");
    dumpEEPROM();
  }
  //pinMode(0,INPUT); //Go to AP Mode Button
  //pinMode(2,OUTPUT); //Go to Sta Mode Button
  // Reading EEProm SSID-Password
  if(getParams(revision,ssid,pass,mqtt,mqttport,idx) !=0 ){
    // Config mode
    startServer = 0;
  }
  else{
    WiFi.mode(WIFI_STA);
    if (debug == 1){
      Serial.println(ssid);
      Serial.println(pass);
      Serial.println(mqtt);  
      Serial.println(mqttport);
      Serial.println(idx);
    }

    WiFi.begin(ssid,pass);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      if (debug == 1){
        Serial.print(".");
      }
    }
    if (debug == 1){
      Serial.print(".");
      Serial.println("");
      Serial.println("WiFi connected");  
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
    }

    WiFi.hostname("SwitchWIFI");
    // Start the web server
    server.begin();
    // http://<ip address>/switch?state=on
    server.on("/switch", []() {
      String state=server.arg("state");
      if (state == "on") setOn();
      else if (state == "off") setOff();
      server.send(200, "text/plain", "switch is now " + state);
    });
    server.on("/reset",resetSettings);
  }
}

void loop() {
  if (startServer == 0){ 
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("Wifi device setup");
    delay(100); //Stable AP
 
    dnsServer.start(DNS_PORT, "*", apIP);
    server.onNotFound(D_AP_SER_Page);
    server.on("/",D_AP_SER_Page); 
    server.on("/a",Get_Req); // If submit button is pressed get the new SSID and Password and store it in EEPROM 
    server.begin();
    startServer = 1;
    // captive network is active 
    captiveNetwork = 1;
  }
  delay(300); 
  if (captiveNetwork == 1){
    dnsServer.processNextRequest();
  }
  server.handleClient();     
}
