//Nickduino based somfy hub for ESP8266 and MQTT interface
/*   This sketch allows you to emulate a Somfy RTS or Simu HZ remote.
   If you want to learn more about the Somfy RTS protocol, check out https://pushstack.wordpress.com/somfy-rts-protocol/
   
   The rolling code will be stored in EEPROM, so that you can power the Arduino off.
   
   Easiest way to make it work for you:
    - Choose a remote number
    - Choose a starting point for the rolling code. Any unsigned int works, 1 is a good start
    - Upload the sketch
    - Long-press the program button of YOUR ACTUAL REMOTE until your blind goes up and down slightly
    - send 'p' to the serial terminal
  To make a group command, just repeat the last two steps with another blind (one by one)
  
  See MQTT section for commands to issue ccontrol commands to somfy
  */

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
    }
    else if (!strcmp(cmnd, "reset")) {
        Serial.print("Received reset command");
        ESP.reset();
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

void setup() {
  Serial.begin(115200);
  Serial.println("Somfy Hub booted");
  pinMode(PORT_TX,OUTPUT);
  //DDRD |= 1<<PORT_TX; // Pin 5 an output
  //PORTD &= !(1<<PORT_TX); // Pin 5 LOW
  GPOS &= !(1<<PORT_TX);

  if (EEPROM.get(EEPROM_ADDRESS, rollingCode) < newRollingCode) {
    EEPROM.put(EEPROM_ADDRESS, newRollingCode);
  }
  Serial.print("Simulated remote number : "); Serial.println(REMOTE, HEX);
  Serial.print("Current rolling code    : "); Serial.println(rollingCode);

  //WIFI and MQTT
  setup_wifi();                   // Connect to wifi 
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

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
                                        // EEPROM. It should take up to 2 adresses but the
                                        // Arduino function takes care of it.
}

void SendCommand(byte *frame, byte sync) {
  if(sync == 2) { // Only with the first frame.
  //Wake-up pulse & Silence
    //PORTD |= 1<<PORT_TX;
    GPOS |= 1<<PORT_TX;
    delayMicroseconds(9415);
    //PORTD &= !(1<<PORT_TX);
    GPOS &= !(1<<PORT_TX);
    delayMicroseconds(89565);
  }

// Hardware sync: two sync for the first frame, seven for the following ones.
  for (int i = 0; i < sync; i++) {
    //PORTD |= 1<<PORT_TX;
    GPOS |= 1<<PORT_TX;
    delayMicroseconds(4*SYMBOL);
    //PORTD &= !(1<<PORT_TX);
    GPOS &= !(1<<PORT_TX);
    delayMicroseconds(4*SYMBOL);
  }

// Software sync
  //PORTD |= 1<<PORT_TX;
  GPOS |= 1<<PORT_TX;
  delayMicroseconds(4550);
  //PORTD &= !(1<<PORT_TX);
  GPOS &= !(1<<PORT_TX);
  delayMicroseconds(SYMBOL);
  
  
//Data: bits are sent one by one, starting with the MSB.
  for(byte i = 0; i < 56; i++) {
    if(((frame[i/8] >> (7 - (i%8))) & 1) == 1) {
      //PORTD &= !(1<<PORT_TX);
      GPOS &= !(1<<PORT_TX);
      delayMicroseconds(SYMBOL);
      //PORTD ^= 1<<5;
      GPOS ^= 1<<5;
      delayMicroseconds(SYMBOL);
    }
    else {
      //PORTD |= (1<<PORT_TX);
      GPOS |= (1<<PORT_TX);
      delayMicroseconds(SYMBOL);
      //PORTD ^= 1<<5;
      GPOS ^= 1<<5;
      delayMicroseconds(SYMBOL);
    }
  }
  
  //PORTD &= !(1<<PORT_TX);
  GPOS &= !(1<<PORT_TX);
  delayMicroseconds(30415); // Inter-frame silence
}
