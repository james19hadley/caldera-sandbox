# Caldera Backend - Depth Sensor Setup

## Quick Start

1. **Auto-detect and setup** (requires sudo for udev rules):
   ```bash
   ./sensor_setup.sh              # Detects and sets up all connected sensors
   ```

2. **Build the project**:
   ```bash
   ./build.sh
   ```

3. **Test sensor connections**:
   ```bash
   ./sensor_setup.sh list         # List connected sensors
   ./sensor_setup.sh test         # Test all sensors
   ./sensor_setup.sh test kinect2 # Test specific sensor
   ```

## Supported Hardware

- **Kinect V2** (Xbox One Kinect) – Full support via `libfreenect2` (vcpkg managed)
- **Kinect V1** (Xbox 360 / Kinect for Windows v1) – Experimental via vendored `libfreenect` submodule (auto-built)
- **USB**: USB 3.0 port (Kinect V2) / USB 2.0 (Kinect V1)
- **OS**: Linux with udev support
- **Permissions**: User must be in `plugdev` group

Note: You can disable Kinect v1 entirely now with: `-DCALDERA_WITH_KINECT_V1=OFF` when invoking CMake (or editing the build script to pass it). When OFF a stub is used if `CALDERA_SENSOR_TYPE=kinect1` is requested at runtime.

## Kinect v1 Details

Because upstream vcpkg does not ship a port for the original `libfreenect` (only `libfreenect2`), the project vendors the library as a git submodule under `vendor/libfreenect` and auto-builds it during `./build.sh` if not already built.

### Status
* Submodule required: `git submodule update --init --recursive`
* Auto-build: `backend/build.sh` compiles `vendor/libfreenect` (Release, examples OFF) once.
* CMake discovers the built library via manual `find_path`/`find_library` and fails fast if missing (deterministic builds).
* Future toggle will allow excluding it entirely for minimal CI environments.

### Using the Vendored Path (Recommended)
```
git submodule update --init --recursive
cd backend
./build.sh
```
Artifacts: `vendor/libfreenect/build/lib/libfreenect.(a|so)`

If configuration fails:
1. Verify submodule: `test -d vendor/libfreenect`
2. Verify build output: `ls vendor/libfreenect/build/lib`
3. Re-run clean build if needed: `rm -rf backend/build vendor/libfreenect/build && (cd backend && ./build.sh)`

### Manual System Install (Alternative)
Not required, but if you want a system-wide install:
```
sudo apt-get update
sudo apt-get install -y build-essential cmake libusb-1.0-0-dev libturbojpeg0-dev
git clone https://github.com/OpenKinect/libfreenect.git
cd libfreenect && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_FAKENECT=OFF -DBUILD_OPENNI2_DRIVER=OFF ..
make -j$(nproc)
sudo make install && sudo ldconfig
```
Currently the project expects the vendored layout; removing `vendor/libfreenect` will cause a CMake failure until an optional toggle is added.

### Verifying Detection
```
ls vendor/libfreenect/build/lib/libfreenect.*
grep -R "libfreenect(v1) include:" backend/build/CMakeCache.txt 2>/dev/null || echo "Check CMake configure output"
```

### Runtime Permissions (Kinect v1)
Example udev rules (add if not already present):
```
# /etc/udev/rules.d/51-kinect-v1.rules
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02ae", MODE="0660", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02ad", MODE="0660", GROUP="plugdev"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02b0", MODE="0660", GROUP="plugdev"
```
Then:
```
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo usermod -aG plugdev $USER
newgrp plugdev
```

### Why Not a vcpkg Port?
* Only `libfreenect2` is packaged upstream today.
* A port overlay is planned for later to remove manual vendor logic.

### Future Enhancements (v1)
* Improve stub diagnostics when `CALDERA_WITH_KINECT_V1=OFF`
* vcpkg overlay port
* Frame translation validation (depth scaling, RGB packing) and sync
* Tests differentiating hardware vs stub path

## Troubleshooting

### "Access denied" or "insufficient permissions"
```bash
./check_kinect.sh  # Check setup
# If issues found:
./setup_system.sh  # Re-run setup
# Then log out/in or restart
```

### Kinect not detected
1. Check USB connection: `lsusb | grep 045e:02c4`
2. Try different USB 3.0 port
3. Unplug/replug Kinect after setup

### Build issues  
```bash
./build.sh         # Clean rebuild
```

## Files Overview

- `setup_system.sh` - One-time system configuration (needs sudo)
- `check_kinect.sh` - Quick permission and device check (no sudo)
- `build.sh` - Build the project 
- `test.sh` - Run all tests
- `/etc/udev/rules.d/90-kinect2.rules` - USB device permissions (created by setup)

## Security Note

The setup adds your user to the `plugdev` group and creates udev rules that allow this group to access Kinect V2 devices. This is the standard approach used by libfreenect2 and similar hardware libraries.