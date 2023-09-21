/*
 * CaptureIR - Infrared transceiver control application
 *
 * Copyright (c) Xen xen-re[at]tutanota.com
 */

#include "ctqirsignal.h"

//LIRC defines
#ifndef PULSE_BIT
#define PULSE_BIT       0x01000000
#define PULSE_MASK      0x00FFFFFF
#endif

CTqIrSignal::CTqIrSignal()
{
    DrawXScale = 1;
    DrawYscale = 1;
    DrawYscale /= 1000 * UnitsInMksec;
}

bool CTqIrSignal::FromTiqiaa(std::vector<uint8_t> signal)
{
    return FromTiqiaa(signal.data(), signal.size());
}

bool CTqIrSignal::FromTiqiaa(uint8_t * data, size_t size)
{
    uint32_t PulseSize;
    bool CurrLvl, NewLvl;

    if ((data == NULL) || (size < 1)) return false;
    SignalData.clear();
    PulseSize = 0;
    CurrLvl = (data[0] & TiqiaaPulseBit) != 0;

    for (size_t i = 0; i < size; i++)
    {
        NewLvl = (data[i] & TiqiaaPulseBit) != 0;
        if (CurrLvl != NewLvl)
        {
            SignalData.push_back(PulseSize | (CurrLvl ? PulseBit : 0));
            CurrLvl = NewLvl;
            PulseSize = 0;
        }
        PulseSize += (data[i] & TiqiaaPulseMask) * TiqiaaTickSize;
    }
    if (PulseSize > 0) SignalData.push_back(PulseSize | (CurrLvl ? PulseBit : 0));
    return true;
}

std::vector<uint8_t> CTqIrSignal::ToTiqiaa()
{
    std::vector<uint8_t> res;
    int32_t SrcPos = 0;
    int32_t DstPos = 0;
    uint32_t Ps;
    uint8_t Pb;

    for (size_t i = 0; i < SignalData.size(); i++){
        Ps = SignalData[i];
        Pb = (Ps & PulseBit) ? TiqiaaPulseBit : 0;
        Ps &= PulseMask;
        SrcPos += Ps;
        Ps = SrcPos - DstPos;
        Ps /= TiqiaaTickSize;
        DstPos += Ps * TiqiaaTickSize;
        while (Ps > 0)
        {
            if (Ps > TiqiaaPulseMask){
                res.push_back(TiqiaaPulseMask | Pb);
                Ps -= TiqiaaPulseMask;
            } else {
                res.push_back(Ps | Pb);
                Ps = 0;
            }
        }
    }
    return res;
}

bool CTqIrSignal::FromLirc(uint32_t * data, size_t size)
{
    uint32_t PulseSize;
    bool CurrLvl, NewLvl;

    if ((data == NULL) || (size < 1)) return false;
    SignalData.clear();
    PulseSize = 0;
    CurrLvl = (data[0] & PULSE_BIT) != 0;

    for (size_t i = 0; i < size; i++)
    {
        NewLvl = (data[i] & PULSE_BIT) != 0;
        if (CurrLvl != NewLvl)
        {
            SignalData.push_back(PulseSize | (CurrLvl ? PulseBit : 0));
            CurrLvl = NewLvl;
            PulseSize = 0;
        }
        PulseSize += (data[i] & PULSE_MASK) * UnitsInMksec;
    }
    if (PulseSize > 0) SignalData.push_back(PulseSize | (CurrLvl ? PulseBit : 0));
    return true;
}

bool CTqIrSignal::FromLirc(std::vector<uint32_t> signal)
{
    return FromLirc(signal.data(), signal.size());
}

void CTqIrSignal::WriteIrNecSignalPulse(int PulseCount, bool isSet)
{
    SignalData.push_back(PulseCount * NecPulseSize | (isSet ? PulseBit : 0));
}

void CTqIrSignal::WriteIrNecSignal(uint16_t IrCode){
    uint32_t tcode;

    SignalData.clear();
    ((uint8_t *)(&tcode))[0] = ((uint8_t *)(&IrCode))[1];
    ((uint8_t *)(&tcode))[1] = ~((uint8_t *)(&IrCode))[1];
    ((uint8_t *)(&tcode))[2] = ((uint8_t *)(&IrCode))[0];
    ((uint8_t *)(&tcode))[3] = ~((uint8_t *)(&IrCode))[0];

    WriteIrNecSignalPulse(16, true);
    WriteIrNecSignalPulse(8, false);
    for (int i=0; i<32;i++){
        WriteIrNecSignalPulse(1, true);
        WriteIrNecSignalPulse(((tcode&1) != 0) ? 3 : 1, false);
        tcode >>= 1;
    }
    WriteIrNecSignalPulse(1, true);
    WriteIrNecSignalPulse(72, false);
}

bool CTqIrSignal::SignalInRange(uint32_t value, uint32_t needSize, bool needHigh)
{
    uint32_t needSizeMin, needSizeMax;
    if (((value & PulseBit) != 0) != needHigh) return false;
    needSizeMin = needSize - (needSize / MaxSignalRangeDeviation);
    needSizeMax = needSize + (needSize / MaxSignalRangeDeviation);
    return (((value & PulseMask) >= needSizeMin) && ((value & PulseMask) <= needSizeMax));
}

bool CTqIrSignal::DecodeIrNecSignal(uint16_t * IrCode, uint32_t * RawIrCode){
    uint32_t CodeRaw, CodeBit;
    size_t CodeStartOffs;
    int state = 0;

    for (size_t i=0; i< SignalData.size(); i++)
    {
        switch (state)
        {
            case 0: //Start pulse
                if (SignalInRange(SignalData[i], NecPulseSize * 16, true)){
                    state = 1;
                    CodeStartOffs = i;
                }
                break;
            case 1: //Start pause
                if (SignalInRange(SignalData[i], NecPulseSize * 8, false)){
                    state = 2;
                    CodeRaw = 0;
                    CodeBit = 0;
                } else {
                    state = 0;
                    i = CodeStartOffs + 1;
                }
                break;
            case 2: //Bit pulse
                if (SignalInRange(SignalData[i], NecPulseSize, true)){
                    if (CodeBit >= 32){ //End of signal
                        ((uint8_t *)(RawIrCode))[0] = ((uint8_t *)&CodeRaw)[3] ^ 0xFF;
                        ((uint8_t *)(RawIrCode))[1] = ((uint8_t *)&CodeRaw)[2];
                        ((uint8_t *)(RawIrCode))[2] = ((uint8_t *)&CodeRaw)[1] ^ 0xFF;
                        ((uint8_t *)(RawIrCode))[3] = ((uint8_t *)&CodeRaw)[0];
                        *IrCode = ((CodeRaw & 0xFF) << 8) | ((CodeRaw >> 16) & 0xFF);
                        return true;
                    } else {
                        state = 3;
                    }
                } else {
                    state = 0;
                    i = CodeStartOffs + 1;
                }
                break;
            case 3: //Bit pause
                if (SignalInRange(SignalData[i], NecPulseSize, false)){
                    state = 2;
                    CodeRaw >>= 1;
                    CodeBit ++;
                } else if (SignalInRange(SignalData[i], NecPulseSize * 3, false)){
                    state = 2;
                    CodeRaw >>= 1;
                    CodeRaw |= 0x80000000;
                    CodeBit ++;
                } else {
                    state = 0;
                    i = CodeStartOffs + 1;
                }
                break;
        }
    }
    return false;
}

std::vector<uint32_t> CTqIrSignal::GetSignal()
{
    return SignalData;
}

std::vector<uint32_t> CTqIrSignal::ToLirc()
{
    std::vector<uint32_t> res = SignalData;
    for (size_t i = 0; i< res.size(); i++)
    {
        uint32_t v = res[i];
        bool h = ((v & PulseBit) != 0);
        v &= PulseMask;
        v /= UnitsInMksec;
        if (v > PULSE_MASK) v = PULSE_MASK;
        if (h) v |= PULSE_BIT;
        res[i] = v;
    }
    return res;
}
