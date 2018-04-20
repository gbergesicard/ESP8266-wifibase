#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <string>

#define REV "REV0001"

const char *ssidAP = "ConfigureDevice"; //Ap SSID
const char *passwordAP = "";  //Ap Password
String revision = "";       //Read revision 
String ssid = "";          //Read SSID From Web Page
String pass = "";          //Read Password From Web Page
String mqtt = "";           //Read mqtt server From Web Page
String mqttport = "";       //Read mqtt port From Web Page
String idx = "";            //Read idx From Web Page
int startServer = 0;
int debug = 0;
ESP8266WebServer server(80);//Specify port 
WiFiClient client;

void dumpEEPROM(){
  char output[10];
  Serial.println("dumpEEPROM start");
  for(int i =0;i<70;i++){
    snprintf(output,10,"%x ",EEPROM.read(i));
    Serial.print(output);
  }
  Serial.println("\ndumpEEPROM stop");
}

void readParam(String value,int* index){
  char currentChar = '0';
  value="";
  do{
    currentChar = char(EEPROM.read(*index));
    if(currentChar != '\0'){
      value += currentChar;
    }
    (*index)++;
  }while (currentChar != '\0');
  if (debug == 1){
    Serial.println("readParam ("+value+")");
  }

}
int readRevison(String value,int* index){
  char currentChar = '0';
  value="";
  do{
    currentChar = char(EEPROM.read(*index));
    if(currentChar != '\0'){
      value += currentChar;
    }
    if(*index == 2 && !(value[0] =='R' && value[1] =='E' && value[2] =='V')){
      if (debug == 1){
        Serial.println("Revision read : "+value);
      }
      return -1;
    }
    (*index)++;
  }while (currentChar != '\0');
  if (debug == 1){
    Serial.println("Revision read : "+value);
  }
  // Compare the revision
  if (strcmp(value.c_str(),REV) != 0){
    return -2;
  }
  return 0;
}

int getParams(String revision,String ssid,String pass,String mqtt,String mqttPort,String idx){
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

void writeParam(String value,int* index){
  if (debug == 1){
    Serial.print("writeParam "+value);
    Serial.print(" Index = ");
    Serial.println(*index);
  }
  for (int i = 0; i < value.length(); ++i)
  {
    EEPROM.write(*index, value[i]);
    (*index) ++;
  }
  EEPROM.write(*index, '\0');
  (*index)++;
}
void setParams(String ssid,String pass,String mqtt,String mqttPort,String idx){
  int i =0;
  String revision = REV;
  
  ClearEeprom();//First Clear Eeprom
  delay(10);
  writeParam(revision,&i);
  writeParam(ssid,&i);
  writeParam(pass,&i);
  writeParam(mqtt,&i);
  writeParam(mqttPort,&i);
  writeParam(idx,&i);
  EEPROM.commit();
}

void setup() {
  startServer = 1;
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
    char localSsid [50];
    char localPass [50];
    sprintf(localSsid,"%s",ssid.c_str());
    sprintf(localPass,"%s",pass.c_str());
    WiFi.begin(localSsid,localPass);
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
  }
}

void setOn(){
 Serial.write("\xa0\x01"); // OPEN RELAY
 Serial.write(0x00); // null terminates a string so it has to be sent on its own
 Serial.write(0xa1);
}

void setOff(){
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
    ssid=server.arg("ssid");//Get SSID
    pass=server.arg("pass");//Get Password
    mqtt=server.arg("mqtt");
    mqttport=server.arg("mqttport");
    idx=server.arg("idx");
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

void loop() {
  if (startServer == 0){ 
    WiFi.mode(WIFI_AP_STA); //Both in Station and Access Point Mode
    WiFi.softAP(ssidAP, passwordAP); // Access Point Mode
    delay(100); //Stable AP
    server.on("/",D_AP_SER_Page); 
    server.on("/a",Get_Req); // If submit button is pressed get the new SSID and Password and store it in EEPROM 
    server.begin();
    startServer = 1;  
  }
  delay(300); 
  server.handleClient();     
}
