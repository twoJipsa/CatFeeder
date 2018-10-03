#include <Arduino.h>
#include <Stream.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <Servo.h> 

//AWS
#include "sha256.h"
#include "Utils.h"


//WEBSockets
#include <Hash.h>
#include <WebSocketsClient.h>

//MQTT PAHO
#include <SPI.h>
#include <IPStack.h>
#include <Countdown.h>
#include <MQTTClient.h>



//AWS MQTT Websocket
#include "Client.h"
#include "AWSWebSocketClient.h"
#include "CircularByteBuffer.h"

extern "C" {
  #include "user_interface.h"
}

//AWS IOT config, change these:
char wifi_ssid[]       = "your wifi id";
char wifi_password[]   = "your wifi password";
char aws_endpoint[]    = "your aws https address";
char aws_key[]         = "your aws access key";
char aws_secret[]      = "your aws secret key";
char aws_region[]      = "your aws region";
const char* aws_topic  = "$aws/things/Feeder/shadow/update";

int port = 443; 

//MQTT config
const int maxMQTTpackageSize = 512;
const int maxMQTTMessageHandlers = 1;

ESP8266WiFiMulti WiFiMulti;

AWSWebSocketClient awsWSclient(1000);

IPStack ipstack(awsWSclient);
MQTT::Client<IPStack, Countdown, maxMQTTpackageSize, maxMQTTMessageHandlers> client(ipstack);

//AWS IoT Server Setting
#define THING_NAME "Feeder"
char clientID[] = "feederClient";

char publishPayload[512];
char publishTopic[]   = "$aws/things/" THING_NAME "/shadow/update";
/*char *subscribeTopic[5] = {
  "$aws/things/" THING_NAME "/shadow/update/accepted",
  "$aws/things/" THING_NAME "/shadow/update/rejected",
  "$aws/things/" THING_NAME "/shadow/update/delta",
  "$aws/things/" THING_NAME "/shadow/get/accepted",
  "$aws/things/" THING_NAME "/shadow/get/rejected"
};*/
char *subscribeTopic[2] = {
  "$aws/things/" THING_NAME "/shadow/update/accepted", 
  "$aws/things/" THING_NAME "/shadow/get/accepted", 
};

//# of connections
long connection = 0;

//count messages arrived
int arrivedcount = 0;

//Servo Motor Setting
Servo feedServo; 
int motor_state = 1;

void motorOn(void)
{
  feedServo.write(180);
  delay(500);   
  feedServo.write(0);
  delay(500);  
  Serial.println("MOTOR ON");
  
} 

void updateMotorState(int desired_motor_state) {
  printf("change motor_state to %d\r\n", desired_motor_state);
  motor_state = desired_motor_state;
  
  motorOn();
  
  MQTT::Message message;
  char buf[100];  
  sprintf(buf, "{\"state\":{\"reported\":{\"motor\":%d}},\"clientToken\":\"%s\"}",
    motor_state,
    clientID
  );
  message.qos = MQTT::QOS0;
  message.retained = false;
  message.dup = false;
  message.payload = (void*)buf;
  message.payloadlen = strlen(buf)+1;
  
  int rc = client.publish(aws_topic, message); 
    
  printf("Publish [%s] %s\r\n", aws_topic, buf);
}

//callback to handle mqtt messages
void messageArrived(MQTT::MessageData& md)
{
  char buf[512];
  char *pch;
  int desired_motor_state;

  MQTT::Message &message = md.message;

  Serial.print("Message ");
  Serial.print(++arrivedcount);
  Serial.print(" arrived: qos ");
  Serial.print(message.qos);
  Serial.print(", retained ");
  Serial.print(message.retained);
  Serial.print(", dup ");
  Serial.print(message.dup);
  Serial.print(", packetid ");
  Serial.println(message.id);
  Serial.print("Payload ");
  char* msg = new char[message.payloadlen+1]();
  memcpy (msg,message.payload,message.payloadlen);
  Serial.println(msg);

  pch = strstr(msg, "\"desired\":{\"motor\":");
  if (pch != NULL) {
    pch += strlen("\"desired\":{\"motor\":");
    desired_motor_state = *pch - '0';  
    updateMotorState(desired_motor_state);
  }
  else
  {
    ;
  }
  
  delete msg;
}

//connects to websocket layer and mqtt layer
bool connect () {

    if (client.isConnected ()) {    
        client.disconnect ();
    }  
    //delay is not necessary... it just help us to get a "trustful" heap space value
    delay (1000);
    Serial.print (millis ());
    Serial.print (" - conn: ");
    Serial.print (++connection);
    Serial.print (" - (");
    Serial.print (ESP.getFreeHeap ());
    Serial.println (")");
    
   int rc = ipstack.connect(aws_endpoint, port);
    if (rc != 1)
    {
      Serial.println("error connection to the websocket server");
      return false;
    } else {
      Serial.println("websocket layer connected");
    }

    Serial.println("MQTT connecting");
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 4;
    
    data.clientID.cstring = &clientID[0];
    rc = client.connect(data); 
    
    if (rc != 0)
    {
      Serial.print("error connection to MQTT server");
      Serial.println(rc);
      return false;
    }
    Serial.println("MQTT connected");
    return true;
}

//subscribe to a mqtt topic
void subscribe () {
   //subscript to a topic
    
    for (int i=0; i<2; i++) 
    {     
      int rc = client.subscribe(subscribeTopic[i], MQTT::QOS0, messageArrived);
      if (rc != 0) {
        Serial.print("rc from MQTT subscribe is ");
        Serial.println(rc);
        return;
      }
    }
    Serial.println("MQTT subscribed");
}

void setup() {
    wifi_set_sleep_type(NONE_SLEEP_T);
    Serial.begin (115200);
    delay (2000);
    Serial.setDebugOutput(1); 
    
    feedServo.attach(D5);
    feedServo.write(0); 

    //fill with ssid and wifi password
    WiFiMulti.addAP(wifi_ssid, wifi_password);
    Serial.println ("connecting to wifi");
    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
        Serial.print (".");
    }
    Serial.println ("\nconnected");

    //fill AWS parameters    
    awsWSclient.setAWSRegion(aws_region);
    awsWSclient.setAWSDomain(aws_endpoint);
    awsWSclient.setAWSKeyID(aws_key);
    awsWSclient.setAWSSecretKey(aws_secret);
    awsWSclient.setUseSSL(true);
 
    if (connect ()){
      subscribe ();
    }

}

void loop() {
  //keep the mqtt up and running
  if (awsWSclient.connected ()) {    
      client.yield(50);
  } else {
    //handle reconnection
    if (connect ()){
      subscribe ();      
    }
  }

}
