#include <SPI.h> // LoRa
#include <SD.h>  // SD card
#include <Wire.h> // I2C RTC & Rainfall Sensor
#include <RTClib.h> // DS3231 RTC
#include <LoRa.h> // LoRa
#include <DFRobot_RainfallSensor.h> // Rainfall Sensor

DFRobot_RainfallSensor_I2C Sensor(&Wire); // Initialize the Rainfall Sensor with the I2C interface

#define RFM95_FREQ 915E6  // Set the frequency to 915 MHz
RTC_DS3231 rtc; // Create an instance of the RTC_DS3231 class to interact with the RTC

const int LoRa_CS = 8; // Chip Select pin for LoRa module
const int LoRa_RST = 9; // Reset pin for LoRa module
const int LoRa_IRQ = 2; // Interrupt pin for LoRa module
const int LoRa_EN = ??;

const int SD_CS = 10; // Define the chip select pin for the SD card module

const int V_Batt = A2;  // V_Batt Analog input pin A2 1V1 max

bool debugMode = true; // Set to false to disable serial debug messages

//--------------SETUP ROUTINE--------------//
void setup() {
  Serial.begin(115200); // Begin Serial communication for debugging purposes
  while (!Serial);

  // Initialize SD Card
  Serial.print("Initializing SD card: ");
  if (!SD.begin(SD_CS)) {
    Serial.println("FAILED!");
    // while (true); // Stop the program if SD card can't be initialized
  }
  Serial.println("SUCCESS");

  // Initialize LoRa
  Serial.print("Initializing LoRa: ");
  pinMode(LoRa_EN, OUTPUT);
  digitalWrite(LoRa_EN, HIGH);
  LoRa.setPins(LoRa_CS, LoRa_RST, LoRa_IRQ);
  if (!LoRa.begin(RFM95_FREQ)) {
    Serial.println("FAILED!");
    return;
    // while (true); // Stop the program if LoRa initialization fails.
  }
  Serial.println("SUCCESS");

  // Initialize the RTC module.
  Serial.print("Initializing RTC: ");
  if (!rtc.begin()) {
    Serial.println("FAILED!");
    // while (true); // Stop the program if RTC can't be initialized.
  }
  Serial.println("SUCCESS");

  // Check if the RTC lost power and if so, reset the time.
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting the time to compile time."); 
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Set the RTC to the compile date & time.
  }

  DateTime now = rtc.now(); // Get the current time.
  DateTime nextHour(now.year(), now.month(), now.day(), now.hour() + 1, 0, 0); // Calculate when the start of the next hour is
  if (now.minute() == 0 && now.second() == 0) {   // Check if the current time is already at the start of an hour XX:00 (unlikely)
    nextHour = now; // Overwrite the above nextHour time to be now and initialize the sensor.
  }
  while (rtc.now() < nextHour) { // If the time is not already exactly XX:00, we must wait till nextHour occurs.
    delay(1000); // Wait 1 second
  }

  // Initialize the Rainfall Sensor.
  Serial.print("Initializing bucket: ");
  while (!Sensor.begin()) {
    Serial.println("FAILED!");
    delay(1000); // Retry after a delay.
  }
  Serial.println("SUCCESS");
  
  // Record the power-on time.
  powerOnTime = rtc.now();
}


//--------------SERIAL PRINT DEBUG--------------//
void debugPrint(const String &message) {
  if (debugMode) {
    Serial.print(message);
  }
}
void debugPrintln(const String &message) {
  if (debugMode) {
    Serial.println(message);
  }
}

//--------------READ BATTERY LEVEL--------------//
float readBatteryLevel() {
  // 10K/82K=1/9.2 Input divider
  // 1.092V*9.2=10.0464V Full scale - *1.092V is measured Vref
  // = 0.0099538143V/bit theory
  // V/bit seems to be most accurate
  const float VBattScale = 0.009766;
  float voltage = 0.0;  // Clear old value
  int avgSamples = 10;  // Number of samples averaged for analogue inputs
  for (int x = 0; x < avgSamples; x++) {  // Average multiple readings
    voltage = (analogRead(V_Batt) + voltage);
  }
  voltage = voltage / avgSamples;  // Calculate average
  voltage = voltage * VBattScale;  // Calculate battery voltage
  return voltage;
}

//--------------LOG TO SD--------------//
void writeToSD(const String& data) {
  File dataFile = SD.open("rainlog.txt", FILE_WRITE);
  if (dataFile) {
    dataFile.println(data);
    dataFile.close(); // Close the file to save the data
    debugPrintln("Write to SD: " + data);
  } else {
    debugPrintln("Error opening rainlog.txt");
  }
}

//--------------SEND DATA LORA--------------//
void transmitDataViaLoRa(uint32_t epochTime, float sensorWorkingTime, float hourlyRainfall, float dailyRainfall, int rawBucketTips, float batteryVoltage) {
  // Begin a new LoRa packet for transmission
  LoRa.beginPacket(); 
  LoRa.print(epochTime); // Send epoch time
  LoRa.print(",");
  LoRa.print(sensorWorkingTime); // Send sensor working time
  LoRa.print(",");
  LoRa.print(hourlyRainfall); // Send rainfall data from the last hour
  LoRa.print(",");
  LoRa.print(dailyRainfall); // Send rainfall data from the last 24 hours
  LoRa.print(",");
  LoRa.print(rawBucketTips); // Send bucket tips count
  LoRa.print(",");
  LoRa.print(batteryVoltage, 2);// Send battery level
  // End packet transmission and check if it was successful
  int transmissionStatus = LoRa.endPacket();
  if (transmissionStatus != 1) {
    debugPrintln("Error: LoRa transmission failed.");
  }
}

//--------------MAIN LOOP--------------//
void loop() {
  DateTime now = rtc.now();

  // Trigger the sensor reading at five-minute intervals
  // The modulus operator (%) is used to find the remainder of the division of the 
  // current minute by 5. If the remainder is 0, it means that the current minute 
  // is exactly divisible by 5 (i.e., it is a multiple of 5). This effectively checks 
  // if the current minute is one of the following: 0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, or 55.
  if (now.minute() % 5 == 0 && now.second() == 0) { 
    static int lastMinute = -1;  // Store the last minute we recorded
    
    // Check if this minute has already been processed
    if (lastMinute != now.minute()) {
      lastMinute = now.minute(); // Update lastMinute to prevent multiple readings

      // Convert the sensor reading time
      uint32_t epochTime = now.unixtime(); // Current epoch time
      
      // Collect data from the RTC and sensors
      float sensorWorkingTime = Sensor.getSensorWorkingTime();
      float hourlyRainfall = Sensor.getRainfall(1); // Rainfall in the last hour (range 1-24)
      float dailyRainfall = Sensor.getRainfall(); // Total rainfall since power on (deafult 24)
      int rawBucketTips = Sensor.getRawData(); // Count of bucket tips (0.28mm per tip)
      float batteryVoltage = readBatteryLevel(); // Get batery voltage (expecting above ~7V)

      // Print to serial when debugMode = true
      debugPrint("Epoch Time: "); debugPrint(String(epochTime));
      debugPrint(", Working Time: "); debugPrint(String(sensorWorkingTime) + " H");
      debugPrint(", Hourly Rainfall: "); debugPrint(String(hourlyRainfall) + " mm");
      debugPrint(", Total Rainfall: "); debugPrint(String(dailyRainfall) + " mm");
      debugPrint(", Bucket Tips: "); debugPrint(String(rawBucketTips));
      debugPrint(", Battery Level: "); debugPrintln(String(batteryVoltage) + " V");

      // Transmit data via LoRa
      transmitDataViaLoRa(epochTime, sensorWorkingTime, hourlyRainfall, dailyRainfall, rawBucketTips, batteryVoltage);
            
      // Convert individual data points to a single string and write to SD card
      String dataString = String(epochTime) + "," +
                    String(sensorWorkingTime) + "H," +
                    String(hourlyRainfall) + "mm," +
                    String(dailyRainfall) + "mm," +
                    String(rawBucketTips) + "," +
                    String(batteryVoltage) + "V";
      // Write the transmit data to SD in the same format
      writeToSD(dataString);
    }
  }

  delay(500); // Reduce loop frequency to save power
}
