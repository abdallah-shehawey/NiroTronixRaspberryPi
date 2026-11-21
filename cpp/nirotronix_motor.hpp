// NiroTronix MD boards on a Raspberry Pi: PWM/DIR control with 25 kHz hardware PWM.
// No libraries beyond the Linux kernel headers: sysfs PWM for speed, the GPIO
// character device (uAPI v2, kernel 5.10+) for direction.
#pragma once

#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <limits.h>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace nirotronix {

// Find the pwmchip that drives GPIO12/13: fe20c000.pwm on a Pi 4 (7e20c000 /
// 3f20c000 / 2020c000 on older models), 1f00098000.pwm on a Pi 5, whose fan uses
// another PWM block. Falls back to the only chip if there is just one.
inline std::string findPwmChip(const std::string& root = "/sys/class/pwm") {
    std::vector<std::string> chips, named;
    if (DIR* d = opendir(root.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.rfind("pwmchip", 0) == 0) chips.push_back(n);
        }
        closedir(d);
    }
    std::sort(chips.begin(), chips.end());
    if (chips.empty()) throw std::runtime_error("No hardware PWM found. Add the dtoverlay line to config.txt and reboot.");
    static const std::regex gpioPwm("(20c000|1f00098000)\\.pwm$");
    for (const auto& c : chips) {
        char buf[PATH_MAX];
        if (realpath((root + "/" + c + "/device").c_str(), buf) && std::regex_search(std::string(buf), gpioPwm)) named.push_back(c);
    }
    if (named.size() == 1) return root + "/" + named[0];
    if (chips.size() == 1) return root + "/" + chips[0];
    throw std::runtime_error("Several PWM chips found. Pass --chip N for the one that drives GPIO12.");
}

// One channel of the kernel's sysfs PWM interface.
class HardwarePwm {
public:
    HardwarePwm(const std::string& chipDir, int channel, unsigned frequencyHz = 25000)
        : dir_(chipDir + "/pwm" + std::to_string(channel)),
          periodNs_(static_cast<long>(std::lround(1e9 / frequencyHz))) {
        if (access(dir_.c_str(), F_OK) != 0) write(chipDir + "/export", channel);
        // udev gives the new files to the gpio group a moment after export.
        bool ok = false;
        for (int i = 0; i < 50 && !ok; ++i) {
            ok = tryWrite(dir_ + "/duty_cycle", 0);
            if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!ok) throw std::runtime_error("Cannot write " + dir_ + ". Run with sudo, or add your user to the gpio group.");
        write(dir_ + "/period", periodNs_);
        write(dir_ + "/duty_cycle", 0);
        write(dir_ + "/enable", 1);
        dutyFd_ = open((dir_ + "/duty_cycle").c_str(), O_WRONLY);
    }
    ~HardwarePwm() { off(); if (dutyFd_ >= 0) close(dutyFd_); }
    HardwarePwm(const HardwarePwm&) = delete;
    HardwarePwm& operator=(const HardwarePwm&) = delete;

    // Duty cycle, 0.0 to 1.0.
    void duty(double fraction) {
        fraction = std::clamp(fraction, 0.0, 1.0);
        write(dir_ + "/duty_cycle", std::lround(periodNs_ * fraction));
    }
    void off() {
        tryWrite(dir_ + "/duty_cycle", 0);
        tryWrite(dir_ + "/enable", 0);
    }
    // For a signal handler: only write(2) on an already-open file.
    void zeroFromSignal() const {
        if (dutyFd_ >= 0) { ssize_t r = ::pwrite(dutyFd_, "0", 1, 0); (void)r; }
    }

private:
    template <typename T> static bool tryWrite(const std::string& path, T value) {
        std::ofstream f(path);
        if (!f) return false;
        f << value;
        f.flush();
        return static_cast<bool>(f);
    }
    template <typename T> static void write(const std::string& path, T value) {
        if (!tryWrite(path, value)) throw std::runtime_error("Cannot write " + path);
    }
    std::string dir_;
    long periodNs_;
    int dutyFd_ = -1;
};

// One output line on the GPIO character device.
class OutputLine {
public:
    explicit OutputLine(unsigned gpio) {
        int chip = openPiGpioChip();
        gpio_v2_line_request req{};
        req.offsets[0] = gpio;
        req.num_lines = 1;
        std::strncpy(req.consumer, "nirotronix-motor", sizeof(req.consumer) - 1);
        req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
        req.config.num_attrs = 1;
        req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        req.config.attrs[0].attr.values = 0;  // start LOW
        req.config.attrs[0].mask = 1;
        int rc = ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &req);
        close(chip);
        if (rc < 0) throw std::runtime_error("Cannot claim GPIO" + std::to_string(gpio) + ": " + std::strerror(errno));
        fd_ = req.fd;
    }
    ~OutputLine() { if (fd_ >= 0) { trySet(false); close(fd_); } }
    OutputLine(const OutputLine&) = delete;
    OutputLine& operator=(const OutputLine&) = delete;

    void set(bool high) {
        if (!trySet(high)) throw std::runtime_error("Cannot set the DIR line");
    }

private:
    bool trySet(bool high) noexcept {
        gpio_v2_line_values v{};
        v.mask = 1;
        v.bits = high ? 1 : 0;
        return ioctl(fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &v) == 0;
    }

    // The 40-pin header lines: pinctrl-rp1 on a Pi 5, pinctrl-bcm2711 on a Pi 4,
    // pinctrl-bcm2835 on older models. The chip number changes between kernels.
    static int openPiGpioChip() {
        for (int n = 0; n < 16; ++n) {
            std::string path = "/dev/gpiochip" + std::to_string(n);
            int fd = open(path.c_str(), O_RDWR | O_CLOEXEC);
            if (fd < 0) continue;
            gpiochip_info info{};
            if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0) {
                std::string label = info.label;
                if (label == "pinctrl-rp1" || label == "pinctrl-bcm2711" || label == "pinctrl-bcm2835") return fd;
            }
            close(fd);
        }
        throw std::runtime_error("No Raspberry Pi GPIO chip found in /dev/gpiochip*.");
    }
    int fd_ = -1;
};

// One driver channel: PWM sets the speed, DIR the direction.
class Motor {
public:
    Motor(const std::string& chipDir, int pwmChannel, unsigned dirGpio, unsigned frequencyHz = 25000)
        : dir_(dirGpio), pwm_(chipDir, pwmChannel, frequencyHz) {}
    void run(bool reverse, double fraction) { dir_.set(reverse); pwm_.duty(fraction); }
    void stop() { pwm_.duty(0); }
    void zeroFromSignal() const { pwm_.zeroFromSignal(); }

private:
    OutputLine dir_;
    HardwarePwm pwm_;
};

}  // namespace nirotronix
