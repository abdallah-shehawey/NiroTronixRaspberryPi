# NiroTronix Raspberry Pi motor control

Python and C++ libraries for NiroTronix MD.1.15.40, MD.1.30.40, MD.2.15.40 and MD.2.15.40T. Both use 25 kHz hardware PWM for speed and GPIO for direction on Raspberry Pi 3, 4 and 5 running Raspberry Pi OS.

See the [wiring guide](https://nirotronix.com/raspberry-pi-dc-motor-driver-wiring/) for the pin table, PWM overlay, power-up order and copyable sources. Follow those steps before powering a motor. Keep motor power off until the demo reports that PWM is off. Disconnect the Raspberry Pi before using the MD.2.15.40T onboard test buttons, which can put 5 V on the GPIO lines.

## Python

Install [nirotronix-motor from PyPI](https://pypi.org/project/nirotronix-motor/) on Raspberry Pi OS.

```sh
sudo apt install python3-venv python3-gpiozero python3-lgpio
python3 -m venv --system-site-packages .venv
. .venv/bin/activate
python -m pip install nirotronix-motor
nirotronix-motor
```

Use `nirotronix-motor --motor 2` for the second channel on a dual-channel board. If you cloned this repository, you can install the source with `python -m pip install ./python`.

```python
from nirotronix_motor import Motor, PINS, find_pwm_chip

motor = Motor(find_pwm_chip(), **PINS[1])
try:
    motor.stop()
    # Your control loop goes here. Wait for a full stop before reversing.
finally:
    motor.close()
```

## C++

Include `nirotronix_motor.hpp` in a C++17 project with Linux GPIO v2 headers, or add this checkout as a CMake dependency.

```cmake
add_subdirectory(NiroTronixRaspberryPi/cpp)
target_link_libraries(my_app PRIVATE NiroTronix::Motor)
```

To build and run the demo from the repository root:

```sh
sudo apt install g++ cmake
cmake -S cpp -B build
cmake --build build
./build/motor_demo
```

Use `./build/motor_demo --motor 2` for the second channel. To install the library for other projects, run `cmake --install build --prefix /your/install/path`, set `CMAKE_PREFIX_PATH` to that prefix, and use `find_package(NiroTronixMotor CONFIG REQUIRED)` before linking `NiroTronix::Motor`.

### Install with vcpkg

In your project, add the following entry under `registries` in `vcpkg-configuration.json`. Keep your existing default registry. For a new project, start with `vcpkg new --application`.

```json
{
  "kind": "git",
  "repository": "https://github.com/abdallah-shehawey/NiroTronixRaspberryPi.git",
  "baseline": "07b5efe23d39e749fe82aa74a31d65b34589ae2c",
  "packages": ["nirotronix-motor"]
}
```

```sh
vcpkg add port nirotronix-motor
vcpkg install
```

Build on Linux with the vcpkg CMake toolchain and link the installed library.

```cmake
find_package(NiroTronixMotor CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE NiroTronix::Motor)
```

## Demo controls and hardware status

Both demos start with PWM off. `S` turns PWM off, `A` confirms that the shaft has stopped, `F` or `R` starts one direction at 25% duty, and `Q` exits. Stop the motor and wait for the shaft to come to rest before reversing. Keep a motor-power switch within reach. A process crash or `kill -9` can leave the kernel PWM running.

The Python and C++ packages were checked on Linux using simulated PWM files. The examples still need a hardware test on a Raspberry Pi connected to a NiroTronix driver.
