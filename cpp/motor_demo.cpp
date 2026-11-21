// NiroTronix MD boards on a Raspberry Pi: manual PWM/DIR demonstration in C++.
//
// Wiring (Raspberry Pi 40-pin header):
//   PWM1 <- GPIO12 (pin 32)   DIR1 <- GPIO16 (pin 36)
//   PWM2 <- GPIO13 (pin 33)   DIR2 <- GPIO26 (pin 37)   (MD.2.15.40 and MD.2.15.40T)
//   GND  <- pin 34 (or any Pi GND)
//   +5V  <- pin 2 or 4, on boards with a +5V logic input (not MD.2.15.40T)
// Motor power is a separate, fused supply on POWER and GND.
//
// One-time setup, then reboot:
//   /boot/firmware/config.txt:  dtoverlay=pwm-2chan,pin=12,func=4,pin2=13,func2=4
// Build:  g++ -std=c++17 -O2 -Wall -o motor_demo motor_demo.cpp
// Run:    ./motor_demo            (motor 1)
//         ./motor_demo --motor 2  (motor 2 on a dual channel board)
//
// Commands, each followed by Enter: S = PWM off; A = confirm the shaft has stopped;
// F/R = start one direction at 25 % duty after that confirmation; Q = PWM off and quit.
// There is no shaft feedback, current limit or emergency stop in this example.
#include "nirotronix_motor.hpp"

#include <csignal>
#include <cctype>
#include <iostream>
#include <memory>

namespace {
nirotronix::Motor* g_motor = nullptr;

void onSignal(int) {
    if (g_motor) g_motor->zeroFromSignal();  // PWM to 0 before the process ends
    _exit(0);
}
}  // namespace

int main(int argc, char** argv) {
    int motorNo = 1;
    int chipNo = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--motor" && i + 1 < argc) motorNo = std::stoi(argv[++i]);
        else if (a == "--chip" && i + 1 < argc) chipNo = std::stoi(argv[++i]);
        else { std::cerr << "usage: " << argv[0] << " [--motor 1|2] [--chip N]\n"; return 2; }
    }
    if (motorNo != 1 && motorNo != 2) { std::cerr << "--motor must be 1 or 2\n"; return 2; }
    const int pwmChannel = motorNo == 1 ? 0 : 1;
    const unsigned dirGpio = motorNo == 1 ? 16 : 26;
    const double runDuty = 0.25;

    try {
        std::string chip = chipNo >= 0 ? "/sys/class/pwm/pwmchip" + std::to_string(chipNo) : nirotronix::findPwmChip();
        auto motor = std::make_unique<nirotronix::Motor>(chip, pwmChannel, dirGpio);
        g_motor = motor.get();
        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        std::cout << "Motor " << motorNo << ", PWM 25 kHz on " << chip.substr(chip.rfind('/') + 1) << ". PWM OFF.\n"
                  << "S=stop, A=confirm stopped, F/R=start at 25 % duty, Q=quit.\n";
        bool running = false, stoppedConfirmed = false;
        std::string line;
        while (std::getline(std::cin, line)) {
            const char c = line.empty() ? '\0' : static_cast<char>(std::toupper(static_cast<unsigned char>(line[0])));
            if (c == 'S') {
                motor->stop();
                running = stoppedConfirmed = false;
                std::cout << "PWM OFF. Wait for the shaft to stop; then send A.\n";
            } else if (c == 'A') {
                if (running) std::cout << "Send S first and wait for the shaft to stop.\n";
                else { stoppedConfirmed = true; std::cout << "Stopped confirmed. F or R will start the motor.\n"; }
            } else if (c == 'F' || c == 'R') {
                if (running || !stoppedConfirmed) { std::cout << "Start blocked. Send S, wait for a full stop, then send A.\n"; continue; }
                motor->run(c == 'R', runDuty);
                running = true;
                stoppedConfirmed = false;
                std::cout << "Running at 25 % duty. S turns PWM off.\n";
            } else if (c == 'Q') {
                break;
            }
        }
        g_motor = nullptr;
        motor.reset();  // PWM off, DIR low
        std::cout << "PWM OFF.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
