#include <cstdint>
#include <cstdio>
#include <iostream>
#include <ostream>
#include <thread>

#include "CLI11.hpp"
#include "TiqiaaUsb.h"
#include "ctqirsignal.h"

static bool waiting;
static CTqIrSignal signal;

void irRecvCallback(uint8_t *data, int size, class TiqiaaUsbIr *IrCls,
                    void *context) {
  std::cout << "Data: " << (unsigned) *data << " Size: " << size << std::endl;
  std::cout << "Tiqiaa: " << signal.FromTiqiaa(data, size) << std::endl;
  std::cout << "Data: " << (unsigned) *data << " Size: " << size << std::endl;
  waiting = false;
}

int main(int argc, char **argv) {
  CLI::App app{"Tiqiaa USB - cli"};

  uint16_t receiveNec = 0;
  CLI::Option *receiveNecOpt = app.add_option(
      "-r,--receiveNec", receiveNec, "Receive a NEC code (hexadecimal)");

  uint16_t sendNec = 0;
  CLI::Option *sendNecOpt = app.add_option(
      "-s,--send", sendNec, "Send a NEC code (hexadecimal), e.g.: 0x8002");

  CLI11_PARSE(app, argc, argv);

  TiqiaaUsbIr Ir;
  Ir.IrRecvCallback = &irRecvCallback;

  if (!Ir.Open()) {
    std::cout << "Could not open the device." << std::endl;
    return 1;
  }

  if (*sendNecOpt) {
    std::cerr << "Sending..." << std::endl;

    if (Ir.SendNecSignal(sendNec)) {
      std::cout << "Sent code successfully" << std::endl;
    } else {
      std::cout << "Send failure" << std::endl;
    }

    std::cerr << "Sent." << std::endl;
  }

  if (*receiveNecOpt) {
    std::cerr << "Receiving..." << std::endl;
    waiting = true;
    Ir.StartRecvIR();
    while (waiting) {
      std::this_thread::yield();
    }
  }

  Ir.Close();

  return 0;
}
