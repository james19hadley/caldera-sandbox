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

- **Kinect V2** (Xbox One Kinect) - Full support
- **Kinect V1** (Xbox 360 Kinect) - Planned support
- **USB**: USB 3.0 port (Kinect V2) or USB 2.0 (Kinect V1)
- **OS**: Linux with udev support
- **Permissions**: User must be in `plugdev` group

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