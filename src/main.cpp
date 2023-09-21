#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

#include "TiqiaaUsb.h"
#include "ctqirsignal.h"

static bool waiting;
static CTqIrSignal signal;

void irRecvCallback(uint8_t *data, int size, class TiqiaaUsbIr *IrCls,
                    void *context) {
  std::cout << "Size: " << size << std::endl;
  signal.FromTiqiaa(data, size);
  waiting = false;
}

int main(int argc, char **argv) {
  TiqiaaUsbIr Ir;
  Ir.IrRecvCallback = &irRecvCallback;

  if (!Ir.Open()) {
    std::cerr << "Could not open the device." << std::endl;
    return 1;
  }

  std::cout << "Sending..." << std::endl;
  uint16_t code1 = 0x8002;
  uint16_t code2 = 0x8004;
  uint16_t code3 = 0x8006;

  if (Ir.SendNecSignal(0x8002)) {
    printf("Send good\n");
  } else {
    printf("Send bad\n");
  }

  std::cout << "Sent." << std::endl;

  Ir.Close();
  return 0;
}
