/*
RFID PROJECT
BY TODD CARPER
EDIT BY VEMA REDDY
*/

// Needed Library
#include <SPI.h>         // needed for Arduino versions later than 0018
#include <Ethernet.h>
#define UDP_TX_PACKET_MAX_SIZE 256
#include <MFRC522.h>
#include <EEPROM.h>
#include <TrueRandom.h>
#include "ArduinoJson.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1331.h>
#include <rBase64.h>

/*
GLOBAL VARIABLES *******************************************************
 */

#define READER 1
#define DOOR 2
#define LOCKER 3
#define RELAY 4
#define RGBLED 5
#define ADDRESSABLERGBLED 6

#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define WHITE 0xFFFF

// MAC Address
byte mac[6] = { 0x90, 0xA2, 0xDA, 0x00, 0x00, 0x00 };

StaticJsonBuffer<256> jsonBuffer;
// StaticJsonBuffer<256> jsonBuffer2;

bool registered = false;

unsigned int LOCALPORT = 4547;      // local port to listen on
unsigned int SERVERPORT = 4545;
unsigned int MODULEPORT = 4546;

// buffers for receiving and sending data
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];  //buffer to hold incoming packet,

// An EthernetUDP instance to let us send and receive packets over UDP
EthernetUDP Udp;

char sendingBuffer[100];
IPAddress remoteServerIP;

/*
  MEGA  RFID
  RST/REST 5
  SPI SS 53
  SPI MOSI 51
  SPI MISO  50
  SPI CSK 52
*/

// for the RFID Reader
#define RST_PIN 5
#define SS_PIN 53
MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

#define DEBUG 0

#if DEBUG == 1
byte    ourUID[10];
#endif

char    macstr[18];

int moduleType = 2;

// door lock
// constexpr uint8_t RST_PIN = 20;
constexpr uint8_t RFID_SS_PIN = 21;
constexpr uint8_t ETHERNET_SS_PIN = 10;
constexpr uint8_t DOOR_LATCH = 7;
constexpr uint8_t RELAY_LATCH = 7;


/*  
  MEGA OLED
  GND GND
  5V
  SCL (CLK of SPI) D13
  SDA (mosi) D11
  Reset D9
  DC D8
  CD D10
*/

#define sclk 13
#define mosi 11
#define cs   10
#define rst  9
#define dc   8

Adafruit_SSD1331 display = Adafruit_SSD1331 (cs, dc, mosi, sclk, rst); // (cs, dc, rst); 
//*********************************************************************************************
void setup() {
 // pinMode(cs, OUTPUT);
  // Store MAC address in EEPROM or read it back
  if (EEPROM.read(1) == '#') {
    for (int i = 3; i < 6; i++) {
      mac[i] = EEPROM.read(i);
    }
  } else {
    for (int i = 3; i < 6; i++) {
      mac[i] = TrueRandom.randomByte();
      EEPROM.write(i, mac[i]);
    }
    EEPROM.write(1, '#');
  }
  snprintf(macstr, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // start the Ethernet and UDP:
  Ethernet.begin(mac); // mac
  Udp.begin(LOCALPORT);

  SPI.begin();      // Init SPI bus
  mfrc522.PCD_Init();   // Init MFRC522

#if DEBUG == 1
  Serial.begin(9600);
  Serial.println("Starting");
  Serial.println(macstr);
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details
#endif

  display.begin();
  display.fillScreen(WHITE);
  
  switch (moduleType) {
    case DOOR:
    case LOCKER: {
      centerMessage("Starting up...", BLUE, 1);
      }
      break;
  }
}
//*************************************************************************************
#if DEBUG == 1
void printHex(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
  Serial.println(" ");
}
#endif
//#####################################################################################
void loop() {
  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    remoteServerIP = Udp.remoteIP();
    Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    packetBuffer[packetSize] = 0;

#if DEBUG == 1
    for (int i = 0; i < 4; i++) {
      Serial.print(remoteServerIP[i], DEC);
      if (i < 3) {
        Serial.print(".");
      }
    }
    Serial.print(", port ");
    Serial.println(Udp.remotePort());
    Serial.println("Contents:");
    Serial.println(packetBuffer);
#endif
    
    JsonObject& jsonItem = jsonBuffer.parseObject(packetBuffer);

    if (! registered) {
      if (jsonItem.containsKey("RISE")) {
        registered = true;
        jsonBuffer.clear();
        JsonObject& msgToServer = jsonBuffer.createObject();
        msgToServer["COMMAND"] = "REGISTER";
        msgToServer["MODULEID"] = macstr;
        msgToServer["DEVICETYPE"] = moduleType;  // get from DIP

        msgToServer.printTo(sendingBuffer, sizeof(sendingBuffer));  // get the JSON string
        #if DEBUG == 1
          Serial.println(sendingBuffer);
       #endif
        Udp.beginPacket(remoteServerIP, SERVERPORT);
        Udp.write(sendingBuffer);
        Udp.endPacket();
        Udp.stop(); // close out the server listener port for IP

        Udp.begin(MODULEPORT);  // switch away from the IP broadcast port

        readyMessage(10);
        /*
           http://arduinojson.org/doc/encoding/

            // send a reply to the IP address and port that sent us the packet we received
            Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
            Udp.write(ReplyBuffer);
            Udp.endPacket();
        */
      }
    } else {
      // we already have the server IP, so this should be a command coming in - deal with it
      if (jsonItem.containsKey("DIRECTIVE")) {

        String ack = jsonItem["DIRECTIVE"];

        if (ack == "OK") {
          String configString = jsonItem["Configuration"];
          
          JsonObject& jsonConfig = jsonBuffer.parseObject(configString);

          switch (moduleType) {
            case READER: {


              }
              break;
            case LOCKER:
            case DOOR: {

                String operation = jsonConfig["Operation"];
                String fullName = jsonItem["Name"];
                String jsonImage = jsonItem["Image"];
                uint32_t imageLen = jsonImage.length();

                display.fillScreen(WHITE);

                if (fullName != "") {
                  display.setTextSize(1);
                  display.setTextColor(BLUE);
                  display.setCursor(10, 10);
                  display.print(fullName);
                }
                
                if (imageLen > 10) {
                  uint8_t theImageArray[imageLen];
                  jsonImage.getBytes(theImageArray, imageLen);

                  imageLen = rbase64_dec_len(theImageArray, imageLen);

                  String decodedImage = rbase64.decode(theImageArray, imageLen );
                  uint8_t theImage[imageLen];
                  decodedImage.getBytes(theImage, imageLen);
                  bmpDraw(theImage, 40, 40);
                }

                if (operation == "OPEN") {
                  digitalWrite(DOOR_LATCH, HIGH);
                  delay(5000);
                  digitalWrite(DOOR_LATCH, LOW);
                }

                clearScreen();
                readyMessage(2000);
              }
              break;

            case RELAY: {
                String operation = jsonConfig["Operation"];
                if (operation == "ON") {
                  digitalWrite(RELAY_LATCH, HIGH);
                }
                else {
                  digitalWrite(RELAY_LATCH, LOW);
                }
              }
              break;

            case RGBLED: {
                String operation = jsonConfig["Operation"];
                String color = jsonConfig["Color"];
                if (operation == "ON") {
                  digitalWrite(RELAY_LATCH, HIGH);
                }
                else {
                  digitalWrite(RELAY_LATCH, LOW);
                }
              }
              break;

            case ADDRESSABLERGBLED: {
                String operation = jsonConfig["Operation"];
                String pattern = jsonConfig["Pattern"];
                if (operation == "ON") {
                  digitalWrite(RELAY_LATCH, HIGH);
                }
                else {
                  digitalWrite(RELAY_LATCH, LOW);
                }
              }
              break;
          } // switch

          
        } // ack ?
        else {  // we have an error - no access allowed, what to do
          String message = jsonItem["Message"];

           // send an error message, flash lights?
          if (message.length() != 0) {
            centerMessage(message, RED, 1);
            readyMessage(2000);
          }    
        }
      } // contains directive
      else {  // we have an error - no access allowed, what to do
        String message = jsonItem["Message"];

         // send an error message, flash lights?
        if (message.length() != 0) {
          centerMessage(message, RED, 1);
          readyMessage(2000);
        }
      }
    } // registered
    //   delay(10);
  } else {  // no packet data so...
    // Look for new cards and send the RFID to the server - the server will decide what to do

    if ( ! mfrc522.PICC_IsNewCardPresent()) {
      return;
    }

    // Select one of the cards
    if ( ! mfrc522.PICC_ReadCardSerial()) {
      return;
    }

    // Dump debug info about the card; PICC_HaltA() is automatically called
    // mfrc522.PICC_DumpToSerial(&(mfrc522.uid));

    String ourUIDString = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      ourUIDString += mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ";
      ourUIDString += String(mfrc522.uid.uidByte[i], HEX);

#if DEBUG == 1
      ourUID[i] = mfrc522.uid.uidByte[i];
#endif

    }
    mfrc522.PICC_HaltA();

    ourUIDString.trim();

#if DEBUG == 1
    ourUID[mfrc522.uid.size] = 0;
    printHex(&ourUID[0], mfrc522.uid.size);
#endif

    // inform the server that a tag was swiped
    jsonBuffer.clear();
    JsonObject& msgToServer = jsonBuffer.createObject();
    msgToServer["COMMAND"] = "REQUEST";
    msgToServer["MODULEID"] = macstr;
    msgToServer["RFID"] = ourUIDString;

    msgToServer.printTo(sendingBuffer, sizeof(sendingBuffer));  // get the JSON string
 #if DEBUG == 1
    Serial.println(sendingBuffer);
 #endif
    Udp.beginPacket(remoteServerIP, SERVERPORT);
    Udp.write(sendingBuffer);
    Udp.endPacket();

  } // look at rfid
}
//##########################################################################################
//
void centerMessage(String message, int color, int textSize ) {
  // with textSize == 1
 // we have 16 capital chars across   6 pixels wide / character   96 total
 // we have 8 Capital characters high  8 pixels high / character  64 total

 clearMessage();
 
  int offsetX = (96-(message.length() * 6)) / 2;
  if (offsetX < 0) {
    offsetX = 0;
  }

  int offsetY =(64 -  (8 - 1) ) / 2;
  display.setTextSize(textSize);
  display.setTextColor(color);
  display.setCursor(offsetX, offsetY);
  display.print(message);
  
}

//
void clearScreen() {
  int startX = 0;
  int startY = 0;
display.fillScreen(WHITE);
  /*
  int maxX = startX + display.width();
  int maxY = startY + display.height();

 //  display.begin();
  for (int i = startX; i < maxX; i++) { // For each scanline..
    for (int j = startY; j < maxY; j++) {

      display.drawPixel(i, j, display.Color565(255,255,255));
      
  //    display.goTo(i, j);
  //    display.pushColor(display.Color565(255, 255, 0));
    }
  }
*/
}

//
void clearMessage() {
  int startX = 0;
  int startY = 18;
  int maxX = startX + 300;
  int maxY = startY + 20;
display.fillScreen(WHITE);
/*
 //  display.begin();
  for (int i = startX; i < maxX; i++) { // For each scanline..
    for (int j = startY; j < maxY; j++) {

      display.drawPixel(i, j, display.Color565(255,255,255));
      
  //    display.goTo(i, j);
  //    display.pushColor(display.Color565(255, 255, 0));
    }
  }
  */
}

//
void readyMessage(int delaySeconds) {
  delay(delaySeconds);
  centerMessage("Ready", BLUE, 1);
  
}

#define BUFFPIXEL 20

//
  void bmpDraw(uint8_t image[], uint8_t x, uint8_t y) {

    int      bmpWidth, bmpHeight;   // W+H in pixels
    uint32_t  offset = 0;
    uint8_t  bmpDepth;              // Bit depth (currently must be 24)
    uint32_t bmpImageoffset;        // Start of image data in file
    uint32_t rowSize;               // Not always = bmpWidth; may have padding
    uint8_t  sdbuffer[3 * BUFFPIXEL]; // pixel buffer (R+G+B per pixel)
    uint8_t  buffidx = sizeof(sdbuffer); // Current position in sdbuffer
    boolean  goodBmp = false;       // Set to true on valid header parse
    boolean  flip    = true;        // BMP is stored bottom-to-top
    int      w, h, row, col;
    uint8_t  r, g, b;
    uint32_t pos = 0, startTime = millis();

    if ((x >= display.width()) || (y >= display.height())) return;

    // Parse BMP header
    if (read16(image, offset) == 0x4D42) { // BMP signature
      offset += 2;
      #if DEBUG == 1
      Serial.print("File size: ");
      Serial.println(read32(image, offset));
      #endif
      offset += 4;
      (void)read32(image, offset); // Read & ignore creator bytes
      offset += 4;
      bmpImageoffset = read32(image, offset); // Start of image data
      offset += 4;
      #if DEBUG ==1 
      Serial.print("Image Offset: "); Serial.println(bmpImageoffset, DEC);
      // Read DIB header
      Serial.print("Header size: "); Serial.println(read32(image, offset));
      #endif
      offset += 4;
      bmpWidth  = read32(image, offset);
      offset += 4;
      bmpHeight = read32(image, offset);
      offset += 4;
      if (read16(image, offset) == 1) { // # planes -- must be '1'
        offset += 2;
        bmpDepth = read16(image, offset); // bits per pixel
        offset += 2;
        #if DBEUG == 1
        Serial.print("Bit Depth: "); Serial.println(bmpDepth);
        #endif
        if ((bmpDepth == 24) && (read32(image, offset) == 0)) { // 0 = uncompressed
          offset += 4;
          goodBmp = true; // Supported BMP format -- proceed!
          #if DBEUG == 1
            Serial.print("Image size: ");
            Serial.print(bmpWidth);
            Serial.print('x');
            Serial.println(bmpHeight);
          #endif

          // BMP rows are padded (if needed) to 4-byte boundary
          rowSize = (bmpWidth * 3 + 3) & ~3;

          // If bmpHeight is negative, image is in top-down order.
          // This is not canon but has been observed in the wild.
          if (bmpHeight < 0) {
            bmpHeight = -bmpHeight;
            flip      = false;
          }

          // Crop area to be loaded
          w = bmpWidth;
          h = bmpHeight;
          if ((x + w - 1) >= display.width())  w = display.width()  - x;
          if ((y + h - 1) >= display.height()) h = display.height() - y;

          for (row = 0; row < h; row++) { // For each scanline...
            display.goTo(x, y + row);

            // Seek to start of scan line.  It might seem labor-
            // intensive to be doing this on every line, but this
            // method covers a lot of gritty details like cropping
            // and scanline padding.  Also, the seek only takes
            // place if the file position actually needs to change
            // (avoids a lot of cluster math in SD library).
            if (flip) // Bitmap is stored bottom-to-top order (normal BMP)
              pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
            else     // Bitmap is stored top-to-bottom
              pos = bmpImageoffset + row * rowSize;
            if (offset != pos) { // Need seek?
              offset = pos;
              buffidx = sizeof(sdbuffer); // Force buffer reload
            }

            // optimize by setting pins now
            for (col = 0; col < w; col++) { // For each pixel...
              // Time to read more pixel data?
              if (buffidx >= sizeof(sdbuffer)) { // Indeed
                memcpy(sdbuffer, image, sizeof(sdbuffer));
                //       bmpFile.read(sdbuffer, sizeof(sdbuffer));
                buffidx = 0; // Set index to beginning
              }

              // Convert pixel from BMP to TFT format, push to display
              b = sdbuffer[buffidx++];
              g = sdbuffer[buffidx++];
              r = sdbuffer[buffidx++];

              //tft.drawPixel(x+col, y+row, tft.Color565(r,g,b));
              // optimized!
       //       display.drawPixel(x+col, y+row, display.Color565(r,g,b));
              display.pushColor(display.Color565(r, g, b));
            } // end pixel
          } // end scanline
          #if DEBUG == 1
            Serial.print("Loaded in ");
            Serial.print(millis() - startTime);
            Serial.println(" ms");
          #endif
        } // end goodBmp
      }
    }
    #if DBEUG == 1
      if (!goodBmp) Serial.println("BMP format not recognized.");
    #endif
  }

  // These read 16 - and 32-bit types from the SD card file.
  // BMP data is stored little-endian, Arduino is little-endian too.
  // May need to reverse subscript order if porting elsewhere.

  uint16_t read16(uint8_t image[], uint32_t offset) {
    uint16_t result;
    ((uint8_t *)&result)[0] = image[offset]; // LSB
    ((uint8_t *)&result)[1] = image[offset++]; // MSB
    return result;
  }

  uint32_t read32(uint8_t image[], uint32_t offset) {
    uint32_t result;
    ((uint8_t *)&result)[0] = image[offset]; // LSB
    ((uint8_t *)&result)[1] = image[offset++];
    ((uint8_t *)&result)[2] = image[offset++];
    ((uint8_t *)&result)[3] = image[offset++]; // MSB
    return result;
  }
