#pragma once

#include "wled.h"
#include "battery_defaults.h"
#include "battery.h"
#include "unkown.h"
#include "lion.h"
#include "lipo.h"

/*
 * Usermod by Maximilian Mewes
 * Mail: mewes.maximilian@gmx.de
 * GitHub: itCarl
 * Date: 25.12.2022
 * If you have any questions, please feel free to contact me.
 */
class UsermodBattery : public Usermod 
{
  private:
    // battery pin can be defined in my_config.h
    int8_t batteryPin = USERMOD_BATTERY_MEASUREMENT_PIN;
    
    Battery* bat = nullptr;
    batteryConfig bcfg;

    // how often to read the battery voltage
    unsigned long readingInterval = USERMOD_BATTERY_MEASUREMENT_INTERVAL;
    unsigned long nextReadTime = 0;
    unsigned long lastReadTime = 0;

    // auto shutdown/shutoff/master off feature
    bool autoOffEnabled = USERMOD_BATTERY_AUTO_OFF_ENABLED;
    int8_t autoOffThreshold = USERMOD_BATTERY_AUTO_OFF_THRESHOLD;

    // low power indicator feature
    bool lowPowerIndicatorEnabled = USERMOD_BATTERY_LOW_POWER_INDICATOR_ENABLED;
    int8_t lowPowerIndicatorPreset = USERMOD_BATTERY_LOW_POWER_INDICATOR_PRESET;
    int8_t lowPowerIndicatorThreshold = USERMOD_BATTERY_LOW_POWER_INDICATOR_THRESHOLD;
    int8_t lowPowerIndicatorReactivationThreshold = lowPowerIndicatorThreshold+10;
    int8_t lowPowerIndicatorDuration = USERMOD_BATTERY_LOW_POWER_INDICATOR_DURATION;
    bool lowPowerIndicationDone = false;
    unsigned long lowPowerActivationTime = 0; // used temporary during active time
    int8_t lastPreset = 0;

    bool initDone = false;
    bool initializing = true;

    // strings to reduce flash memory usage (used more than twice)
    static const char _name[];
    static const char _readInterval[];
    static const char _enabled[];
    static const char _threshold[];
    static const char _preset[];
    static const char _duration[];
    static const char _init[];

    float dot2round(float x) 
    {
      float nx = (int)(x * 100 + .5);
      return (float)(nx / 100);
    }

    /*
     * Turn off all leds
     */
    void turnOff()
    {
      bri = 0;
      stateUpdated(CALL_MODE_DIRECT_CHANGE);
    }

    /*
     * Indicate low power by activating a configured preset for a given time and then switching back to the preset that was selected previously
     */
    void lowPowerIndicator()
    {
      if (!lowPowerIndicatorEnabled) return;
      if (batteryPin < 0) return;  // no measurement
      if (lowPowerIndicationDone && lowPowerIndicatorReactivationThreshold <= bat->getLevel()) lowPowerIndicationDone = false;
      if (lowPowerIndicatorThreshold <= bat->getLevel()) return;
      if (lowPowerIndicationDone) return;
      if (lowPowerActivationTime <= 1) {
        lowPowerActivationTime = millis();
        lastPreset = currentPreset;
        applyPreset(lowPowerIndicatorPreset);
      }

      if (lowPowerActivationTime+(lowPowerIndicatorDuration*1000) <= millis()) {
        lowPowerIndicationDone = true;
        lowPowerActivationTime = 0;
        applyPreset(lastPreset);
      }      
    }

  public:
    //Functions called by WLED

    /*
     * setup() is called once at boot. WiFi is not yet connected at this point.
     * You can use it to initialize variables, sensors or similar.
     */
    void setup() 
    {
      #ifdef ARDUINO_ARCH_ESP32
        bool success = false;
        DEBUG_PRINTLN(F("Allocating battery pin..."));
        if (batteryPin >= 0 && digitalPinToAnalogChannel(batteryPin) >= 0) 
          if (pinManager.allocatePin(batteryPin, false, PinOwner::UM_Battery)) {
            DEBUG_PRINTLN(F("Battery pin allocation succeeded."));
            success = true;
          }

        if (!success) {
          DEBUG_PRINTLN(F("Battery pin allocation failed."));
          batteryPin = -1;  // allocation failed
        } else {
          pinMode(batteryPin, INPUT);
        }
      #else //ESP8266 boards have only one analog input pin A0

        pinMode(batteryPin, INPUT);
      #endif

      //this could also be handled with a factory class but for only 2 types it should be sufficient for now
      if(bcfg.type == (batteryType)lipo) {
        bat = new Lipo();
      } else 
      if(bcfg.type == (batteryType)lion) {
        bat = new Lion();
      } else {
        bat = new Unkown(); // nullObject
      }

      bat->update(bcfg);

      nextReadTime = millis() + readingInterval;
      lastReadTime = millis();

      initDone = true;
    }


    /*
     * connected() is called every time the WiFi is (re)connected
     * Use it to initialize network interfaces
     */
    void connected() 
    {
      //Serial.println("Connected to WiFi!");
    }


    /*
     * loop() is called continuously. Here you can check for events, read sensors, etc.
     * 
     */
    void loop() 
    {
      if(strip.isUpdating()) return;

      lowPowerIndicator();

      // check the battery level every USERMOD_BATTERY_MEASUREMENT_INTERVAL (ms)
      if (millis() < nextReadTime) return;

      nextReadTime = millis() + readingInterval;
      lastReadTime = millis();

      if (batteryPin < 0) return;  // nothing to read

      initializing = false;     
      float voltage = -1.0f;
      float rawValue = 0.0f;
#ifdef ARDUINO_ARCH_ESP32
      // use calibrated millivolts analogread on esp32 (150 mV ~ 2450 mV)
      rawValue = analogReadMilliVolts(batteryPin);
      // calculate the voltage
      voltage = (rawValue / 1000.0f) + calibration;
      // usually a voltage divider (50%) is used on ESP32, so we need to multiply by 2
      voltage *= 2.0f;
#else
      // read battery raw input
      rawValue = analogRead(batteryPin);

      // calculate the voltage     
      voltage = ((rawValue / getAdcPrecision()) * bat->getMaxVoltage()) + bat->getCalibration();
#endif

      bat->setVoltage(voltage);
      // translate battery voltage into percentage
      bat->calculateAndSetLevel(voltage);

      // Auto off -- Master power off
      if (autoOffEnabled && (autoOffThreshold >= bat->getLevel()))
        turnOff();

      // SmartHome stuff
      // still don't know much about MQTT and/or HA
      if (WLED_MQTT_CONNECTED) {
        char buf[64]; // buffer for snprintf()
        snprintf_P(buf, 63, PSTR("%s/voltage"), mqttDeviceTopic);
        mqtt->publish(buf, 0, false, String(voltage).c_str());
      }

    }

    /*
     * addToJsonInfo() can be used to add custom entries to the /json/info part of the JSON API.
     * Creating an "u" object allows you to add custom key/value pairs to the Info section of the WLED web UI.
     * Below it is shown how this could be used for e.g. a light sensor
     */
    void addToJsonInfo(JsonObject& root)
    {
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");

      if (batteryPin < 0) {
        JsonArray infoVoltage = user.createNestedArray(F("Battery voltage"));
        infoVoltage.add(F("n/a"));
        infoVoltage.add(F(" invalid GPIO"));
        return;  // no GPIO - nothing to report
      }

      // info modal display names
      JsonArray infoPercentage = user.createNestedArray(F("Battery level"));
      JsonArray infoVoltage = user.createNestedArray(F("Battery voltage"));
      JsonArray infoNextUpdate = user.createNestedArray(F("Next update"));

      infoNextUpdate.add((nextReadTime - millis()) / 1000);
      infoNextUpdate.add(F(" sec"));
      
      if (initializing) {
        infoPercentage.add(FPSTR(_init));
        infoVoltage.add(FPSTR(_init));
        return;
      }

      if (bat->getLevel() < 0) {
        infoPercentage.add(F("invalid"));
      } else {
        infoPercentage.add(bat->getLevel());
      }
      infoPercentage.add(F(" %"));

      if (bat->getVoltage() < 0) {
        infoVoltage.add(F("invalid"));
      } else {
        infoVoltage.add(dot2round(bat->getVoltage()));
      }
      infoVoltage.add(F(" V"));
    }


    /*
     * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    /*
    void addToJsonState(JsonObject& root)
    {
      // TBD
    }
    */


    /*
     * readFromJsonState() can be used to receive data clients send to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    /*
    void readFromJsonState(JsonObject& root)
    {
      // TBD
    }
    */


    /*
     * addToConfig() can be used to add custom persistent settings to the cfg.json file in the "um" (usermod) object.
     * It will be called by WLED when settings are actually saved (for example, LED settings are saved)
     * If you want to force saving the current state, use serializeConfig() in your loop().
     * 
     * CAUTION: serializeConfig() will initiate a filesystem write operation.
     * It might cause the LEDs to stutter and will cause flash wear if called too often.
     * Use it sparingly and always in the loop, never in network callbacks!
     * 
     * addToConfig() will make your settings editable through the Usermod Settings page automatically.
     *
     * Usermod Settings Overview:
     * - Numeric values are treated as floats in the browser.
     *   - If the numeric value entered into the browser contains a decimal point, it will be parsed as a C float
     *     before being returned to the Usermod.  The float data type has only 6-7 decimal digits of precision, and
     *     doubles are not supported, numbers will be rounded to the nearest float value when being parsed.
     *     The range accepted by the input field is +/- 1.175494351e-38 to +/- 3.402823466e+38.
     *   - If the numeric value entered into the browser doesn't contain a decimal point, it will be parsed as a
     *     C int32_t (range: -2147483648 to 2147483647) before being returned to the usermod.
     *     Overflows or underflows are truncated to the max/min value for an int32_t, and again truncated to the type
     *     used in the Usermod when reading the value from ArduinoJson.
     * - Pin values can be treated differently from an integer value by using the key name "pin"
     *   - "pin" can contain a single or array of integer values
     *   - On the Usermod Settings page there is simple checking for pin conflicts and warnings for special pins
     *     - Red color indicates a conflict.  Yellow color indicates a pin with a warning (e.g. an input-only pin)
     *   - Tip: use int8_t to store the pin value in the Usermod, so a -1 value (pin not set) can be used
     *
     * See usermod_v2_auto_save.h for an example that saves Flash space by reusing ArduinoJson key name strings
     * 
     * If you need a dedicated settings page with custom layout for your Usermod, that takes a lot more work.  
     * You will have to add the setting to the HTML, xml.cpp and set.cpp manually.
     * See the WLED Soundreactive fork (code and wiki) for reference.  https://github.com/atuline/WLED
     * 
     * I highly recommend checking out the basics of ArduinoJson serialization and deserialization in order to use custom settings!
     */
    void addToConfig(JsonObject& root)
    {
      JsonObject battery = root.createNestedObject(FPSTR(_name));           // usermodname
      #ifdef ARDUINO_ARCH_ESP32
        battery[F("pin")] = batteryPin;
      #endif

      battery[F("type")] = (String)bcfg.type; // has to be a String otherwise it won't get converted to a Dropdown
      battery[F("min-voltage")] = bat->getMinVoltage();
      battery[F("max-voltage")] = bat->getMaxVoltage();
      battery[F("capacity")] = bat->getCapacity();
      battery[F("calibration")] = bat->getCalibration();
      battery[FPSTR(_readInterval)] = readingInterval;
      
      JsonObject ao = battery.createNestedObject(F("auto-off"));  // auto off section
      ao[FPSTR(_enabled)] = autoOffEnabled;
      ao[FPSTR(_threshold)] = autoOffThreshold;

      JsonObject lp = battery.createNestedObject(F("indicator")); // low power section
      lp[FPSTR(_enabled)] = lowPowerIndicatorEnabled;
      lp[FPSTR(_preset)] = lowPowerIndicatorPreset; // dropdown trickery (String)lowPowerIndicatorPreset; 
      lp[FPSTR(_threshold)] = lowPowerIndicatorThreshold;
      lp[FPSTR(_duration)] = lowPowerIndicatorDuration;

      DEBUG_PRINTLN(F("Battery config saved."));
    }

    void appendConfigData()
    {
      oappend(SET_F("td=addDropdown('Battery', 'type');"));
      oappend(SET_F("addOption(td, 'Unkown', '0');"));
      oappend(SET_F("addOption(td, 'LiPo', '1');"));
      oappend(SET_F("addOption(td, 'LiOn', '2');"));
      oappend(SET_F("addInfo('Battery:type',1,'<small style=\"color:orange\">requires reboot</small>');"));
      oappend(SET_F("addInfo('Battery:min-voltage', 1, 'v');"));
      oappend(SET_F("addInfo('Battery:max-voltage', 1, 'v');"));
      oappend(SET_F("addInfo('Battery:capacity', 1, 'mAh');"));
      oappend(SET_F("addInfo('Battery:interval', 1, 'ms');"));
      oappend(SET_F("addInfo('Battery:auto-off:threshold', 1, '%');"));
      oappend(SET_F("addInfo('Battery:indicator:threshold', 1, '%');"));
      oappend(SET_F("addInfo('Battery:indicator:duration', 1, 's');"));
      
      // cannot quite get this mf to work. its exeeding some buffer limit i think
      // what i wanted is a list of all presets to select one from
      // oappend(SET_F("bd=addDropdown('Battery:low-power-indicator', 'preset');"));
      // the loop generates: oappend(SET_F("addOption(bd, 'preset name', preset id);"));
      // for(int8_t i=1; i < 42; i++) {
      //   oappend(SET_F("addOption(bd, 'Preset#"));
      //   oappendi(i);
      //   oappend(SET_F("',"));
      //   oappendi(i);
      //   oappend(SET_F(");"));
      // }
    }


    /*
     * readFromConfig() can be used to read back the custom settings you added with addToConfig().
     * This is called by WLED when settings are loaded (currently this only happens immediately after boot, or after saving on the Usermod Settings page)
     * 
     * readFromConfig() is called BEFORE setup(). This means you can use your persistent values in setup() (e.g. pin assignments, buffer sizes),
     * but also that if you want to write persistent values to a dynamic buffer, you'd need to allocate it here instead of in setup.
     * If you don't know what that is, don't fret. It most likely doesn't affect your use case :)
     * 
     * Return true in case the config values returned from Usermod Settings were complete, or false if you'd like WLED to save your defaults to disk (so any missing values are editable in Usermod Settings)
     * 
     * getJsonValue() returns false if the value is missing, or copies the value into the variable provided and returns true if the value is present
     * The configComplete variable is true only if the "exampleUsermod" object and all values are present.  If any values are missing, WLED will know to call addToConfig() to save them
     * 
     * This function is guaranteed to be called on boot, but could also be called every time settings are updated
     */
    bool readFromConfig(JsonObject& root)
    {
      #ifdef ARDUINO_ARCH_ESP32
        int8_t newBatteryPin = batteryPin;
      #endif
      
      JsonObject battery = root[FPSTR(_name)];
      if (battery.isNull()) 
      {
        DEBUG_PRINT(FPSTR(_name));
        DEBUG_PRINTLN(F(": No config found. (Using defaults.)"));
        return false;
      }

      #ifdef ARDUINO_ARCH_ESP32
        newBatteryPin     = battery[F("pin")] | newBatteryPin;
      #endif

      getJsonValue(battery[F("type")], bcfg.type);
      getJsonValue(battery[F("min-voltage")], bcfg.minVoltage);
      getJsonValue(battery[F("max-voltage")], bcfg.maxVoltage);
      getJsonValue(battery[F("capacity")], bcfg.capacity);
      getJsonValue(battery[F("calibration")], bcfg.calibration);
      setReadingInterval(battery[FPSTR(_readInterval)] | readingInterval);

      JsonObject ao = battery[F("auto-off")];
      setAutoOffEnabled(ao[FPSTR(_enabled)] | autoOffEnabled);
      setAutoOffThreshold(ao[FPSTR(_threshold)] | autoOffThreshold);

      JsonObject lp = battery[F("indicator")];
      setLowPowerIndicatorEnabled(lp[FPSTR(_enabled)] | lowPowerIndicatorEnabled);
      setLowPowerIndicatorPreset(lp[FPSTR(_preset)] | lowPowerIndicatorPreset); // dropdown trickery (int)lp["preset"]
      setLowPowerIndicatorThreshold(lp[FPSTR(_threshold)] | lowPowerIndicatorThreshold);
      lowPowerIndicatorReactivationThreshold = lowPowerIndicatorThreshold+10;
      setLowPowerIndicatorDuration(lp[FPSTR(_duration)] | lowPowerIndicatorDuration);

      DEBUG_PRINT(FPSTR(_name));

      #ifdef ARDUINO_ARCH_ESP32
        if (!initDone) 
        {
          // first run: reading from cfg.json
          batteryPin = newBatteryPin;
          DEBUG_PRINTLN(F(" config loaded."));
        } 
        else 
        {
          DEBUG_PRINTLN(F(" config (re)loaded."));

          // changing parameters from settings page
          if (newBatteryPin != batteryPin) 
          {
            // deallocate pin
            pinManager.deallocatePin(batteryPin, PinOwner::UM_Battery);
            batteryPin = newBatteryPin;
            // initialise
            setup();
          }
        }
      #endif

      if(initDone) 
        bat->update(bcfg);

      return !battery[FPSTR(_readInterval)].isNull();
    }

    /*
     * TBD: Generate a preset sample for low power indication
     * a button on the config page would be cool, currently not possible
     */
    void generateExamplePreset()
    {
      // StaticJsonDocument<300> j;
      // JsonObject preset = j.createNestedObject();
      // preset["mainseg"] = 0;
      // JsonArray seg = preset.createNestedArray("seg");
      // JsonObject seg0 = seg.createNestedObject();
      // seg0["id"] = 0;
      // seg0["start"] = 0;
      // seg0["stop"] = 60;
      // seg0["grp"] = 0;
      // seg0["spc"] = 0;
      // seg0["on"] = true;
      // seg0["bri"] = 255;

      // JsonArray col0 = seg0.createNestedArray("col");
      // JsonArray col00 = col0.createNestedArray();
      // col00.add(255);
      // col00.add(0);
      // col00.add(0);

      // seg0["fx"] = 1;
      // seg0["sx"] = 128;
      // seg0["ix"] = 128;

      // savePreset(199, "Low power Indicator", preset);
    }
   

    /*
     *
     * Getter and Setter. Just in case some other usermod wants to interact with this in the future
     *
     */

    /*
     * getId() allows you to optionally give your V2 usermod an unique ID (please define it in const.h!).
     * This could be used in the future for the system to determine whether your usermod is installed.
     */
    uint16_t getId()
    {
      return USERMOD_ID_BATTERY;
    }


    unsigned long getReadingInterval()
    {
      return readingInterval;
    }

    /*
     * minimum repetition is 3000ms (3s) 
     */
    void setReadingInterval(unsigned long newReadingInterval)
    {
      readingInterval = max((unsigned long)3000, newReadingInterval);
    }

    /*
     * Get the choosen adc precision
     * esp8266 = 10bit resolution = 1024.0f 
     * esp32 = 12bit resolution = 4095.0f
     */
    float getAdcPrecision()
    {
      #ifdef ARDUINO_ARCH_ESP32
        // esp32
        return 4096.0f;
      #else
        // esp8266
        return 1024.0f;
      #endif
    }

    /*
     * Get auto-off feature enabled status
     * is auto-off enabled, true/false
     */
    bool getAutoOffEnabled()
    {
      return autoOffEnabled;
    }

    /*
     * Set auto-off feature status 
     */
    void setAutoOffEnabled(bool enabled)
    {
      autoOffEnabled = enabled;
    }
    
    /*
     * Get auto-off threshold in percent (0-100)
     */
    int8_t getAutoOffThreshold()
    {
      return autoOffThreshold;
    }

    /*
     * Set auto-off threshold in percent (0-100) 
     */
    void setAutoOffThreshold(int8_t threshold)
    {
      autoOffThreshold = min((int8_t)100, max((int8_t)0, threshold));
      // when low power indicator is enabled the auto-off threshold cannot be above indicator threshold
      autoOffThreshold  = lowPowerIndicatorEnabled /*&& autoOffEnabled*/ ? min(lowPowerIndicatorThreshold-1, (int)autoOffThreshold) : autoOffThreshold;
    }


    /*
     * Get low-power-indicator feature enabled status
     * is the low-power-indicator enabled, true/false
     */
    bool getLowPowerIndicatorEnabled()
    {
      return lowPowerIndicatorEnabled;
    }

    /*
     * Set low-power-indicator feature status 
     */
    void setLowPowerIndicatorEnabled(bool enabled)
    {
      lowPowerIndicatorEnabled = enabled;
    }

    /*
     * Get low-power-indicator preset to activate when low power is detected
     */
    int8_t getLowPowerIndicatorPreset()
    {
      return lowPowerIndicatorPreset;
    }

    /* 
     * Set low-power-indicator preset to activate when low power is detected
     */
    void setLowPowerIndicatorPreset(int8_t presetId)
    {
      // String tmp = ""; For what ever reason this doesn't work :(
      // lowPowerIndicatorPreset = getPresetName(presetId, tmp) ? presetId : lowPowerIndicatorPreset;
      lowPowerIndicatorPreset = presetId;
    }

    /*
     * Get low-power-indicator threshold in percent (0-100)
     */
    int8_t getLowPowerIndicatorThreshold()
    {
      return lowPowerIndicatorThreshold;
    }

    /*
     * Set low-power-indicator threshold in percent (0-100)
     */
    void setLowPowerIndicatorThreshold(int8_t threshold)
    {
      lowPowerIndicatorThreshold = threshold;
      // when auto-off is enabled the indicator threshold cannot be below auto-off threshold
      lowPowerIndicatorThreshold  = autoOffEnabled /*&& lowPowerIndicatorEnabled*/ ? max(autoOffThreshold+1, (int)lowPowerIndicatorThreshold) : max(5, (int)lowPowerIndicatorThreshold);
    }

    /*
     * Get low-power-indicator duration in seconds
     */
    int8_t getLowPowerIndicatorDuration()
    {
      return lowPowerIndicatorDuration;
    }

    /*
     * Set low-power-indicator duration in seconds
     */
    void setLowPowerIndicatorDuration(int8_t duration)
    {
      lowPowerIndicatorDuration = duration;
    }


    /*
     * Get low-power-indicator status when the indication is done thsi returns true
     */
    bool getLowPowerIndicatorDone()
    {
      return lowPowerIndicationDone;
    }
};

// strings to reduce flash memory usage (used more than twice)
const char UsermodBattery::_name[]          PROGMEM = "Battery";
const char UsermodBattery::_readInterval[]  PROGMEM = "interval";
const char UsermodBattery::_enabled[]       PROGMEM = "enabled";
const char UsermodBattery::_threshold[]     PROGMEM = "threshold";
const char UsermodBattery::_preset[]        PROGMEM = "preset";
const char UsermodBattery::_duration[]      PROGMEM = "duration";
const char UsermodBattery::_init[]          PROGMEM = "init";
