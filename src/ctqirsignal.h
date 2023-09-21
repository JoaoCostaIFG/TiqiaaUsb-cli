/*
 * CaptureIR - Infrared transceiver control application
 *
 * Copyright (c) Xen xen-re[at]tutanota.com
 */

#ifndef CTQIRSIGNAL_H
#define CTQIRSIGNAL_H

#include <stdint.h>
#include <vector>


class CTqIrSignal
{
private:
    static const int UnitsInMksec = 2;
    static const uint32_t PulseBit = 0x80000000;
    static const uint32_t PulseMask = 0x7FFFFFFF;
    static const int TiqiaaTickSize = 16 * UnitsInMksec; //16 mks
    static const uint8_t TiqiaaPulseBit = 0x80;
    static const uint8_t TiqiaaPulseMask = 0x7F;
    static const int NecPulseSize = 1125; //562.5 mks
    static const int MaxSignalRangeDeviation = 5; // 1/5

    std::vector<uint32_t> SignalData;

    double DrawXScale;
    double DrawYscale;

    int IrWrData_PulseTime;
    int IrWrData_SenderTime;

    void WriteIrNecSignalPulse(int PulseCount, bool isSet);
    bool SignalInRange(uint32_t value, uint32_t needSize, bool needHigh);

public:
    CTqIrSignal();

    bool FromTiqiaa(std::vector<uint8_t> sample);
    bool FromTiqiaa(uint8_t * data, size_t size);
    std::vector<uint8_t> ToTiqiaa();
    bool FromLirc(uint32_t * data, size_t size);
    bool FromLirc(std::vector<uint32_t> signal);
    std::vector<uint32_t> ToLirc();
    void WriteIrNecSignal(uint16_t IrCode);
    bool DecodeIrNecSignal(uint16_t * IrCode, uint32_t * RawIrCode);
    std::vector<uint32_t> GetSignal();
};

#endif // CTQIRSIGNAL_H
