
class OneWireSerial:public SoftwareSerial
{
  public:
  OneWireSerial(uint8_t pin):
    SoftwareSerial(pin, pin), mPin(pin)
  {}
  void enableTx(){pinMode(mPin, OUTPUT);}
  void disableTx(){pinMode(mPin, INPUT);}

  int writeRead(char* out, char* in, int max)
  {
    stopListening();
    enableTx();
    print(out);
    disableTx();
    listen();
    int len = readBytesUntil('\n', in, max);
    stopListening();
    return len;
  }

  private:
  uint8_t mPin;
};

class DistanceSensor
{
  public:
  DistanceSensor(OneWireSerial* serial, uint8_t deviceIndex) : mSerial(serial), mDeviceIndex(deviceIndex)
  {}

  bool readRegister(uint8_t index, uint16_t* result)
  {
    char buffer[11];
    sprintf(buffer, "R%02X%02X\n",mDeviceIndex, index);
    int len = mSerial->writeRead(buffer, buffer, sizeof(buffer));
    if (len <= 0 || len > 4) return false;
    buffer[len] = 0;
    *result = strtol(buffer, 0, 16);
    return true;
  }

  bool writeRegister(uint8_t index, uint16_t value)
  {
    char buffer[15];
    sprintf(buffer, "W%02X%02X%04X\n",mDeviceIndex, index, value);
    int len = mSerial->writeRead(buffer, buffer, sizeof(buffer));
    if (len != 1) return false;
    buffer[len] = 0;
    if (strtol(buffer, 0, 16) != 0xA) return false;
    return true;
  }

  unsigned int readDistance_mm()
  {
    int res = 0;
    bool ok = readRegister(0x01, &res);
    if (!ok) return 0xFFF;
    return res;
  }

  unsigned int readDistance_cm()
  {
    int res = 0;
    bool ok = readRegister(0x06, &res);
    if (!ok) return 0xFF;
    return res;
  }

  private:
  OneWireSerial* mSerial;
  uint8_t mDeviceIndex;
};
