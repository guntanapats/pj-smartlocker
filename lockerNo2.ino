#include <WiFi.h>
#include <PubSubClient.h>

#define APP_WIFI_SSID  "God_AP"
#define APP_WIFI_PASS  "God123456"

#define APP_MQTT_SERVER  "192.168.0.104"
#define APP_MQTT_PORT    1883

#define RELAY_PIN 14 // ESP32 pin GPIO16, which connects to the solenoid lock via the relay
#define LED_R 26
#define LED_G 25
#define LED_Y 17

int countOpen = 0;
const int hallSensorPin = 34;  // Replace with the actual GPIO pin connected to the Hall sensor
unsigned long period = 3000; 
unsigned long last_time = 0;
int buzzerPin = 18;
int alarm_time = 15*1000;
unsigned long startTime;
unsigned long alarmStartTime = 0;
unsigned long alarmDuration = 15 * 1000; // 7 seconds in milliseconds
unsigned long doorStartTime = 0;
unsigned long doorDuration = 4 * 1000;
int hallThreshold = 2200;
int doorTriggered = 0;
static bool hallTriggered = true;
static bool alarmTriggered = false;

int Hall_Value = 0;

String wifiMac;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

int LockerNo = 2;
char statusVariable[50];
const char* mqttTopic = "";

void setupWiFi() {
    Serial.println("Connecting to " + String(APP_WIFI_SSID) + " ...");

    WiFi.mode(WIFI_STA);
    WiFi.begin(APP_WIFI_SSID, APP_WIFI_PASS);

    if (WiFi.waitForConnectResult() == WL_CONNECTED) {
        wifiMac = WiFi.macAddress();
        wifiMac.replace(":", "");
        wifiMac.toLowerCase();

        Serial.println("Wi-Fi connected, IP address: " + WiFi.localIP().toString());
    }
    else {
        Serial.println("Failed to connect to the Wi-Fi network, restarting...");
        delay(2000);
        ESP.restart();
    }
}

void setupMqttClient() {
    mqttClient.setServer(APP_MQTT_SERVER, APP_MQTT_PORT);
    mqttClient.setCallback(callback);

    Serial.println("Connecting to MQTT broker " + String(APP_MQTT_SERVER) + ":" + String(APP_MQTT_PORT));

    while(!mqttClient.connected()) {
        if (mqttClient.connect("lockerno2")) {
            String ledCommandTopic = "device/+/Locker/"+String(LockerNo)+"/status";

            Serial.println("Connected to MQTT broker");
            Serial.println("Locker Number"+String(LockerNo)+"Subscribing to command topic: " + ledCommandTopic);

            mqttClient.subscribe(ledCommandTopic.c_str());
        }
        else {
            Serial.println("Failed to connect to MQTT broker, retrying in 5 seconds...");
            delay(5000);
        }
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Handle incoming messages on the subscribed topic
    payload[length] = '\0';  // Null-terminate the payload
    strcpy(statusVariable, (char*)payload);
    Serial.println("Received message on topic " + String(topic) + ": " + String(statusVariable));
    
}

void sendCloseLockerStatus(){
    String LockerStatusTopic = "device/246f28f2f29c/Locker/" + String(LockerNo) + "/status";
    String LockerStateStr = "0";
    mqttClient.publish(LockerStatusTopic.c_str(), LockerStateStr.c_str());
}

void play_Incorrect_sound() {
    tone(buzzerPin,330);  // E4
    delay(200);
    noTone(buzzerPin);
}

void setup() {
    // initialize digital pin  as an output.
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(hallSensorPin, INPUT);
    pinMode(LED_R, OUTPUT);
    pinMode(LED_Y, OUTPUT);
    pinMode(LED_G, OUTPUT);
    setupWiFi();
    setupMqttClient();
    sendCloseLockerStatus();
    digitalWrite(LED_G, HIGH);  
    
    
    
}

// the loop function runs over and over again forever
void loop() {
    mqttClient.loop();
    static long alarmStartTime = 0;
    
    int sensorValue = analogRead(hallSensorPin);
    Serial.print("Hall Sensor Value: ");
    Serial.println(sensorValue);
    // Serial.println(doorTriggered);
    
    if(String(statusVariable)=="1"){
        
        // if(countOpen==1){
        //     alarmStartTime = millis();
        //     doorStartTime = millis();
        //     alarmTriggered = true;
        //     delay(2000);
        // }
        
        Serial.println("Pass");
        
        
        if(doorTriggered == 0){
            digitalWrite(RELAY_PIN, HIGH);
            Serial.println("Open Locker");
            doorTriggered = 1;
            countOpen += 1;
        }
        if(!alarmTriggered){
            alarmStartTime = millis();
            doorStartTime = millis();
            alarmTriggered = true;
        }
        if (millis() - alarmStartTime > alarmDuration) {
            play_Incorrect_sound();  // Play the incorrect sound for  15 seconds
            
        }
        if (millis() - doorStartTime > doorDuration){
            Serial.println("Lock on");
            digitalWrite(RELAY_PIN, LOW);
            hallTriggered = false;
            
            
            
        }
        
        digitalWrite(LED_R,HIGH);
        digitalWrite(LED_G,LOW);
        // Serial.println("Open Locker");
        digitalWrite(LED_Y,HIGH);
        
        
        
    }if(hallTriggered == false){
        if(sensorValue > hallThreshold){
                delay(2000);
                
                hallTriggered = true;
                doorTriggered = 0;
                alarmTriggered = false;
                Serial.println("Close Locker");
                sendCloseLockerStatus();
                digitalWrite(LED_Y, LOW);
                strcpy(statusVariable, "0");
                

                if(countOpen==2){
                    Serial.println("CountOpen:2");
                    delay(2000);
                    digitalWrite(RELAY_PIN, LOW);
                    digitalWrite(LED_R,LOW);
                    digitalWrite(LED_G,HIGH);
                    countOpen = 0;
                }
            }  
    }
        
  
  
}

