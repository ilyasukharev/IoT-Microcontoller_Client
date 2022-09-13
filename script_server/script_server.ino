#include <ESP8266WiFi.h>
#include <Ticker.h>

#define led D8
#define photoRelay D7
#define acp A0
#define motorEnable D2
#define changeMotor D1
#define hollUp D3
#define hollDown D4

using namespace std;
WiFiServer server(80);

//for hand mode
boolean stateLed = false;
boolean stateMotorUp = false;
boolean stateMotorDown = false;
boolean autoMode = false;
//

//for auto mode
boolean state1 = false;
boolean state2 = false;

boolean stateMotorTICKER = false;
boolean stateMotorReverseSideTICKER = false;
boolean hUpState;
boolean hDownState;
//

int dataInternalSensor[3];
int internalInd = 0;

int dataExternalSensor[3];
int externalInd = 0;

int middleInternalValue = 0;
int middleExternalValue = 0;

Ticker tickInternalData;
Ticker tickExternalData;

Ticker tickMotorEnable;
Ticker tickMotorDisable;

Ticker tickMotorReverseSide;
Ticker tickMotorNormalSide;

Ticker tickHUPStateChecker;
Ticker tickHDOWNStateChecker;

Ticker tickStateChecker;

void setup() {
  //Enable mode
  pinMode(led, OUTPUT);
  pinMode(photoRelay, OUTPUT);
  pinMode(motorEnable, OUTPUT);
  pinMode(changeMotor, OUTPUT);
  pinMode(hollUp, INPUT);
  pinMode(hollDown, INPUT);
  
  //Connect to WLAN
  WiFi.begin("lolita", "cardigan92");

  //Enable terminal
  Serial.begin(9600);
  
  
  //Output data
  delay(5000);
  Serial.println(WiFi.localIP());
  
  server.begin();
  tickInternalData.attach(5.0, getInternalData);
  tickExternalData.attach(5.2, getExternalData);

  tickMotorEnable.attach(0.1, doCheckPositiveStateMotorTicker);
  tickMotorDisable.attach(0.1, doCheckNegativeStateMotorTicker);

  tickMotorReverseSide.attach(0.1, doMotorPositiveReverseSideTicker);
  tickMotorNormalSide.attach(0.1, doMotorNegativeReverseSideTicker);

  tickHUPStateChecker.attach(0.1, doGetHollUpState);
  tickHDOWNStateChecker.attach(0.1, doGetHollDownState);

  tickStateChecker.attach(0.1, doStateChecker);
}

void getInternalData(){
  if (autoMode){
    digitalWrite(photoRelay, HIGH);
    dataInternalSensor[internalInd] = analogRead(acp);
    Serial.println("Внутренний датчик");
    Serial.println(dataInternalSensor[internalInd]);
    internalInd++;

    if (internalInd == 3){
      for (int i = 0; i < 3; i++){
        middleInternalValue += dataInternalSensor[i];
      }
      middleInternalValue /= 3;
      internalInd = 0;
    }
   }
}
void getExternalData(){
  if (autoMode){
    digitalWrite(photoRelay, LOW);
    dataExternalSensor[externalInd] = analogRead(acp);
    Serial.println("Внешний датчик");
    Serial.println(dataExternalSensor[externalInd]);
    externalInd++;

    if (externalInd == 3){
      for (int i = 0; i < 3; i++){
        middleExternalValue += dataExternalSensor[i];
      }
      middleExternalValue /= 3;
      externalInd = 0;
      getCommonSensorData();
    }
  }
}
void getCommonSensorData(){
  Serial.println("Среднее внутреннее значение");
  Serial.println(middleInternalValue);
  Serial.println("Среднее внешнее значение");
  Serial.println(middleExternalValue);
    if (middleInternalValue < 450 && middleExternalValue >= 650){
        //motorDown
        //state2 = false
        state1 = true;
    }
    else if (middleInternalValue < 450 && middleExternalValue < 650){
      //motorUp
        //state1 = false;
        state2 = true;
    }
    middleInternalValue = 0;
    middleExternalValue = 0;
}

void doStateChecker(){
  if (autoMode){
    if (state1){
      if (!hUpState){
        stateMotorReverseSideTICKER = false;
        stateMotorTICKER = true;
      }
      else{
        stateMotorTICKER = false;
        state1 = false;
      }
    }
   else if(state2){
    if (!hDownState){
      stateMotorReverseSideTICKER = true;
      stateMotorTICKER = true;
    }
    else{
      stateMotorTICKER = false;
      state2 = false;
    }
  }
 }
}

void doCheckPositiveStateMotorTicker(){
  if (autoMode){
    if (stateMotorTICKER){
      delay(2000);
      digitalWrite(motorEnable, HIGH);
    } 
  }
}
void doCheckNegativeStateMotorTicker(){
  if (autoMode){
    if (!stateMotorTICKER){
      digitalWrite(motorEnable, LOW);
    }
  }
}
void doMotorPositiveReverseSideTicker(){
  if (autoMode){
    if (stateMotorReverseSideTICKER){
      digitalWrite(changeMotor, HIGH);
    }
  }
}
void doMotorNegativeReverseSideTicker(){
  if (autoMode){
    if (!stateMotorReverseSideTICKER){
      digitalWrite(changeMotor, LOW);
    }  
  }
}
void doGetHollUpState(){
  if (autoMode){
    if (digitalRead(hollUp) == LOW){
      Serial.println("ON");
      hUpState = true;
    }
    else{
      hUpState = false;
    }
  }
}
void doGetHollDownState(){
  if (autoMode){
    if (digitalRead(hollDown) == LOW){
      hDownState = true;
    }
    else{
      hDownState = false;
    }  
  }
}


void loop() {
  WiFiClient client = server.available();
  if (client){
    Serial.println("Client connected!");
    while(client){
      int indBuff = 0;
      char buff[20];
      while(client.available()>0){
        char c = client.read();
        
        if (c == 'l'){
          if (stateLed == false){
            digitalWrite(led, HIGH);
            stateLed = true;
            char buff[5] = {'L','E','D','O','N'};
            client.write(buff, sizeof(buff));
          }
          else{
            digitalWrite(led, LOW);
            stateLed = false;
            char buff[6] = {'L','E','D','O','F', 'F'};
            client.write(buff, sizeof(buff));
          }
        }

        if (c == 'u'){
          digitalWrite(changeMotor, HIGH);
          if (!stateMotorUp){
              digitalWrite(motorEnable, HIGH);
              stateMotorUp = true;
              if (digitalRead(motorEnable) == HIGH){
                if (digitalRead(changeMotor) == HIGH){
                  if (digitalRead(hollDown) == LOW){
                    digitalWrite(motorEnable, LOW);
                  }
                }
              }
              char buff[9] = {'M','O','T','O', 'R', 'U', 'P', 'O', 'N'};
              client.write(buff, sizeof(buff));
            }
            else{
              digitalWrite(motorEnable, LOW);
              stateMotorUp = false;
              char buff[10] = {'M','O','T','O', 'R', 'U', 'P', 'O', 'F', 'F'};
              client.write(buff, sizeof(buff));
            }
        }

        if (c == 'd'){
          digitalWrite(changeMotor, LOW);
          if (!stateMotorDown){
              digitalWrite(motorEnable, HIGH);
              stateMotorDown = true;
              if (digitalRead(motorEnable) == HIGH){
                if (digitalRead(changeMotor) == LOW){
                  if (digitalRead(hollUp) == LOW){
                    digitalWrite(motorEnable, LOW);
                  }
                }
              }
              char buff[11] = {'M','O','T','O', 'R', 'D', 'O', 'W', 'N', 'O', 'N'};
              client.write(buff, sizeof(buff));
            }
            else{
              digitalWrite(motorEnable, LOW);
              stateMotorDown = false;
              char buff[12] = {'M','O','T','O', 'R', 'D', 'O', 'W', 'N', 'O', 'F', 'F'};
              client.write(buff, sizeof(buff));
            }
        }

        if (c == 'a'){
          if (autoMode == false){
            if (digitalRead(motorEnable) == true){
              digitalWrite(motorEnable, LOW);
            }
            autoMode = true;
            char buff[10] = {'A','U','T','O','M','O','D','E','O','N'};
            client.write(buff, sizeof(buff));
          }
          else{
            autoMode = false;
            if (digitalRead(motorEnable) == true){
              digitalWrite(motorEnable, LOW);
            }
            char buff[11] = {'A','U','T','O','M','O','D','E','O','F','F'};
            client.write(buff, sizeof(buff));
          }
        }

        if (c == '1'){
          char buff[1] = {'0'};
          client.write(buff, sizeof(buff));
        }
        }
      }
    }
}
