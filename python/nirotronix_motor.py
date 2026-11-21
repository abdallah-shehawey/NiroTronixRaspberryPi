#!/usr/bin/env python3
"""NiroTronix MD boards on a Raspberry Pi: PWM/DIR control with 25 kHz hardware PWM.

Wiring (Raspberry Pi 40-pin header):
  PWM1 <- GPIO12 (pin 32)   DIR1 <- GPIO16 (pin 36)
  PWM2 <- GPIO13 (pin 33)   DIR2 <- GPIO26 (pin 37)   (MD.2.15.40 and MD.2.15.40T)
  GND  <- pin 34 (or any Pi GND)
  +5V  <- pin 2 or 4, on boards with a +5V logic input (not MD.2.15.40T)
Motor power is a separate, fused supply on POWER and GND.

One-time setup, then reboot:
  /boot/firmware/config.txt:  dtoverlay=pwm-2chan,pin=12,func=4,pin2=13,func2=4
  sudo apt install python3-gpiozero

Run:  python3 nirotronix_motor.py            (motor 1)
      python3 nirotronix_motor.py --motor 2  (motor 2 on a dual channel board)

Commands, each followed by Enter: S = PWM off; A = confirm the shaft has stopped;
F/R = start one direction at 25 % duty after that confirmation; Q = PWM off and quit.
There is no shaft feedback, current limit or emergency stop in this example.
"""
import argparse
import os
import re
import signal
import sys
import time

from gpiozero import DigitalOutputDevice

PWM_ROOT = "/sys/class/pwm"
PINS = {1: {"pwm_channel": 0, "dir_gpio": 16}, 2: {"pwm_channel": 1, "dir_gpio": 26}}
# The PWM block that drives GPIO12/13: fe20c000.pwm on a Pi 4 (7e20c000 / 3f20c000 /
# 2020c000 on older models), 1f00098000.pwm on a Pi 5. The Pi 5 fan uses another block.
GPIO_PWM_DEVICE = re.compile(r"(20c000|1f00098000)\.pwm$")


def find_pwm_chip(root=None):
    """Return the pwmchip directory that drives GPIO12/13."""
    root = root or PWM_ROOT
    chips = sorted(n for n in os.listdir(root) if n.startswith("pwmchip")) if os.path.isdir(root) else []
    if not chips:
        raise RuntimeError("No hardware PWM found. Add the dtoverlay line to config.txt and reboot.")
    named = [c for c in chips if GPIO_PWM_DEVICE.search(os.path.basename(os.path.realpath(os.path.join(root, c, "device"))))]
    if len(named) == 1:
        return os.path.join(root, named[0])
    if len(chips) == 1:
        return os.path.join(root, chips[0])
    raise RuntimeError("Several PWM chips found (%s). Select the one that drives GPIO12." % ", ".join(chips))


class HardwarePWM:
    """One channel of the kernel's sysfs PWM interface."""

    def __init__(self, chip_dir, channel, frequency_hz=25000):
        self.dir = os.path.join(chip_dir, "pwm%d" % channel)
        if not os.path.isdir(self.dir):
            self._write(os.path.join(chip_dir, "export"), channel)
        self.period_ns = round(1e9 / frequency_hz)
        # udev gives the new files to the gpio group a moment after export.
        for _ in range(50):
            try:
                self._write(os.path.join(self.dir, "duty_cycle"), 0)
                break
            except (PermissionError, FileNotFoundError):
                time.sleep(0.02)
        else:
            raise PermissionError("Cannot write %s. Check the PWM device permissions." % self.dir)
        self._write(os.path.join(self.dir, "period"), self.period_ns)
        self._write(os.path.join(self.dir, "duty_cycle"), 0)
        self._write(os.path.join(self.dir, "enable"), 1)

    @staticmethod
    def _write(path, value):
        with open(path, "w") as f:
            f.write(str(value))

    def duty(self, fraction):
        """Set the duty cycle, 0.0 to 1.0."""
        fraction = min(max(fraction, 0.0), 1.0)
        self._write(os.path.join(self.dir, "duty_cycle"), round(self.period_ns * fraction))

    def off(self):
        self.duty(0)
        self._write(os.path.join(self.dir, "enable"), 0)


class Motor:
    """One driver channel: PWM sets the speed, DIR the direction."""

    def __init__(self, chip_dir, pwm_channel, dir_gpio, frequency_hz=25000):
        self.dir = DigitalOutputDevice(dir_gpio, initial_value=False)
        try:
            self.pwm = HardwarePWM(chip_dir, pwm_channel, frequency_hz)
        except Exception:
            self.dir.close()
            raise

    def run(self, reverse, fraction):
        self.dir.value = bool(reverse)
        self.pwm.duty(fraction)

    def stop(self):
        self.pwm.duty(0)

    def close(self):
        try:
            self.pwm.off()
            self.dir.off()
        finally:
            self.dir.close()


def main():
    ap = argparse.ArgumentParser(description="NiroTronix PWM/DIR motor driver demo for Raspberry Pi")
    ap.add_argument("--motor", type=int, choices=(1, 2), default=1, help="driver channel (2 = second motor)")
    ap.add_argument("--chip", type=int, help="pwmchip number, if it cannot be found automatically")
    args = ap.parse_args()
    try:
        chip = os.path.join(PWM_ROOT, "pwmchip%d" % args.chip) if args.chip is not None else find_pwm_chip()
        motor = Motor(chip, **PINS[args.motor])
    except (OSError, RuntimeError) as error:
        ap.exit(1, f"{error}\n")
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))  # run the finally block on kill
    run_duty = 0.25
    running = False
    stopped_confirmed = False
    print("Motor %d, PWM 25 kHz on %s. PWM OFF." % (args.motor, os.path.basename(chip)))
    print("S=stop, A=confirm stopped, F/R=start at 25 %% duty, Q=quit.")
    try:
        for line in sys.stdin:
            command = line.strip().upper()[:1]
            if command == "S":
                motor.stop()
                running, stopped_confirmed = False, False
                print("PWM OFF. Wait for the shaft to stop; then send A.")
            elif command == "A":
                if running:
                    print("Send S first and wait for the shaft to stop.")
                else:
                    stopped_confirmed = True
                    print("Stopped confirmed. F or R will start the motor.")
            elif command in ("F", "R"):
                if running or not stopped_confirmed:
                    print("Start blocked. Send S, wait for a full stop, then send A.")
                    continue
                motor.run(command == "R", run_duty)
                running, stopped_confirmed = True, False
                print("Running at 25 % duty. S turns PWM off.")
            elif command == "Q":
                break
    except KeyboardInterrupt:
        pass
    finally:
        motor.close()
        print("PWM OFF.")


if __name__ == "__main__":
    main()
