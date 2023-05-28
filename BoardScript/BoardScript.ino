#include <ESP8266WiFi.h>
#include <WebSocketClient.h>
#include <ArduinoJson.h>
#include <Ticker.h>

#define boardId "54fdcf80-a2e9-11ed-a8fc-0242ac120002"
#define wifiLOGIN ""
#define wifiPWD ""
#define failedDelayTime 100000

#define led D8
#define photoRelay D7
#define motorEnable D2
#define motorSide D1
#define hollUp D4
#define hollDown D3
#define acp A0


struct ServerSettings {
  char host[128] = "151.248.116.208";
  char connectionPath[128] = "/connection/board";
  char managementPath[128] = "/management/board";
  uint16_t port = 8000;
} ss;

struct Types {
  String tBoolean = "Boolean";
  String tInteger = "Integer";
} types;

struct DeviceStructure {
  String deviceName = "Умная штора"; 
  String deviceDescription = ("Умная штора предназначена для применения в быту, выполняет функции открытия, закрытия шторы при участии датчиков, измеряющих освещение внутри и снаружи помещения и напряжение магнитного поля.");
  String sensors[5] = {"Мотор вверх", "Мотор вниз", "Автоматический режим", "Время между измерениями (мс)", "Светодиод"};
  String sensorsTypes[5] = {types.tBoolean, types.tBoolean, types.tBoolean, types.tInteger, types.tBoolean};
  int sensorsCount = 5;
} ds;

struct ClientElements {
  WiFiClient client;
  WebSocketClient connectionClient;
  WebSocketClient managementClient;
  StaticJsonDocument<1024> controlDeviceData;
  StaticJsonDocument<1024> boardIdentificationData;
  Ticker photoRelayMeasuresTicker;
  Ticker motorMovesTicker;
  Ticker motorSideTicker;
} ce;

struct AutomodeResources {
  bool isAutomodeEnabled = false;
  bool isMotorEnabled = false;
  bool isMotorSideUp = true; 

  unsigned long minimalBorder = 0;
  int delayTime = 10000;
  int buffSize = 3;
  int buffInd = 0;
  int buff[3];
} ar;

void getPhotoRelayMeasure() {
  if (!ar.isAutomodeEnabled) return;
  
  if (millis() < 10) ar.minimalBorder = 0;
  
  if (millis() - ar.minimalBorder >= ar.delayTime) {
    ar.minimalBorder = millis();

    int x, y;
    if (digitalRead(photoRelay) == LOW) {
      x = analogRead(acp);
      digitalWrite(photoRelay, HIGH);
    } else {
      x = analogRead(acp);
      digitalWrite(photoRelay, LOW);
    }
    y = analogRead(acp);
    ar.buff[ar.buffInd] = (x + y) / 2;
    Serial.println(ar.buff[ar.buffInd++]);

    if (ar.buffInd == 3) {
      ar.buffInd = 0;

      int middle = 0;
      for (int i = 0; i < ar.buffSize; i++) {
        middle += ar.buff[i];
      }
      middle /= ar.buffSize;

      Serial.println("Среднее: ");
      Serial.println(middle);
      Serial.println();
      
      if(middle > 512) {
        Serial.println("МОТОР ДОЛЖЕН ПОЕХАТЬ ВНИЗ");
        ar.isMotorEnabled = true;
        ar.isMotorSideUp = false;
      } else {
        Serial.println("МОТОР ДОЛЖЕН ПОЕХАТЬ ВВЕРХ");
        ar.isMotorEnabled = true;
        ar.isMotorSideUp = true;
      }
    }
  }
}

void setMotorMoving() {
  if (!ar.isAutomodeEnabled) return;
  if (!ar.isMotorEnabled)   return; 

  if (ar.isMotorSideUp) {
    if (digitalRead(hollUp) == LOW) {
      digitalWrite(motorEnable, LOW);
      ar.isMotorEnabled = false;
    } else digitalWrite(motorEnable, HIGH);
  } else {
    if (digitalRead(hollDown) == LOW) {
      digitalWrite(motorEnable, LOW);
      ar.isMotorEnabled = false;
    } else digitalWrite(motorEnable, HIGH);
  }
}

void setMotorSideMoving() {
  if (!ar.isAutomodeEnabled) return;
  if (!ar.isMotorEnabled)   return; 

  if (ar.isMotorSideUp) {
    digitalWrite(motorSide, HIGH);
  } else {
    digitalWrite(motorSide, LOW);
  }
}

void setupAllDeviceSensors() {
  pinMode(motorEnable, OUTPUT);
  pinMode(motorSide, OUTPUT);
  pinMode(photoRelay, OUTPUT);
  pinMode(hollUp, INPUT);
  pinMode(hollDown, INPUT);
}

void setupAllTickers() {
  ce.photoRelayMeasuresTicker.attach_ms(10, getPhotoRelayMeasure);
  ce.motorMovesTicker.attach_ms(10, setMotorMoving);
  ce.motorSideTicker.attach_ms(10, setMotorSideMoving);
}

void setup()
{
  Serial.begin(9600);
  
  setupAllDeviceSensors();
  setupAllTickers();
  
  WiFi.begin(wifiLOGIN, wifiPWD);

  Serial.print("\nConnecting Wifi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  if (connectClientWithServer() != 1) {}
  Serial.println("Server connected");

  ce.connectionClient.path = ss.connectionPath;
  ce.connectionClient.host = ss.host;

  ce.managementClient.path = ss.managementPath;
  ce.managementClient.host = ss.host;

  initializeJsonStructures();
}

void loop() { 
  int res = socketConnect();
  if (res == -1)  delay(failedDelayTime);
  else if (res == 0) {
    delay(3000);
    res = sendSettingsToSocket();
    if (res == -1) {
      Serial.println("Failed sending settings");
      delay(failedDelayTime);
    }
  }
  delay(5000);
  connectToSocketManagement();
  ESP.restart();
}

int connectClientWithServer() {
  int attemtsCount = 0;
  while(!ce.client.connect(ss.host, ss.port)) {
    if (++attemtsCount == 10) return -1;

    Serial.println("Retrying to connect the server");
    delay(1000);

  }
  return 1;
}

String getValueFromUpdate(String str, bool isState) {
  char buff[str.length() + 1];
  str.toCharArray(buff, str.length() + 1);
  
  String result = "";
  boolean isFirstHalf = true;
  
  for (int i = 0; i <= str.length(); i++) {
    if (buff[i] == NULL) break;
   
    if (buff[i] == ':') {
      isFirstHalf = false;
      continue;
    }

    if (isFirstHalf && isState) continue;
    if (!isFirstHalf &&!isState) continue;

    result += buff[i];
  }

  return result;
}

void connectToSocketManagement() {
  Serial.println("Attempt to connect with the socket management");
  ce.client.stop();

  
  if (connectClientWithServer() != 1) {
    return;
  }
  Serial.println("Server connected");

  int attemptsCount = 0;
  while(!ce.managementClient.handshake(ce.client)){
    if (++attemptsCount == 10) return;
    
    Serial.println("Retrying to get access from the server");
    delay(3000);
  }
  Serial.println("Access to socket management granted");
  String ping = "1";
  String pong = "0";
  String data;
  ce.managementClient.sendData("connect");
  
  while(WiFi.status() == WL_CONNECTED && ce.client.connected()) {
    ce.managementClient.getData(data);
    delay(500); 
    if (data != NULL && data != "Update is null") {
      if (data == ping) Serial.println("Ping");
      else              Serial.println(data);
    }
    
    if (data == "Sending board id accepted") {
      StaticJsonDocument<1024> boardConnectionData;
      boardConnectionData["boardIdentificationData"].set(ce.boardIdentificationData);
      char output[128];
      serializeJsonPretty(boardConnectionData, output);
      int res = sendJsonObject(output, true, ce.managementClient);
      if (!trackSendingStatusJO(res)) return;
      else                            Serial.println("ACCEPTED");          
    }
    else if (data == ping)                                          ce.managementClient.sendData(pong);
    else if (data == "Update is null")                              ce.managementClient.sendData("update");
    else if (data == "Device was submitted")                        ce.managementClient.sendData("update");
    else if (data == "Board id was not found")                      return;
    else if (data == "Such board UUID is not exists")               return;
    else if (data == "Command is unknown")                          return;
    else if (data == "Socket timeout response")                     return;
    else if (data == "Device was declined")                         return;
    else if (data == "Such board is listening")                     return;
    else {
      if (data != NULL) {
        String sensor = getValueFromUpdate(data, false);
        String state = getValueFromUpdate(data, true);
        changeDeviceSettings(sensor, state);
        ce.managementClient.sendData("update");
      }
    }
    delay(500);   
  }
  Serial.println("The connection was lost");
}

void changeDeviceSettings(String sensor, String state) {
  state.toLowerCase();
  if (sensor == "Мотор вверх") {
    if (ar.isAutomodeEnabled) ar.isAutomodeEnabled = false;
    
    if (state == "true") {
        Serial.println("Мотор вверх включен");
        if (digitalRead(motorEnable) == LOW) digitalWrite(motorEnable, HIGH);
        digitalWrite(motorSide, HIGH);
      } else {
        Serial.println("Мотор вверх выключен");
        if (digitalRead(motorEnable) == HIGH) digitalWrite(motorEnable, LOW);
      }
  }
  else if (sensor == "Мотор вниз") {
    if (ar.isAutomodeEnabled) ar.isAutomodeEnabled = false;
    
    if (state == "true") {
      Serial.println("Мотор вниз включен");
      digitalWrite(motorEnable, HIGH);
      digitalWrite(motorSide, LOW);
    } else {
      if (digitalRead(motorEnable) == HIGH) digitalWrite(motorEnable, LOW);
    }
  }
  else if (sensor == "Автоматический режим") {
    if (digitalRead(motorEnable) == HIGH) {
      digitalWrite(motorEnable, LOW); 
    }
    
    if (state == "true")  ar.isAutomodeEnabled = true;
    else                  ar.isAutomodeEnabled = false;
  }
  else if (sensor == "Время между измерениями (мс)") {
    int x = state.toInt();
    if (x < 1000)     ar.delayTime = 1000;
    else              ar.delayTime = x;
  }
  else if (sensor == "Светодиод") {
    if (state == "true") digitalWrite(led, HIGH);
    else                 digitalWrite(led, LOW);
  }
}

void initializeJsonStructures() {
  StaticJsonDocument<1024> sensorsDoc;
  StaticJsonDocument<1024> typesDoc;
  JsonArray sensors = sensorsDoc.to<JsonArray>();
  JsonArray types = typesDoc.to<JsonArray>();

  for (int i = 0; i < ds.sensorsCount; i++) {
    sensors.add(ds.sensors[i]);
    types.add(ds.sensorsTypes[i]);
  }

  if (sensors.isNull() || types.isNull()) {
    Serial.println("Structure is empty");
    exit(0);
  }

  ce.boardIdentificationData["boardUUID"].set(boardId);
  ce.controlDeviceData["deviceName"].set(ds.deviceName);
  ce.controlDeviceData["deviceDescription"].set(ds.deviceDescription);
  ce.controlDeviceData["sensorsList"].set(sensors);
  ce.controlDeviceData["statesTypesList"].set(types);
}

//0 - successfully sending settings, -1 - fatal exception
int sendSettingsToSocket() {
  String data;
  ce.connectionClient.sendData("check");

  while(WiFi.status() == WL_CONNECTED && ce.client.connected()) {
    ce.connectionClient.getData(data);
    if (data != NULL) Serial.println(data);
    
    if (data == "Sending board id accepted") {
      StaticJsonDocument<1024> boardConnectionData;
      boardConnectionData["boardIdentificationData"].set(ce.boardIdentificationData);
      char output[128];
      serializeJsonPretty(boardConnectionData, output);
      int res = sendJsonObject(output, true, ce.connectionClient);
      if (!trackSendingStatusJO(res)) return -1;
    }
    else if (data == "Authorization board error") return -1;
    else if (data == "User id was not found")     return -1;
    else if (data == "Sending settings accepted") {
      StaticJsonDocument<1024> boardConnectionData;
      boardConnectionData["controlDeviceData"].set(ce.controlDeviceData);
      char output[1024];
      serializeJsonPretty(boardConnectionData, output);
      int res = sendJsonObject(output, false, ce.connectionClient);
      if (!trackSendingStatusJO(res)) return -1;
      delay(1000);
    }
    else if (data == "Integrity objects violation")             return -1;
    else if (data == "Device has not been added")               return -1;
    else if (data == "Device id was not found")                 return -1;
    else if (data == "Such device id already registered")       return 1;
    else if (data == "Device has not been added to user base")  return -1;
    else if (data == "Data were successfully wrote")            return 1;
    
    delay(1000);
  }
}

//1-such board already exists, 0 - successfully connection, -1 - fatal exception
int socketConnect() {
  int attemptsCount = 0;
  while(!ce.connectionClient.handshake(ce.client)){
    if (++attemptsCount == 10) return -1;
    
    Serial.println("Retrying to get access from the server");
    delay(3000);
  }
  Serial.println("Access granted to board connection.");
  
  String data;
  ce.connectionClient.sendData("connect");
  
  while(WiFi.status() == WL_CONNECTED && ce.client.connected()) {
    ce.connectionClient.getData(data);
    if (data != NULL && data != "Searching the client") Serial.println(data);
    
    if (data == "Sending board id accepted") {
      StaticJsonDocument<1024> boardConnectionData;
      boardConnectionData["boardIdentificationData"].set(ce.boardIdentificationData);
      char output[128];
      serializeJsonPretty(boardConnectionData, output);
      int res = sendJsonObject(output, true, ce.connectionClient);
      if (!trackSendingStatusJO(res)) return -1;
    }
    else if (data == "Such board already listening")    return -1;
    else if (data == "Successfully board connection")   return 0;
    else if (data == "Clients was not connected")       return -1;
    else if (data == "Searching the client")            Serial.print(".");
    else if (data == "Such board UUID already exists")  return 1;
    
  delay(1000);
  }

  Serial.println("Connection with network was lost.");
  return -1;
}


bool trackSendingStatusJO(int res) {
  if (res != 1) {
    String textRes;
    switch(res) {
      case 0:
             textRes = "Arrived data is incorrect, but json structure is correct!";
             break;
      case -1:
              textRes = "Arrived data is correct, but json structure is incorrect!";        
              break;
      case -2:
              textRes = "Serialization failed";  
              break;
        }
    Serial.println(textRes);
    return false;
  }
  return true;
}

//1 - true, 0 - false
//-1 - output isnt correct, -2 - serialization failed
int sendJsonObject (const char output[1024], bool isBoardID, WebSocketClient wsClient) {
  wsClient.sendData(output);
  String data;
  
  while(data == "") {
    wsClient.getData(data);
  }

  if (data == "Board id successfully received") {
    if (isBoardID) return 1;
    else           return -1;
  }
  else if (data == "Arrived board id is incorrect") {
    if (isBoardID) return 0;
    else           return -1;
  }
  else if (data == "Settings successfully received") {
    if (!isBoardID) return 1;
    else            return -1;
  }
  else if (data == "Arrived settings is incorrect") {
    if (!isBoardID) return 0;
    else            return -1;
  }
  else if (data == "Serialization failed"){
    return -2;  
  }
  else  sendJsonObject(output, isBoardID, wsClient);
}
  
