//Nickduino based somfy hub for ESP8266 and MQTT interface
//https://github.com/Nickduino/Somfy_Remote
// Emulates Somfy RTS remote for e.g. awning motors
//Protocol details https://pushstack.wordpress.com/somfy-rts-protocol/
//Rolling code will be stored in EEPROM
//
//Started
//- chose remote number (or keep default)
//- chose rolling code (or keep default)
//- flash once with EEPROM overwrite in setup() activitad
//- flash normal version w/o EEPROM overwrite
//- Use actual Somfy remote to go into programming mode. Press button on back until awning jogs
//- send "PROG" via MQTT (until awning confirms with jog)
// 
//MQTT Interface control: 
//- cmnd/somfy_hub/control (UP, DOWN, PROG, STOP)
//- cmnd/somfy_hub/rolling_code (code) to set new rolling code
//MQTT Interface status: stat/somfy_hub/ip_address, rolling_code, remote
// Schmolders 03.09.2017
   

#include <EEPROM.h>
//required for MQTT
#include "wifi_credentials.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define PORT_TX 5 //5 of PORTD = DigitalPin 5. GPIO 5 equals D1

#define SYMBOL 640
#define HAUT 0x2
#define STOP 0x1
#define BAS 0x4
#define PROG 0x8
#define EEPROM_ADDRESS 0

#define REMOTE 0x121300    //<-- Change it!

unsigned int newRollingCode = 101;       //<-- Change it!
unsigned int rollingCode = 0;
byte frame[7];
byte checksum;

//MQTT
WiFiClient espClient;
PubSubClient client(espClient);
const char* inTopic = "cmnd/somfy_hub/#";
const char* outTopic = "stat/somfy_hub/";

void BuildFrame(byte *frame, byte button);
void SendCommand(byte *frame, byte sync);

void setup_wifi() {
    delay(10);
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
      
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

//callback function for MQTT client
void callback(char* topic, byte* payload, unsigned int length) {
    payload[length]='\0'; // Null terminator used to terminate the char array
    String message = (char*)payload;
  
    Serial.print("Message arrived on topic: [");
    Serial.print(topic);
    Serial.print("]: ");
    Serial.println(message);
    
    //get last part of topic 
    char* cmnd = "test";
    char* cmnd_tmp=strtok(topic, "/");
  
    while(cmnd_tmp !=NULL) {
      cmnd=cmnd_tmp; //take over last not NULL string
      cmnd_tmp=strtok(NULL, "/"); //passing Null continues on string
      //Serial.println(cmnd_tmp);    
    }
  
    if (!strcmp(cmnd, "control")) {
        if (message == "UP") {
            Serial.println("Received Somfy Up"); 
            BuildFrame(frame, HAUT);
        }
        else if (message == "DOWN") {
            Serial.println("Received Somfy Down"); 
            BuildFrame(frame, BAS);
        }
        else if (message == "STOP") {
            Serial.println("Received Somfy Stop"); 
            BuildFrame(frame, STOP);
        }
        else if (message == "PROG") {
            Serial.println("Received Somfy Prog"); 
            BuildFrame(frame, PROG);
        }
        //TODO add custom series as send over MQTT
        SendCommand(frame, 2);
        for(int i = 0; i<2; i++) {
          SendCommand(frame, 7);
        }
        sendSomfyStatus();
    }
    else if (!strcmp(cmnd, "reset")) {
        Serial.println("Received reset command");
        ESP.reset();
    }
    else if (!strcmp(cmnd, "code")) {
      Serial.print("Received new code");
      unsigned int code;
      //get code from message
      code=message.toInt();
      Serial.println(code);
      //store in EEPROM and set global var
      EEPROM.put(EEPROM_ADDRESS, code);
      rollingCode=code;
    }

}

void reconnect() {
    // Loop until we're reconnected
    while (!client.connected()) {
      Serial.print("Attempting MQTT connection...");
      // Attempt to connect
      if (client.connect("SOMFY_HUB")) {
        Serial.println("connected");        
        client.publish(outTopic, "Somfy Hub booted");        
        // ... and resubscribe
        client.subscribe(inTopic);  
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");      
        delay(5000);
      }
    }
}

void sendSomfyStatus() {
    char outTopic_status[50];
    char msg[50];

    //IP Address
    strcpy(outTopic_status,outTopic);
    strcat(outTopic_status,"ip_address");
    WiFi.localIP().toString().toCharArray(msg,50);
    client.publish(outTopic_status,msg ); 

    //Rolling Code
    strcpy(outTopic_status,outTopic);
    strcat(outTopic_status,"rolling_code");
    unsigned int code;
    EEPROM.get(EEPROM_ADDRESS, code);
    dtostrf(code,2,0,msg);
    client.publish(outTopic_status,msg ); 

    //Remote number
    strcpy(outTopic_status,outTopic);
    strcat(outTopic_status,"remote");
    dtostrf(REMOTE,2,0,msg);
    client.publish(outTopic_status,msg ); 
}

void setup() {
  Serial.begin(115200);
  Serial.println("Somfy Hub booted");
  pinMode(PORT_TX,OUTPUT);
  //DDRD |= 1<<PORT_TX; // Pin 5 an output
  //PORTD &= !(1<<PORT_TX); // Pin 5 LOW
  //GPOS &= !(1<<PORT_TX);
  //GPOC = 1<<PORT_TX;
  digitalWrite(PORT_TX,0);

  EEPROM.begin(512); //added Smo
  if (EEPROM.get(EEPROM_ADDRESS, rollingCode) < newRollingCode) {
    //Achtung der Check ist shit. wenn ich einen neuen Rolling Code wähle muss ich neu initaliisueren mit größer als new Rolling Code!
    EEPROM.put(EEPROM_ADDRESS, newRollingCode);
  }
  //EEPROM.put(EEPROM_ADDRESS, newRollingCode); //only needed ONCE for init of EEPROM.
  Serial.print("Simulated remote number : "); Serial.println(REMOTE, HEX);
  Serial.print("Current rolling code    : "); Serial.println(rollingCode);

  //WIFI and MQTT
  setup_wifi();                   // Connect to wifi 
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

    sendSomfyStatus();
}

void loop() {
    if (!client.connected()) {
        reconnect();
      }
      client.loop();
      
}


void BuildFrame(byte *frame, byte button) {
  unsigned int code;
  EEPROM.get(EEPROM_ADDRESS, code);
  Serial.print("Sending w/ rolling code    : "); Serial.println(code);
  frame[0] = 0xA7; // Encryption key. Doesn't matter much
  frame[1] = button << 4;  // Which button did  you press? The 4 LSB will be the checksum
  frame[2] = code >> 8;    // Rolling code (big endian)
  frame[3] = code;         // Rolling code
  frame[4] = REMOTE >> 16; // Remote address
  frame[5] = REMOTE >>  8; // Remote address
  frame[6] = REMOTE;       // Remote address

  Serial.print("Frame         : ");
  for(byte i = 0; i < 7; i++) {
    if(frame[i] >> 4 == 0) { //  Displays leading zero in case the most significant
      Serial.print("0");     // nibble is a 0.
    }
    Serial.print(frame[i],HEX); Serial.print(" ");
  }
  
// Checksum calculation: a XOR of all the nibbles
  checksum = 0;
  for(byte i = 0; i < 7; i++) {
    checksum = checksum ^ frame[i] ^ (frame[i] >> 4);
  }
  checksum &= 0b1111; // We keep the last 4 bits only


//Checksum integration
  frame[1] |= checksum; //  If a XOR of all the nibbles is equal to 0, the blinds will
                        // consider the checksum ok.

  Serial.println(""); Serial.print("With checksum : ");
  for(byte i = 0; i < 7; i++) {
    if(frame[i] >> 4 == 0) {
      Serial.print("0");
    }
    Serial.print(frame[i],HEX); Serial.print(" ");
  }

  
// Obfuscation: a XOR of all the bytes
  for(byte i = 1; i < 7; i++) {
    frame[i] ^= frame[i-1];
  }

  Serial.println(""); Serial.print("Obfuscated    : ");
  for(byte i = 0; i < 7; i++) {
    if(frame[i] >> 4 == 0) {
      Serial.print("0");
    }
    Serial.print(frame[i],HEX); Serial.print(" ");
  }
  Serial.println("");
  Serial.print("Rolling Code  : "); Serial.println(code);
  EEPROM.put(EEPROM_ADDRESS, code + 1); //  We store the value of the rolling code in the
  EEPROM.commit();                      // EEPROM. It should take up to 2 adresses but the
                                        // Arduino function takes care of it.
}

void SendCommand(byte *frame, byte sync) {
  if(sync == 2) { // Only with the first frame.
  //Wake-up pulse & Silence
    //PORTD |= 1<<PORT_TX;
    //GPOS |= 1<<PORT_TX; //wrong usage anyway
    digitalWrite(PORT_TX,1);
    delayMicroseconds(9415);
    //PORTD &= !(1<<PORT_TX);
    //GPOS &= !(1<<PORT_TX);
    digitalWrite(PORT_TX,0);
    delayMicroseconds(89565);
  }

// Hardware sync: two sync for the first frame, seven for the following ones.
  for (int i = 0; i < sync; i++) {
    //PORTD |= 1<<PORT_TX;
    //GPOS |= 1<<PORT_TX;
    digitalWrite(PORT_TX,1);
    delayMicroseconds(4*SYMBOL);
    //PORTD &= !(1<<PORT_TX);
    //GPOS &= !(1<<PORT_TX);
    digitalWrite(PORT_TX,0);
    delayMicroseconds(4*SYMBOL);
  }

// Software sync
  //PORTD |= 1<<PORT_TX;
  //GPOS |= 1<<PORT_TX;
  digitalWrite(PORT_TX,1);
  delayMicroseconds(4550);
  //PORTD &= !(1<<PORT_TX);
  //GPOS &= !(1<<PORT_TX);
  digitalWrite(PORT_TX,0);
  delayMicroseconds(SYMBOL);
  
  
//Data: bits are sent one by one, starting with the MSB.
  for(byte i = 0; i < 56; i++) {
    if(((frame[i/8] >> (7 - (i%8))) & 1) == 1) {
      //PORTD &= !(1<<PORT_TX);
      //GPOS &= !(1<<PORT_TX);
      digitalWrite(PORT_TX,0);
      delayMicroseconds(SYMBOL);
      //PORTD ^= 1<<5;
      //GPOS ^= 1<<5; //dont need to xor if I know state
      digitalWrite(PORT_TX,1);
      delayMicroseconds(SYMBOL);
    }
    else {
      //PORTD |= (1<<PORT_TX);
      //GPOS |= (1<<PORT_TX);
      digitalWrite(PORT_TX,1);
      delayMicroseconds(SYMBOL);
      //PORTD ^= 1<<5;
      //GPOS ^= 1<<5;
      digitalWrite(PORT_TX,0);
      delayMicroseconds(SYMBOL);
    }
  }
  
  //PORTD &= !(1<<PORT_TX);
  //GPOS &= !(1<<PORT_TX);
  digitalWrite(PORT_TX,0);
  delayMicroseconds(30415); // Inter-frame silence
}
