/*
 * Userspace driver for Tiqiaa Tview USB IR Transeiver
 *
 * Copyright (c) Xen xen-re[at]tutanota.com
 *
 * LibUSB port: Rabit
 */

#include "TiqiaaUsb.h"
#include <cstring>
#include <stdlib.h>

#include <libusb-1.0/libusb.h>

#include <cstdio>

#define VENDOR_ID 0x10c4
#define PRODUCT_ID 0x8468

TiqiaaUsbIr::TiqiaaUsbIr() {
    dev_h = NULL;
    IrRecvCallback = NULL;
    IrRecvCbContext = NULL;
    PacketIndex = 0;
    CmdId = 0;
    DeviceState = 0;

    pthread_cond_init(&read_thread_info.condition, NULL);
    pthread_mutex_init(&read_thread_info.mutex, NULL);
}

TiqiaaUsbIr::~TiqiaaUsbIr() {
    Close();
    pthread_mutex_destroy(&read_thread_info.mutex);
    pthread_cond_destroy(&read_thread_info.condition);
}

bool TiqiaaUsbIr::InitDevice() {
    if( libusb_set_configuration(dev_h, 1) < 0 ) return false;
    if( libusb_claim_interface(dev_h, 0) < 0 ) return false;
    return true;
}

bool TiqiaaUsbIr::Open() {
    if( IsOpen() ) return false;

    if( libusb_init(NULL) != LIBUSB_SUCCESS ) return false;

    dev_h = libusb_open_device_with_vid_pid(NULL, VENDOR_ID, PRODUCT_ID);
    if( dev_h && libusb_reset_device(dev_h) == 0 && InitDevice() ) {
        IsWaitingCmdReply = false;
        ReadActive = true;
        pthread_create(&(read_thread_info.thread_id), NULL, TiqiaaUsbIr::RunReadThreadFn, (void*)this);

        if( read_thread_info.thread_id ) {
            if( SendCmdAndWaitReply(CmdVersion, GetCmdId(), CmdReplyWaitTimeout) ) {
                if( SendCmdAndWaitReply(CmdSendMode, GetCmdId(), CmdReplyWaitTimeout) ) {
                    return true;
                }
            }
            ReadActive = false;
            pthread_join(read_thread_info.thread_id, NULL);
        }
    }


    libusb_close(dev_h);
    dev_h = NULL;
    libusb_exit(NULL);

    return false;
}

bool TiqiaaUsbIr::Close() {
    if( !IsOpen() ) return false;
    ReadActive = false;
    SetIdleMode();
    pthread_join(read_thread_info.thread_id, NULL);
    libusb_close(dev_h);
    dev_h = NULL;
    libusb_exit(NULL);
    return true;
}

bool TiqiaaUsbIr::IsOpen() {
    return dev_h != NULL;
}

bool TiqiaaUsbIr::SendReport2(void * data, int size) {
    uint8_t FragmBuf[61];
    TiqiaaUsbIr_Report2Header * ReportHdr = (TiqiaaUsbIr_Report2Header *)FragmBuf;
    int RdPtr;
    int FragmIndex;
    int FragmSize;
    int UsbTxSize;

    RdPtr = 0;
    if( (size <= 0) || (size > MaxUsbPacketSize) ) return false;
    memset(FragmBuf, 0, sizeof(FragmBuf));
    ReportHdr->ReportId = WriteReportId;
    ReportHdr->FragmCount = size / MaxUsbFragmSize;
    if( (size % MaxUsbFragmSize) != 0 ) ReportHdr->FragmCount ++;
    PacketIndex ++;
    if( PacketIndex > MaxUsbPacketIndex ) PacketIndex = 1;
    ReportHdr->PacketIdx = PacketIndex;
    FragmIndex = 0;
    while( RdPtr < size ) {
        FragmIndex ++;
        ReportHdr->FragmIdx = FragmIndex;
        FragmSize = size - RdPtr;
        if( FragmSize > MaxUsbFragmSize ) FragmSize = MaxUsbFragmSize;
        ReportHdr->FragmSize = FragmSize + 3;
        memcpy(FragmBuf + sizeof(TiqiaaUsbIr_Report2Header), ((uint8_t *)data) + RdPtr, FragmSize);
        if( libusb_bulk_transfer(dev_h, WritePipeId, FragmBuf, FragmSize + sizeof(TiqiaaUsbIr_Report2Header), &UsbTxSize, 0) < 0 ) return false;
        RdPtr += FragmSize;
    }
    return true;
}

bool TiqiaaUsbIr::SendCmd(uint8_t cmdType, uint8_t cmdId) {
    TiqiaaUsbIr_SendCmdPack Pack;

    Pack.StartSign = PackStartSign;
    Pack.CmdType = cmdType;
    Pack.CmdId = cmdId;
    Pack.EndSign = PackEndSign;
    return SendReport2(&Pack, sizeof(Pack));
}

bool TiqiaaUsbIr::SendIRCmd(int freq, void * buffer, int buf_size, uint8_t cmdId) {
    uint8_t PackBuf[MaxUsbPacketSize];
    TiqiaaUsbIr_SendIRPackHeader * PackHeader = (TiqiaaUsbIr_SendIRPackHeader *)PackBuf;
    uint8_t IrFreqId;
    int PackSize = sizeof(TiqiaaUsbIr_SendIRPackHeader);

    if( buf_size < 0 ) return false;
    if( (buf_size + sizeof(TiqiaaUsbIr_SendIRPackHeader) + sizeof(uint16_t)) > MaxUsbPacketSize ) return false;
    if( freq > 255 ) {
        IrFreqId = 0;
        while( (IrFreqId < TiqiaaUsbIr_IrFreqTableSize) && (TiqiaaUsbIr_IrFreqTable[IrFreqId] != freq) ) IrFreqId++;
        if( IrFreqId >= TiqiaaUsbIr_IrFreqTableSize ) return false;
    } else {
        if( freq < TiqiaaUsbIr_IrFreqTableSize ) IrFreqId = freq; else return false;
    }
    PackHeader->StartSign = PackStartSign;
    PackHeader->CmdType = 'D';
    PackHeader->CmdId = cmdId;
    PackHeader->IrFreqId = IrFreqId;
    memcpy(PackBuf + PackSize, buffer, buf_size);
    PackSize += buf_size;
    *(uint16_t *)(PackBuf + PackSize) = PackEndSign;
    PackSize += sizeof(uint16_t);
    return SendReport2(PackBuf, PackSize);
}

bool TiqiaaUsbIr::SendCmdAndWaitReply(uint8_t cmdType, uint8_t cmdId, uint16_t timeout) {
    if( !StartCmdReplyWaiting(cmdType, cmdId) ) return false;
    if( SendCmd(cmdType, cmdId) ) {
        if( WaitCmdReply(timeout) ) return true;
    }
    CancelCmdReplyWaiting();
    return false;
}

uint8_t TiqiaaUsbIr::GetCmdId() {
    if( CmdId < MaxCmdId ) CmdId ++; else CmdId = 1;
    return CmdId;
}

bool TiqiaaUsbIr::StartCmdReplyWaiting(uint8_t cmdType, uint8_t cmdId) {
    if( !IsOpen() ) return false;

    pthread_mutex_lock(&read_thread_info.mutex);
    if( IsWaitingCmdReply ) return false;
    WaitCmdId = cmdId;
    WaitCmdType = cmdType;
    IsWaitingCmdReply = true;
    IsCmdReplyReceived = false;
    pthread_mutex_unlock(&read_thread_info.mutex);

    return true;
}

bool TiqiaaUsbIr::WaitCmdReply(uint16_t timeout) {
    bool res = false;
    struct timespec now;
    struct timespec wait_until;

    if( !IsOpen() ) return false;
    if( !IsWaitingCmdReply ) return false;

    clock_gettime(CLOCK_REALTIME, &now);
    // timeout in msec
    wait_until.tv_sec = now.tv_sec + timeout / 1000;
    wait_until.tv_nsec = now.tv_nsec + (timeout % 1000) * 1000000;
    while( wait_until.tv_nsec > 999999999 ) {
        wait_until.tv_sec += 1;
        wait_until.tv_nsec -= 999999999;
    }
    pthread_cond_timedwait(&read_thread_info.condition, &read_thread_info.mutex, &wait_until);
    // Here need to process return and unlock the mutex after that
    pthread_mutex_unlock(&read_thread_info.mutex);

    pthread_mutex_lock(&read_thread_info.mutex);
    if( IsWaitingCmdReply && IsCmdReplyReceived ) {
        res = true;
        IsWaitingCmdReply = false;
    }
    pthread_mutex_unlock(&read_thread_info.mutex);
    return res;
}

bool TiqiaaUsbIr::CancelCmdReplyWaiting() {
    bool res = false;
    if( !IsOpen() ) return false;

    pthread_mutex_lock(&read_thread_info.mutex);
    if( IsWaitingCmdReply ) {
        IsWaitingCmdReply = false;
        res = true;
    }
    pthread_mutex_unlock(&read_thread_info.mutex);
    return res;
}

bool TiqiaaUsbIr::SetIdleMode() {
    if( !IsOpen() ) return false;
    if( DeviceState == StateIdle ) return true;
    if( SendCmdAndWaitReply(CmdIdleMode, GetCmdId(), CmdReplyWaitTimeout) ) {
        if( DeviceState == StateIdle ) return true;
    }
    return false;
}

bool TiqiaaUsbIr::SendIR(int freq, void * buffer, int buf_size) {
    if( !IsOpen() ) return false;
    if( DeviceState != StateSend ) {
        if( !SendCmdAndWaitReply(CmdSendMode, GetCmdId(), CmdReplyWaitTimeout) ) return false;
    }
    if( DeviceState != StateSend ) return false;
    uint8_t SendIRCmdId = GetCmdId();
    if( !StartCmdReplyWaiting(CmdOutput, SendIRCmdId) ) return false;
    if( SendIRCmd(freq, buffer, buf_size, SendIRCmdId) ) {
        if( WaitCmdReply(IrReplyWaitTimeout)) return true;
    }
    CancelCmdReplyWaiting();
    return false;
}

bool TiqiaaUsbIr::StartRecvIR() {
    if( !IsOpen() ) return false;
    if( DeviceState != StateRecv ) {
        if( !SendCmdAndWaitReply(CmdRecvMode, GetCmdId(), CmdReplyWaitTimeout) ) return false;
        if( DeviceState != StateRecv ) return false;
        if( !SendCmdAndWaitReply(CmdCancel, GetCmdId(), CmdReplyWaitTimeout) ) return false;
    }
    if( !SendCmd(CmdOutput, GetCmdId()) ) return false;
    return true;
}

bool TiqiaaUsbIr::SendNecSignal(uint16_t IrCode) {
    uint8_t Buf[128];
    int BufSize;

    BufSize = WriteIrNecSignal(IrCode, Buf);
    return SendIR(38000, Buf, BufSize);
}


void TiqiaaUsbIr::WriteIrNecSignalPulse(TqIrWriteData * IrWrData, int PulseCount, bool isSet) {
    int TickCount;
    int SendBlockSize;

    IrWrData->PulseTime += PulseCount * NecPulseSize;
    TickCount = IrWrData->PulseTime - IrWrData->SenderTime;
    TickCount /= IrSendTickSize;
    IrWrData->SenderTime += TickCount * IrSendTickSize;
    while( TickCount > 0 ) {
        SendBlockSize = TickCount;
        if( SendBlockSize > MaxIrSendBlockSize ) SendBlockSize = MaxIrSendBlockSize;
        TickCount -= SendBlockSize;
        if( isSet ) SendBlockSize |= 0x80;
        IrWrData->Buf[IrWrData->Size] = SendBlockSize;
        IrWrData->Size ++;
    }
}

int TiqiaaUsbIr::WriteIrNecSignal(uint16_t IrCode, uint8_t * OutBuf) {
    TqIrWriteData WriteData;
    int i;
    uint32_t tcode;

    WriteData.Buf = OutBuf;
    WriteData.Size = 0;
    WriteData.PulseTime = 0;
    WriteData.SenderTime = 0;

    ((uint8_t *)(&tcode))[0] = ((uint8_t *)(&IrCode))[1];
    ((uint8_t *)(&tcode))[1] = ~((uint8_t *)(&IrCode))[1];
    ((uint8_t *)(&tcode))[2] = ((uint8_t *)(&IrCode))[0];
    ((uint8_t *)(&tcode))[3] = ~((uint8_t *)(&IrCode))[0];

    WriteIrNecSignalPulse(&WriteData, 16, true);
    WriteIrNecSignalPulse(&WriteData, 8, false);

    for( i=0; i<32;i++ ) {
        WriteIrNecSignalPulse(&WriteData, 1, true);
        WriteIrNecSignalPulse(&WriteData, ((tcode&1) != 0) ? 3 : 1, false);
        tcode >>= 1;
    }
    WriteIrNecSignalPulse(&WriteData, 1, true);
    WriteIrNecSignalPulse(&WriteData, 72, false);
    return WriteData.Size;
}


void TiqiaaUsbIr::ProcessRecvPacket(uint8_t * pack, int size) {
    if( IsWaitingCmdReply ) {
        pthread_mutex_lock(&read_thread_info.mutex);
        if( IsWaitingCmdReply && !IsCmdReplyReceived ) {
            if( (pack[0] == WaitCmdId) && (pack[1] == WaitCmdType) ) {
                IsCmdReplyReceived = true;
                pthread_cond_signal(&read_thread_info.condition);
            }
        }
        pthread_mutex_unlock(&read_thread_info.mutex);
    }
    switch( pack[1] ) {
        case CmdVersion:
            if( size == (sizeof(TiqiaaUsbIr_VersionPacket) + 2) ) {
                TiqiaaUsbIr_VersionPacket * version = (TiqiaaUsbIr_VersionPacket *)(pack + 2);
                DeviceState = version->State;
            }
            break;
        case CmdIdleMode:
        case CmdSendMode:
        case CmdRecvMode:
        case CmdOutput:
        case CmdCancel:
        case CmdUnknown:
            DeviceState = pack[2];
            break;
        case CmdData:
            TiqiaaUsbIr_IrRecvCallback * RecvCallback = IrRecvCallback;
            if( RecvCallback ) RecvCallback(pack + 2, size - 2, this, IrRecvCbContext);
            break;
    }
}

void *TiqiaaUsbIr::RunReadThreadFn(void *pcls)
{
    if( pcls == NULL ) return NULL;
    TiqiaaUsbIr* cls = static_cast<TiqiaaUsbIr*>(pcls);
    cls->ReadThreadFn();
    return 0;
}

void TiqiaaUsbIr::ReadThreadFn() {
    uint8_t FragmBuf[64];
    uint8_t PackBuf[MaxUsbPacketSize];
    int PackSize;
    int FragmSize;
    uint8_t PacketIdx;
    uint8_t FragmCount;
    uint8_t LastFragmIdx;
    TiqiaaUsbIr_Report2Header * ReportHdr = (TiqiaaUsbIr_Report2Header *)FragmBuf;
    int UsbRxSize;

    FragmCount = 0; // not receiving packet
    while( ReadActive ) {
        if( libusb_bulk_transfer(dev_h, ReadPipeId, FragmBuf, 64, &UsbRxSize, 0) < 0 )
            continue;

        if( !((UsbRxSize > sizeof(TiqiaaUsbIr_Report2Header)) && (ReportHdr->ReportId == ReadReportId) && ((uint32_t)(ReportHdr->FragmSize + 2) <= UsbRxSize)) )
            continue;

        if( FragmCount ) { // adding data to existing packet
            if( (ReportHdr->PacketIdx == PacketIdx) && (ReportHdr->FragmCount == FragmCount) && (ReportHdr->FragmIdx == (LastFragmIdx + 1)) ) {
                LastFragmIdx++;
            } else { // wrong fragment - drop packet
                FragmCount = 0;
            }
        }
        if( FragmCount == 0 ) { // new packet
            if( (ReportHdr->FragmCount > 0) && (ReportHdr->FragmIdx == 1) ) {
                PacketIdx = ReportHdr->PacketIdx;
                FragmCount = ReportHdr->FragmCount;
                PackSize = 0;
                LastFragmIdx = 1;
            }
        }
        if( FragmCount ) {
            FragmSize = ReportHdr->FragmSize + 2 - sizeof(TiqiaaUsbIr_Report2Header);
            if( (PackSize + FragmSize) <= MaxUsbPacketSize ) {
                memcpy(PackBuf + PackSize, FragmBuf + sizeof(TiqiaaUsbIr_Report2Header), FragmSize);
                PackSize += FragmSize;
                if( (ReportHdr->FragmIdx == LastFragmIdx) && (PackSize > 6) ) {
                    if( (*((uint16_t *)(PackBuf)) == PackStartSign) && (*((uint16_t *)(PackBuf + PackSize - 2)) == PackEndSign) ) {
                        ProcessRecvPacket(PackBuf + 2, PackSize - 4);
                    }
                }
            } else // buffer overflow - drop packet
                FragmCount = 0;
        }
    }
}
