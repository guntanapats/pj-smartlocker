#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <vector>
#include <SPI.h>
#include <MFRC522.h>
#include <PubSubClient.h>

#define RFID_SS_PIN 5
#define RFID_RESET_PIN 36

MFRC522 rfid(RFID_SS_PIN,RFID_RESET_PIN);
String storedUID = "";
String AdminUID = "30893312";
int Card_State = 0;
unsigned long lastCardPresentTime = 0;

#define APP_MQTT_SERVER  "192.168.0.104"
#define APP_MQTT_PORT    1883

String wifiMac;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

char statusVariable[2]; 
const char* mqttTopic = "";
String LockerStatusTopic ="";

// Set the LCD address to 0x27 or 0x3F for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 20, 4);

//State
// 0 - Choose Locker
// 1 - Set password
// 2 - confirm password
// 3 - open locker


const byte ROW_NUM    = 4; // four rows
const byte COLUMN_NUM = 4; // four columns
char keys[ROW_NUM][COLUMN_NUM] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
 
byte pin_rows[ROW_NUM] = {13, 12, 14, 27}; //connect to the row pinouts of the keypad
byte pin_column[COLUMN_NUM] = {16, 17, 25, 26}; //connect to the column pinouts of the keypad
Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_column, ROW_NUM, COLUMN_NUM);

int MAX_PASSWORD_LENGTH = 20;
String currentPassword = "";
int Locker_State = 0;
String currentLocker = "";
String confirmPassword = "";
const int NumberOfLocker = 2;
int countcon = 0 ;
int countenter = 0;
int countOpen = 0;
int MIN_PASSWORD = 4;
int MAX_PASSWORD = 9;


const int buzzerPin = 4;

// Locker passwords
String Locker_Password[NumberOfLocker];
String Locker_OC_Status[NumberOfLocker];

std::vector<int> Locker_CountEnter;


// NVS variable to save SSID & PASS
String APP_WIFI_SSID = "";
String APP_WIFI_PASS = "";

#define SOFTAP_WIFI_SSID "ESP-6410110205"
#define SOFTAP_WIFI_PASS "205205205"

unsigned long previousMillis = 0;

WebServer webServer(80);
Preferences pref;

const char *index_html PROGMEM = R"(
<!DOCTYPE html>
<head>
    <meta charset="UTF-8">
    <title>Home</title>
    <style>
        .custom-heading {
            font-family: 'Prompt', sans-serif; 
        }

        .custom-link {
            color: #086CF2;
            font-family: 'Prompt', sans-serif; 
            padding-left: 20px;
        }
    </style>
</head>
<body>
    <h1 class="custom-heading"> 🏠 Home</h1>
    <p><a href="/wifisetup" class="custom-link">WiFi Setup</a></p>
    <p><a href="/update" class="custom-link">Firmware Update</a></p>
    <div>
        <p>
            <strong class="customdata">Locker 1 Status : </strong> 
            <span id="Locker_One_Status" class="customdata">N/A</span>
        </p>
        <p>
            <strong class="customdata">Locker 2 Status : </strong> 
            <span id="Locker_Two_Status" class="customdata">N/A</span> 
        </p>
        
    </div>

     <script>
        setInterval(async () => {
            const response = await fetch("/getLockerData");
            const data = await response.json();
            document.getElementById("Locker_One_Status").innerHTML = data.Locker_One_Status;
            document.getElementById("Locker_Two_Status").innerHTML = data.Locker_Two_Status;
            
        }, 1000);
    </script>
</body>
</html>
)";

const char *wifisetup_html PROGMEM = R"(
<!DOCTYPE html>
<head>
    <meta charset="UTF-8">
    <title>WiFi Setup</title>
    <style>
        .backhome{
            font-family: 'Prompt', sans-serif; 
        }
        .custom-heading {
            font-family: 'Prompt', sans-serif; 
        }
        .customdata {
            font-family: 'Prompt', sans-serif; 
            font-size: 12pt;
            padding: 10px;
        }
        .custombutton {
            font-family: 'Prompt', sans-serif; 
            font-size: 12pt;
        }
    </style>
</head>
<body>
    <p><a href="/" class="backhome"> <- Home </a></p>
    <h1 class="custom-heading">Setup WiFi</h1>
    <form action="/setwifi" method="post">
        <label for="ssid" class="customdata">SSID:</label>
        <input type="text" id="ssid" name="ssid" required><br>
        <label for="pass" class="customdata">Password:</label>
        <input type="password" id="pass" name="pass" required><br>
        <input type="submit" class="custombutton" value="Submit">
    </form>
    <form action="/resetap" method="post">
        <input type="submit" class="custombutton" value="Reset to AP mode">
    </form>
</body>
</html>
)";
void setupMqttClient() {
    mqttClient.setServer(APP_MQTT_SERVER, APP_MQTT_PORT);
    mqttClient.setCallback(callback);

    Serial.println("Connecting to MQTT broker " + String(APP_MQTT_SERVER) + ":" + String(APP_MQTT_PORT));

    while(!mqttClient.connected()) {
        if (mqttClient.connect("panel")) {

            for (int i = 1 ; i <= NumberOfLocker; i++){
                String LockerStatusTopic = "device/"+wifiMac+"/Locker/" + String(i) + "/status";
                String LockerStateStr = "0";
                mqttClient.publish(LockerStatusTopic.c_str(), LockerStateStr.c_str());
            }

            // mqttClient.subscribe(ledCommandTopic.c_str());
        }
        else {
            Serial.println("Failed to connect to MQTT broker, retrying in 5 seconds...");
            delay(5000);
        }
    }
}

void sendOpenLockerStatus(){
    String LockerStatusTopic = "device/"+wifiMac+"/Locker/" + String(currentLocker) + "/status";
    String LockerStateStr = "1";
    Serial.println("Send Status "+String(LockerStatusTopic)+" "+ String(LockerStateStr));
    mqttClient.publish(LockerStatusTopic.c_str(), LockerStateStr.c_str());
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Handle incoming messages on the subscribed topic
    
    payload[length] = '\0';  // Null-terminate the payload
    strcpy(statusVariable, (char*)payload);
    Serial.println("Received message on topic " + String(LockerStatusTopic) + ": " + String(statusVariable));
}

void readRFID(){
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()){
        lastCardPresentTime = 0;
        return;
    }

    if (lastCardPresentTime == 0) {
         lastCardPresentTime = millis(); // Record the time when the card is first detected
    }

    rfid.PICC_HaltA();
    storedUID = "";
    Serial.print("UID:");
    for (int i = 0 ; i < rfid.uid.size ; i++){
        storedUID += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
        storedUID += String(rfid.uid.uidByte[i], HEX);
        Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
        Serial.print(rfid.uid.uidByte[i], HEX);
    }
    Serial.println();
    if(Locker_State == 1){
        if(storedUID.equals(AdminUID)){
                Serial.println("Card pass");
                Card_State = 1;
                storedUID = "";
        }
    }
}

void ClearNVS()
{
    pref.begin("iot-app", false);
    pref.remove("ssid");
    pref.remove("pass");
    pref.end();
}

void SaveToNVS(String ssid, String pass)
{
    pref.begin("iot-app", false);
    pref.putString("ssid", ssid);
    pref.putString("pass", pass);
    pref.end();
}

void setupWiFi()
{
    Serial.println();

    //--------------------------------------------------------AP-Mode------------------------------------------------------------------
    WiFi.mode(WIFI_AP);
    WiFi.softAP(SOFTAP_WIFI_SSID, SOFTAP_WIFI_PASS);

    String esp32IPAddress = WiFi.softAPIP().toString();
    wifiMac = WiFi.macAddress();
    wifiMac.replace(":", "");
    wifiMac.toLowerCase();

    Serial.println();
    Serial.println("--AP Mode--");
    Serial.println("Connect to ESP32  SSID: \"" + String(SOFTAP_WIFI_SSID) + "\" IP: " + esp32IPAddress);

    //--------------------------------------------------------STA-Mode------------------------------------------------------------------
    
    pref.begin("iot-app", false);
    String savedSSID = pref.getString("ssid", "");
    String savedPass = pref.getString("pass", "");

    if (savedSSID != "" && savedPass != "")
    {
        int attempts = 0;
        while (attempts < 3)
        {
            Serial.println("Connecting to WiFi SSID: " + savedSSID);
            WiFi.mode(WIFI_STA);
            WiFi.begin(savedSSID.c_str(), savedPass.c_str());

            int connectionAttempts = 0;
            while (WiFi.status() != WL_CONNECTED && connectionAttempts < 20)
            {
                delay(500);
                Serial.print(".");
                connectionAttempts++;
            }

            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println();
                Serial.println("--STA Mode--");
                Serial.println("Wi-Fi connected, IP address: " + WiFi.localIP().toString());
                return; // Exit the function if connected successfully
            }

            attempts++;
            Serial.println();
            Serial.println("Failed to connect to WiFi. Retrying...");
            delay(2000); // Wait for 2 seconds before the next attempt
        }

        Serial.println();
        Serial.println("Failed to connect to WiFi after multiple attempts. Continuing in AP mode.");

        // Clear NVS values
        ClearNVS();
        ESP.restart();
    }
}
void setupWebServer()
{
    webServer.on("/", HTTP_GET, []()
                 { webServer.send(200, "text/html", index_html); });
    // webServer.on("/getsensor", HTTP_GET, []()
    //              { webServer.send(200, "text/html", getsensor_html); });
    webServer.on("/getLockerData", HTTP_GET, []()
                 {
        StaticJsonDocument<1000> doc;
        doc["Locker_One_Status"] = Locker_OC_Status[0];
        doc["Locker_Two_Status"] = Locker_OC_Status[1];
        


        String json;
        serializeJson(doc, json);

        webServer.send(200, "application/json", json); });

    webServer.on("/wifisetup", HTTP_GET, []()
                 { webServer.send(200, "text/html", wifisetup_html); });

    webServer.on("/setwifi", HTTP_POST, []()
                 {
        String ssid = webServer.arg("ssid");
        String pass = webServer.arg("pass");

        SaveToNVS(ssid, pass);
        webServer.send(200, "text/plain", "WiFi credentials set. Restarting...");
        ESP.restart(); });

    webServer.on("/resetap", HTTP_POST, []()
                 {
        // Clear NVS values
        ClearNVS();

        // Set WiFi mode to AP mode
        WiFi.mode(WIFI_AP);
        WiFi.softAP(SOFTAP_WIFI_SSID, SOFTAP_WIFI_PASS);

        webServer.sendHeader("Location", "/", true);
        webServer.send(200, "text/plain", "Reset To AP Mode. Restarting...");
        ESP.restart(); });
    webServer.begin();
}

void handleLockerInput(char key) {
  // Handle password input and update the display in real-time
  
    
        lcd.setCursor(0,1);

        // Display the last character as a number
        lcd.print(currentLocker);
    
}

void handlePasswordInput(char key) {
  // Handle password input and update the display in real-time
  
    
        lcd.setCursor(0,1);

        // Display previous characters with '*'
        
        for (int i = 0; i < currentPassword.length() - 1; i++) {
            lcd.print('*');
        }

        // Display the last character as a number
        lcd.print(currentPassword.charAt(currentPassword.length() - 1));
    
}

void con_handlePasswordInput(char key) {
  // Handle password input and update the display in real-time
  
        lcd.setCursor(0,1);

        // Display previous characters with '*'
        for (int i = 0; i < confirmPassword.length() - 1; i++) {
            lcd.print('*');
        }

        // Display the last character as a number
        lcd.print(confirmPassword.charAt(confirmPassword.length() - 1));
    
}

void handleKeypadInput(char key){
    Serial.println("Locker State :"+Locker_State);
    Serial.println("Current Locker :"+currentLocker);
    Serial.println("Current Password :"+currentPassword);
    Serial.println("Confirm Password :"+confirmPassword);
    int IntcurrentLocker = currentLocker.toInt();

    if(key == 'D'){
        if(Locker_State == 2){
            Locker_State = 0;
            currentLocker = "";
            currentPassword = "";
            confirmPassword = "";
            Locker_Password[IntcurrentLocker-1] = "";
            play_Correct_sound();
            delay(1000);
            Locker_State = 0;
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Choose Locker : ");
            lcd.setCursor(0,1);
            lcd.print("");
        }else{
            Locker_State = 0;
            currentLocker = "";
            currentPassword = "";
            confirmPassword = "";
            play_Correct_sound();
            delay(1000);
            Locker_State = 0;
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Choose Locker : ");
            lcd.setCursor(0,1);
            lcd.print("");
        }
    }

    if (Locker_State == 0){
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Choose Locker : ");
        lcd.setCursor(0,1);
        lcd.print("");
        if (key >= '0' && key <= '9'){
            currentLocker += key;
            handleLockerInput(key);
        }else if(key == 'A'){
          if(currentLocker.length()>0){
            if(currentLocker.toInt()>NumberOfLocker){
                lcd.clear();
                lcd.setCursor(0,0);
                lcd.print("Choose Locker : ");
                lcd.setCursor(0,1);
                lcd.print("");
                currentLocker = "";
                play_Incorrect_sound();
            }else{
                Locker_State = 1;
                play_Correct_sound();
                key = 'Z';
                handleKeypadInput(key); 
            }
            
          }else{
            
          }
        }else if(key == 'B'){
            LockerdeleteLastCharacter();
            handleLockerInput(key);
        }
    }else if (Locker_State == 1){
        
        
        if(Locker_Password[IntcurrentLocker-1]==""){
            Serial.print("Pass\n");
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Set Password : ");
            lcd.setCursor(0,1);
            lcd.print("");
            if (key >= '0' && key <= '9' ){
                if (currentPassword.length() < MAX_PASSWORD_LENGTH) {
                    
                    currentPassword += key;
                    Serial.println("\nnow current password"+currentPassword);
                    handlePasswordInput(key);
                    
                }
                
            }else if(key == 'A'){
                if(currentPassword.length() <= MIN_PASSWORD || currentPassword.length() >= MAX_PASSWORD){
                    Serial.println("Set"+currentPassword);
                    play_Incorrect_sound();
                    lcd.clear();
                    lcd.setCursor(0,0);
                    lcd.print("Set Password : ");
                    lcd.setCursor(0,1);
                    lcd.print("");
                    lcd.setCursor(0,2);
                    lcd.print("INCORRECT! PLEASE");
                    lcd.setCursor(0,3);
                    lcd.print("ENTER 5-8 INTEGERS");
                    delay(1000);
                    key = 'Z';
                    currentPassword = "";
                    handleKeypadInput(key);
                }else{
                    Serial.println("set complete");
                    Locker_Password[IntcurrentLocker-1] = currentPassword;
                    Locker_State = 2;
                    play_Correct_sound();
                    key = 'Z';
                    handleKeypadInput(key);
                }

            }else if(key == 'B'){
                if(currentPassword.length()>1){
                    deleteLastCharacter();
                    handlePasswordInput(key);
                }else{
                    deleteLastCharacter();
                    lcd.setCursor(0,1);
                    lcd.print("");
                }
               
            }
        }else {
            
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Enter Password : ");
            lcd.setCursor(0,1);
            lcd.print("");
            if (key >= '0' && key <= '9'){
                if(currentPassword.length()<MAX_PASSWORD_LENGTH){
                
                    currentPassword += key;
                    handlePasswordInput(key);
                }
        
            }else if(key == 'A'){
                if(Locker_Password[IntcurrentLocker-1] == currentPassword ){
                    Locker_State = 3;
                    currentPassword = "";
                    countenter = 0;
                    confirmPassword = "";
                    play_Correct_sound();
                    key = 'Z';
                    handleKeypadInput(key);

                }else{
                    lcd.clear();
                    lcd.setCursor(0,0);
                    lcd.print("Enter Password : ");
                    lcd.setCursor(0,1);
                    lcd.print("");
                    lcd.setCursor(0,2);
                    lcd.print("Wrong! Try agian");
                    play_Incorrect_sound();
                    currentPassword = "";
                    countenter += 1 ;
                }
            }else if(key == 'B'){
                if(currentPassword.length()>1){
                    deleteLastCharacter();
                    handlePasswordInput(key);
                }else{
                    deleteLastCharacter();
                    lcd.setCursor(0,1);
                    lcd.print("");
                }
            }
        }
        if(countenter == 3){
            currentPassword = "";
            confirmPassword = "";
            Locker_State = 0;
            currentLocker = "";
            countenter = 0;
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Choose Locker : ");
            lcd.setCursor(0,1);
            lcd.print("");
        }
    }else if(Locker_State == 2){
        
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Confirm Password : ");
        lcd.setCursor(0,1);
        lcd.print("");
        if (key >= '0' && key <= '9'){
                if(confirmPassword.length()<8){
                    if (confirmPassword.length() < MAX_PASSWORD_LENGTH) {
                        confirmPassword += key;
                        con_handlePasswordInput(key);
                    }
                }
        }else if(key == 'A'){
            if(confirmPassword == currentPassword){
                Locker_State = 3;
                countcon = 0 ;
                confirmPassword = "";
                currentPassword = "";
                play_Correct_sound();
                key = 'Z';
                handleKeypadInput(key);

            }else{
                lcd.clear();
                lcd.setCursor(0,0);
                lcd.print("Confirm Password : ");
                lcd.setCursor(0,1);
                lcd.print("");
                lcd.setCursor(0,2);
                lcd.print("Wrong! Try agian");
                play_Incorrect_sound();
                countcon += 1 ;
                confirmPassword = "";

            }
            
        }else if(key == 'B'){
            if(confirmPassword.length()>1){
                confirmdeleteLastCharacter();
                con_handlePasswordInput(key);
            }else{
                confirmdeleteLastCharacter();
                lcd.setCursor(0,1);
                lcd.print("");
            }
        }

        if(countcon == 3){
            Locker_Password[IntcurrentLocker-1] = "";
            currentPassword = "";
            confirmPassword = "";
            Locker_State = 0;
            countcon = 0;
            currentLocker = "";
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("Choose Locker : ");
            lcd.setCursor(0,1);
            lcd.print("");
        }
    }else if(Locker_State == 3){
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("OPEN LOCKER ");
        lcd.setCursor(0,1);
        lcd.print("NUMBER ");
        lcd.print(currentLocker);
        sendOpenLockerStatus();
        Locker_CountEnter[IntcurrentLocker-1] +=1;
        Locker_OC_Status[IntcurrentLocker-1] = "1";
        if(Locker_CountEnter[IntcurrentLocker-1] == 2){
          Locker_Password[IntcurrentLocker-1] = "";
          Locker_OC_Status[IntcurrentLocker-1] = "0";
          Locker_CountEnter[IntcurrentLocker-1] = 0;
        }
        play_Correct_sound();
        currentLocker = "";
        currentPassword = "";
        confirmPassword = "";
        delay(2000);
        Locker_State = 0;
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Choose Locker : ");
        lcd.setCursor(0,1);
        lcd.print("");

    }

}


void deleteLastCharacter() {
  // Delete the last character from the password and update the display
    if (currentPassword.length() > 0) {
        currentPassword.remove(currentPassword.length() - 1, 1);
        
    }
}

void confirmdeleteLastCharacter(){
    if (confirmPassword.length() > 0) {
        confirmPassword.remove(confirmPassword.length() - 1, 1);
    
    }
}

void LockerdeleteLastCharacter(){
    if (currentLocker.length() > 0) {
        currentLocker.remove(currentLocker.length() - 1, 1);
    
    }
}

void play_Correct_sound() {
    tone(buzzerPin,1396);  // F6
    delay(200);
    noTone(buzzerPin);
}

// Function to play a "FALSE" tone
void play_Incorrect_sound() {
    tone(buzzerPin,330);  // E4
    delay(200);
    noTone(buzzerPin);
}

void setup()
{
    Serial.begin(115200);
    SPI.begin();
    rfid.PCD_Init();
    setupWiFi();
    setupWebServer();
    ElegantOTA.begin(&webServer);
    pinMode(buzzerPin, OUTPUT);
    
    // initialize the LCD
    lcd.begin();
    for (int i = 0; i < NumberOfLocker; ++i) {
        Locker_Password[i] = "";
        Locker_OC_Status[i] = "0";
        Locker_CountEnter.push_back(0);
        
    }
    // Turn on the blacklight and print a message.
    
    lcd.clear();
    lcd.backlight();
    lcd.setCursor(0,0);
    lcd.print("Choose Locker : ");
    lcd.setCursor(0,1);
    lcd.print("");
    if (WiFi.status() == WL_CONNECTED){
        setupMqttClient();
    }
    
    
 
}
void loop() {

    readRFID();
    char key = keypad.getKey();
    if(key){
        handleKeypadInput(key);
    }

    
    if(Card_State == 1){
        Locker_State = 3;
        currentPassword = "";
        countenter = 0;
        confirmPassword = "";
        Card_State = 0;
        Locker_CountEnter[currentLocker.toInt()-1] = 1;
        play_Correct_sound();
        key = 'Z';
        handleKeypadInput(key);
    }
    
    webServer.handleClient();
    ElegantOTA.loop();
    if (WiFi.status() == WL_CONNECTED){
        mqttClient.loop();
    }
}