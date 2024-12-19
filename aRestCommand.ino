
// Import required libraries
#include <ESP8266WiFi.h>
#include <aREST.h>
#include "Servo.h"
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <AFArray.h>
#include <RGBLed.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoWebsockets.h>
#include <ArduinoJson.h>

// Create aREST instance
using namespace websockets;
aREST rest = aREST();
WebsocketsClient socketClient;
// WiFi parameters
const char* ssid = "veda";
const char* password = "gn002dynames";

// The port to listen for incoming TCP connections
#define LISTEN_PORT           80
// Create an instance of the server
WiFiServer server(LISTEN_PORT);

// Variables to be exposed to the API
// global variable
String boardKey="";
String serverURL="http://192.168.1.211:8080";
String portConnect="";
boolean socketActive=false;
int socketStart=0;
int socketTimeout=0;

WiFiClient client;
HTTPClient http;
// global operations
boolean uploadMode;
String queryData="";
boolean backgroundRunning;
boolean debug=false;

// global methods 
String servoMove(Servo servo,int pin,int start,int move,int gap,int between,int loop,boolean detach=false){
  if(gap<0){
    gap=1000;
  }
  if(between<0){
    between=5000;
  }
  if(loop<=0){
    return "loop must be bigger than 0";
  }
  if(start<0){
    return "start position missing";
  }
  
  //servo.attach(pin);
  servo.attach(pin,500,2400);
  if(!servo.attached()){
    return "Servo pin does not exist";
  }
  servo.write(start);
  delay(gap);
  for(int i=0; i<loop; i++){
  servo.write(move);
  delay(between);
  servo.write(start);
  delay(gap);
  }
  if(!detach){
    servo.detach();
  }
  return "OK";
}
// send http get request
String httpRequestGet(String url){
  String response="{}";
  http.begin(client,url);
  int httpResponseCode = http.GET();
  if(httpResponseCode==200){
    response=http.getString();
  }
  http.end();
  return response;
}

JsonDocument stringToJson(String jsonString){
  JsonDocument json;
  deserializeJson(json,jsonString);
  return json;
}
struct backgroundVariables{
  int x=1;
  int output=255;
  unsigned long d1;
  unsigned long d2;
  // fade
  int brightness = 0;
  //variables to hold our color intensities and direction
  //and define some initial "random" values to seed it
  int red=254;
  int green=1;
  int blue=127;
  int red_direction= -1;
  int green_direction= 1;
  int blue_direction= -1;
  int count=0;
};
// background task to run
struct backgroundTask{
  String device="";
  String method="";
  int pin=0;
  boolean rgb=false;
  long interval=100;
  // rgb pins
  int rgbRed=0;
  int rgbGreen=0;
  int rgbBlue=0;
  String rgbType;
  //
  int deduction=5;
  int runTarget=0;
  int* rgbSet[3];
  // used for server connection
  boolean background=false;
  //
  backgroundVariables variables;
};
AFArray<backgroundTask> queue;
AFArray<int> deleteQueue;
backgroundTask task={};
// add task
int addTask(String device,String method,int pin,int interval,int deduction=5,int start=1,int output=255,int target=0){
  int index=0;
  int rgbPins[3]={0,0,0};
  boolean exist=false;
  for(int i=0; i<queue.size(); i++){
    backgroundTask t=queue[i];
    if(t.device==device&&t.pin==pin){
      exist=true;
      queue[i]={device,method,pin,false,interval,rgbPins[0],rgbPins[1],rgbPins[2],"",deduction,target};
      queue[i].variables.x=start;
      queue[i].variables.output=output;
      break;
    }
  }
  if(!exist){
    backgroundTask task={device,method,pin,false,interval,rgbPins[0],rgbPins[1],rgbPins[2],"",deduction,target};
    task.variables.x=start;
    task.variables.output=output;
    queue.add(task);
    index=queue.size()-1;
    Serial.println("tasked added "+device+" "+method);
  }
  return index;
}
// add task RGB
int addTaskRgb(String device,String method,String rgbType,int rgbPins [],int rgbSet [3],int interval,int deduction=5,int target=0){
  int index=0;
  boolean exist=false;
  for(int i=0; i<queue.size(); i++){
    backgroundTask t=queue[i];
    if(t.device==device&&t.rgbRed==rgbPins[0]&&t.rgbGreen==rgbPins[1]&&t.rgbBlue==rgbPins[2]&&t.rgbType==rgbType){
      exist=true;
      queue[i]={device,method,0,true,interval,rgbPins[0],rgbPins[1],rgbPins[2],rgbType,deduction,target,rgbSet};
      break;
    }
  }
  if(!exist){
    backgroundTask task={device,method,0,true,interval,rgbPins[0],rgbPins[1],rgbPins[2],rgbType,deduction,target,rgbSet};
    queue.add(task);
    index=queue.size()-1;
    Serial.println("tasked added "+device+" "+method);
  }
  return index;
}

void removeByIndex(int index){
  if(queue.size()>0){
    backgroundTask test=queue[index];
    int startIndex=index;
    // if the index is the last element minus it
    if(queue.size()-1==startIndex){
      startIndex=index-1;
    }
    int indexPlus=index+1;
    int endQueue=queue.size()-1;
    if(queue.size()==1){
      queue.reset();
    }else if(queue.size()>1){
      
      AFArray<backgroundTask> splitA=queue.slice(0,startIndex);
      AFArray<backgroundTask> splitB=queue.slice(indexPlus+1,endQueue);
      Serial.println(splitA.size());
      Serial.println(splitB.size());
      queue=splitA+splitB;
    }
  }
}
// remove device from queue
AFArray<backgroundTask> removeDeviceTask(String device){
  AFArray<backgroundTask> newQueue;
  AFArray<backgroundTask> deviceTasks;
  for(int i=0; i<queue.size(); i++){
    backgroundTask t=queue[i];
    if(t.device!=device){
      newQueue.add(t);
    }else{
      deviceTasks.add(t);
    }
  }
  queue=newQueue;
  return deviceTasks;
}
void clearQueue(){
  queue.reset();
}
void countTimes(int index){
  if(task.variables.count==task.runTarget&&task.runTarget!=0){
      deleteQueue.add(index);
  }
  task.variables.count++;
}
void ledOnTimed(int pin,long interval,int index){
  digitalWrite(pin,task.variables.x);
  task.variables.d1,task.variables.d2;
  task.variables.d2=millis();
  if ( task.variables.d2-task.variables.d1 >= interval){
    task.variables.x=1-task.variables.x;
    task.variables.d1=millis();
    digitalWrite(pin,task.variables.x);
    countTimes(index);
  }
  task.variables.d1,task.variables.d2;
}
void ledFade(int pin,long interval,int index){
  task.variables.d1,task.variables.d2;
  task.variables.d2=millis();
  if ( task.variables.d2-task.variables.d1 >= interval){
    task.variables.x=1-task.variables.x;
    task.variables.d1=millis();
    task.variables.brightness+=task.deduction;
    if(task.variables.brightness<=0||task.variables.brightness>=255){
      task.deduction=-task.deduction;
    }
    analogWrite(pin,task.variables.brightness);
    countTimes(index);
  }
  task.variables.d1,task.variables.d2;
}
void ledBlinkBackGround(int pin,long interval,int index){
  task.variables.d1,task.variables.d2;
  task.variables.d2=millis();
  if ( task.variables.d2-task.variables.d1 >= interval){
    task.variables.x=1-task.variables.x;
    task.variables.d1=millis();
    digitalWrite(pin,task.variables.x);
    countTimes(index);
  }
  task.variables.d1,task.variables.d2;
}
void ledBlinkRgb(int pin,long interval,int powerPin,int index){
  task.variables.d1,task.variables.d2;
  task.variables.d2=millis();
  if ( task.variables.d2-task.variables.d1 >= interval){
    task.variables.x=1-task.variables.x;
    task.variables.d1=millis();
    //digitalWrite(pin,task.x);
    digitalWrite(powerPin,task.variables.x);
    countTimes(index);
  }
   task.variables.d1,task.variables.d2;
}
void rgbFade(int interval,int index){
  task.variables.d1,task.variables.d2;
  task.variables.d2=millis();
  if ( task.variables.d2-task.variables.d1 >= interval){
    task.variables.x=1-task.variables.x;
    task.variables.d1=millis();
    int pins[3]={task.rgbRed,task.rgbGreen,task.rgbBlue};
    int output[3]={task.variables.red,task.variables.green,task.variables.blue};
    setRGBLightFade(task.rgbType,pins,output);
    countTimes(index);
  }
   task.variables.d1,task.variables.d2;
}
void rgbCycle(int interval,int index){
  task.variables.d1,task.variables.d2;
  task.variables.d2=millis();
  if ( task.variables.d2-task.variables.d1 >= interval){
    task.variables.x=1-task.variables.x;
    task.variables.d1=millis();
    task.variables.red = task.variables.red + task.variables.red_direction;   //changing values of LEDs
    task.variables.green = task.variables.green + task.variables.green_direction;
    task.variables.blue = task.variables.blue + task.variables.blue_direction;
    //now change direction for each color if it reaches 255
    if (task.variables.red >= 255 || task.variables.red <= 0)
    {
      task.variables.red_direction = task.variables.red_direction * -1;
    }
    if (task.variables.green >= 255 || task.variables.green <= 0)
    {
      task.variables.green_direction = task.variables.green_direction * -1;
    }
    if (task.variables.blue >= 255 || task.variables.blue <= 0)
    {
      task.variables.blue_direction = task.variables.blue_direction * -1;
    }
    int pins[3]={task.rgbRed,task.rgbGreen,task.rgbBlue};
    int output[3]={task.variables.red,task.variables.green,task.variables.blue};
    setRGBLight(task.rgbType,pins,output);
    countTimes(index);
  }
  if(debug){
    Serial.println("rgbCycle executed");
  }
  task.variables.d1,task.variables.d2;
}
// internal schedule
// request settings at interval times
void requestConnection(int interval){
  task.variables.d1,task.variables.d2;
  task.variables.d2=millis();
  if ( task.variables.d2-task.variables.d1 >= interval){
    String request=httpRequestGet(serverURL+"/");
    
  }
  task.variables.d1,task.variables.d2;
}
// open websocket connection within certain time
void openSocketConnection(){

}
// close websocket connection within certain time
void closeSocketTime(){

}

// led blink function
int x = 1;
unsigned long d1, d2;
void ledBlink(int pin,long interval){
  d1,d2;
  pinMode(pin, OUTPUT);
  d2=millis();
  if (d2-d1 >= interval){
    x=1-x;
    d1=millis();
    digitalWrite(pin,x);
  }
  d1,d2;
}
// calculate time for mins
int calculateTime(int t){
 int min=1000*60;
 return min*t;
}

// websocket function
void startConnection(String url){
  socketClient.connect(url);
}
void sendMessage(){

}
void closeConnection(){
  socketClient.close();
}

// Declare functions to be exposed to the API
// Setting Routes
int uploadModeConfig(String command){
  int r=0;
  if(command=="true"){
    uploadMode=true;
    r=1;
  }
  if(command=="false"){
    uploadMode=false;
    r=1;
  }
  return r;
}
int backgroundrun(String command){
  backgroundRunning=true;
  return 1;
}


// devices
int ledPin=D2;

String writeBackgroundArray(){
  String array="[";
    if(queue.size()>0){
      for(int i=0; i<queue.size(); i++){
        backgroundTask t=queue[i];
          if(t.device!=""){
            String json="{";
            json=json+"device:"+t.device+"~";
            json=json+"method:"+t.method+"~";
            json=json+"rgb:"+t.rgb;
            json=json+"}";
            if(array.equals("[")){
              array=array+json;
            }else{
              array=array+"|"+json;
            }
          }
      }
    }
    array=array+"]";
    return array;
}
// return method and param
String * returnMethodandParam(String method){
  struct query{
    String query;
    String value;
  };
  String seperator=" ";
  if(method.indexOf('&')>-1){
    seperator="&";
  }
  static String arr[2];
  int indexA=method.indexOf(seperator);
  int indexB=method.length();
  String m=method.substring(0,indexA);
  Serial.println(m);
  String p=method.substring(indexA+1,indexB);
  Serial.println(p);
  arr[0]=m;
  arr[1]=p;
  return arr;
}
// return boolean from string
boolean stringToBool(String boo){
  boolean res=false;
  if(boo.startsWith("true")){
    res=true;
  }
  return res;
}
// return pin from string
int stringToPinIntDig(String pin){
  int pinR=D0;
  struct digpin{
    String pinString;
    int pin;
  };
  digpin arr[]={
  {"D0",D0},
  {"D1",D1},
  {"D2",D2},
  {"D3",D3},
  {"D4",D4},
  {"D5",D5},
  {"D6",D6},
  {"D7",D7},
  {"D8",D8}
  };
  int length = sizeof(arr) / sizeof(arr[0]);
  for(int i=0; i<length; i++){
    if(pin.startsWith(arr[i].pinString)){
      pinR=arr[i].pin;
      break;
    }
  }
  //Serial.println(pinR);
  return pinR;
}
void setPinMode(int pins [],uint8_t mode){
  int length=sizeof(pins) / sizeof(pins[0]);
  for(int i=0; i<length; i++){
    int pin=pins[i];
    pinMode(pin,mode);
  }
}
void setRGBLight(String type,int pins [],int output[]){
  if(type.equals("COMMON_ANODE")){
      RGBLed led(pins[0],pins[1],pins[2],RGBLed::COMMON_ANODE);
      led.setColor(output[0],output[1],output[2]);
    }
    if(type.equals("COMMON_CATHODE")){
      setPinMode(pins,OUTPUT);
      RGBLed led(pins[0],pins[1],pins[2],RGBLed::COMMON_CATHODE);
      led.setColor(output[0],output[1],output[2]);
    }
}
void setRGBLightFade(String type,int pins [],int output[]){
  if(type.equals("COMMON_ANODE")){
      RGBLed led(pins[0],pins[1],pins[2],RGBLed::COMMON_ANODE);
      led.fadeIn(output[0],output[1],output[2],task.deduction,task.interval);
      led.fadeOut(output[0],output[1],output[2],task.deduction,task.interval);
    }
    if(type.equals("COMMON_CATHODE")){
      setPinMode(pins,OUTPUT);
      RGBLed led(pins[0],pins[1],pins[2],RGBLed::COMMON_CATHODE);
      led.fadeIn(output[0],output[1],output[2],task.deduction,task.interval);
      led.fadeOut(output[0],output[1],output[2],task.deduction,task.interval);
    }
}
String returnParameterValue(String keyValue){
  String value="";
  int equalIndex=keyValue.indexOf('=');
  value=keyValue.substring(equalIndex+1);
  return value;
}
// return each param variable in array
AFArray<String> parameterArray(int paramsSize,String param){
  AFArray<String> arr;
  String split=param;
  boolean formula=true;
  String seperator=" ";
  Serial.println(param);
  if(param.indexOf('&')>-1){
    seperator="&";
    formula=false;
  }
  for(int i=0; i<paramsSize; i++){
    if(i==paramsSize-1&&!split.startsWith(seperator)){
      split.trim();
      arr.add(split);
      Serial.println(split);
      break;
    }
    int spaceIndex=split.indexOf(seperator);
    int sumA=1;
    if(!formula){
      sumA=0;
    }
    String variable=split.substring(0,spaceIndex+sumA);
    variable.trim();
    Serial.println(variable);
    arr.add(variable);
    split=split.substring(spaceIndex+1,split.length());
 }
 
  Serial.println("split end "+split);
  return arr;
}

// set led brightness
// postive pin,brightness
int setLED(String param){
  AFArray<String> paramArr=parameterArray(2,param);
  int r=0;
  if(paramArr.size()==2){
    int positivePin=stringToPinIntDig(paramArr[0]);
    int pins[1]={positivePin};
    setPinMode(pins,OUTPUT);
    int brightness=paramArr[1].toInt();
    analogWrite(positivePin, brightness);
    r=1;
  }
  return r;
}
// set led animation
// method,device name,postive pin,delay,deduction,start,output,target
int setLEDAnimation(String param){
  int r=0;
  AFArray<String> paramArr=parameterArray(8,param);
  if(paramArr.size()==8){
    String method=paramArr[0];
    String deviceName=paramArr[1];
    int positivePin=stringToPinIntDig(paramArr[2]);
    int delay=paramArr[3].toInt();
    int deduction=paramArr[4].toInt();
    int start=paramArr[5].toInt();
    int output=paramArr[6].toInt();
    int target=paramArr[7].toInt();
    int pins[1]={positivePin};
    setPinMode(pins,OUTPUT);
    addTask(deviceName,method,positivePin,delay,deduction,start,output,target);
    r=1;
  }
  return r;
}
// set rgb animation
// method,device name,rgb type,red pin,green pin,blue pin,delay,set red,set green,set blue
int setRGBAnimation(String param){
  int r=0;
  AFArray<String> paramArr=parameterArray(11,param);
  if(paramArr.size()==11){
    String method=paramArr[0];
    String deviceName=paramArr[1];
    String type=paramArr[2];
    int rPin=stringToPinIntDig(paramArr[3]);
    int gPin=stringToPinIntDig(paramArr[4]);
    int bPin=stringToPinIntDig(paramArr[5]);
    int interval=paramArr[6].toInt();
    int red=0;
    int green=0;
    int blue=0;
    int redSet=paramArr[7].toInt();
    int greenSet=paramArr[8].toInt();
    int blueSet=paramArr[9].toInt();
    int target=paramArr[10].toInt();
    if(redSet>0) red=redSet;
    if(greenSet>0) green=greenSet;
    if(blueSet>0) blue=blueSet;
    int pins[3]={rPin,gPin,bPin};
    int setRgb[3]={red,green,blue};
    addTaskRgb(deviceName,method,type,pins,setRgb,interval,0,target);
    r=1;
  }
  return r;
}
// type COMMON_ANODE or COMMON_CATHODE,red pin,green pin,blue pin,red,green,blue
int setRGB(String param){
  int r=0;
  AFArray<String> paramArr=parameterArray(7,param);
  if(paramArr.size()==7){
    String type=paramArr[0];
    int rPin=stringToPinIntDig(paramArr[1]);
    int gPin=stringToPinIntDig(paramArr[2]);
    int bPin=stringToPinIntDig(paramArr[3]);
    // colour
    int red=paramArr[4].toInt();
    int green=paramArr[5].toInt();
    int blue=paramArr[6].toInt();
    int pins[3]={rPin,gPin,bPin};
    int output[3]={red,green,blue};
    setRGBLight(type,pins,output);
    r=1;
  }
  return r;
}
// move servo
// pin,start position,move,begining delay,delay gap between,repeat,detach
int moveServo(String param){
  int r=0;
  AFArray<String> paramArr=parameterArray(7,param);
  if(paramArr.size()==7){
    Servo servo;
    int pin=stringToPinIntDig(paramArr[0]);
    int start=paramArr[1].toInt();
    int move=paramArr[2].toInt();
    int gap=paramArr[3].toInt();
    int between=paramArr[4].toInt();
    int repeat=paramArr[5].toInt();
    boolean detach=false;
    if(paramArr[6].equals("enable")) detach=true;
    servoMove(servo,pin,start,move,gap,between,repeat,detach);
    r=1;
  }
  return r;
}
// clear command
// method clear method,param
int clearQueueAction(String param){
  int r=0;
  String* arr=returnMethodandParam(param);
  String method=arr[0];
  String para=arr[1];
  if(method=="clear"){
    clearQueue();
    r=1;
  }
  if(method=="removedevicetask"){
    removeDeviceTask(para);
    r=1;
  }
  return r;
}
// query data
int getDeviceData(String command){
  int r=0;
  struct query{
    String query;
    String data;
  };
  queryData="";
  query arr[]={
    {"background",writeBackgroundArray()}
  };
  int length = sizeof(arr) / sizeof(arr[0]);
  for(int i=0; i<length; i++){
    if(command.equals(arr[i].query)){
      queryData=arr[i].data;
      r=1;
      break;
    }
    if(arr[i].query.equals("1")&&command.equals("")){
      queryData=arr[i].data;
      r=1;
      break;
    }
  }
  return r;
}


// send http requests when booting up
void startRequest(){
  // local api
  String ip=WiFi.localIP().toString();
  // launch task
  /*
  int length = sizeof(deviceArr) / sizeof(deviceArr[0]);
  String requestUrl=serverURL+"/schedule/device-startup";
  for(int i=0; i<length; i++){
    deviceS device=deviceArr[i];
    int idArrLength=sizeof(device.scheduleStart) / sizeof(device.scheduleStart[0]);
    if(idArrLength>0){
      for(int x=0; x<idArrLength; x++){
        String id=device.scheduleStart[x];
        Serial.println(id);
        if(id!=""){
          http.begin(client,requestUrl);
          http.addHeader("Content-Type", "application/json");
          int httpResponseCode = http.POST("{\"api_key\":\"tPmAT5Ab3j7F9\",\"id\":\""+id+"\"}");
          Serial.println(httpResponseCode);
          http.end();
        }
      }
    }
  }
  */
}

void setup(void)
{
  // Start Serial
  Serial.begin(115200);
  // Init variables and expose them to REST API
  backgroundRunning=false, uploadMode=false;
  queryData="";
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    pinMode(2,OUTPUT); 
    digitalWrite(2,0); 
    delay(500);
    Serial.print(".");
  }
  //
  // wireless upload
  ArduinoOTA.setPassword((const char *)"123");
   ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    pinMode(2,OUTPUT); 
    for(int a=0; a<3; a++){
      ledBlink(2,900);
    }
    pinMode(2,INPUT); 
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  //
  Serial.println("");
  Serial.println("WiFi connected");
  digitalWrite(2,1);
  pinMode(2,INPUT); 
  // Start the server
  server.begin();
  ArduinoOTA.begin();
  Serial.println("Server started");

  // Print the IP address
  Serial.println(WiFi.localIP());
  
  startRequest();
  // device framework
  rest.variable("background",&backgroundRunning);
  rest.variable("query",&queryData);
   // device readings
  // Function to be exposed
  // config route
  rest.function("query",getDeviceData);
  rest.function("upload",uploadModeConfig);
  rest.function("clear-queue",clearQueueAction);
  // routes
  rest.function("set-led",setLED);
  rest.function("led-animation",setLEDAnimation);
  rest.function("set-rgb",setRGB);
  rest.function("rgb-animation",setRGBAnimation);
  rest.function("set-servo",moveServo);
  // Give name & ID to the device (ID should be 6 characters long)
  rest.set_id("In3|6");
  rest.set_name("aREST Command");
  
}

void loop() {
  // Reset server when upload mode true
  if(uploadMode==true){
    pinMode(2,OUTPUT); 
    ledBlink(2,900);
    ArduinoOTA.handle();
  }else{
  // Handle REST calls
  WiFiClient client = server.available();
  // Reset when cannot connect to wifi
  // Turn on D2 on board LED on when down
  if(WiFi.status()!= WL_CONNECTED){
    pinMode(2,OUTPUT); 
    digitalWrite(2,0);
  }else{
    digitalWrite(2,1);
    pinMode(2,INPUT); 
  }
  
  background();
  if (!client) {
    return;
  }
  while(!client.available()){
    delay(1);
  }
  rest.handle(client);
  }
}
// background processes 
void background(){
  // automatic
  if(queue.size()>0){
    backgroundRunning=true; 
  }else{
    backgroundRunning=false;
    task={};
  }
  
  if(backgroundRunning==true){
    
    for(int i=0; i<queue.size(); i++){
      task=queue[i];

      if(task.method=="ledblinkbackground"){
        ledBlinkBackGround(task.pin,task.interval,i);
        queue[i]=task;
      }
      if(task.method=="ledfade"){
        ledFade(task.pin,task.interval,i);
        queue[i]=task;
      }
      if(task.method=="ledlighttimed"){
        ledOnTimed(task.pin,task.interval,i);
        queue[i]=task;
      }
      if(task.rgb){
        if(task.method=="ledblinkrgb"){
          ledBlinkRgb(task.rgbRed,task.interval,task.pin,i);
          queue[i]=task;
        }
        if(task.method=="rgbcycle"){
          rgbCycle(task.interval,i);
          queue[i]=task;
        }
        if(task.method=="rgbfade"){
          rgbFade(task.interval,i);
          queue[i]=task;
        }
      }
    }
    if(deleteQueue.size()>0){
      for(int dq=0; dq<deleteQueue.size(); dq++){
        removeByIndex(deleteQueue[dq]);
      }
      deleteQueue.reset();
    }
    
  }
}








