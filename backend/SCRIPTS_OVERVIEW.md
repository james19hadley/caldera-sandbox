# Caldera Backend - Script Overview

## Universal Scripts (Recommended)

### `sensor_setup.sh` - Universal sensor management
**Purpose**: Handles all depth sensors (Kinect V1, V2, future sensors)

```bash
./sensor_setup.sh              # Auto-detect and setup everything
./sensor_setup.sh list         # Show connected sensors  
./sensor_setup.sh setup        # Setup for all sensors
./sensor_setup.sh setup kinect2 # Setup only for Kinect V2
./sensor_setup.sh test kinect1  # Test Kinect V1 (when implemented)
./sensor_setup.sh test kinect2  # Test Kinect V2
./sensor_setup.sh check        # Quick status check
./sensor_setup.sh fix          # Fix permissions
```

**Features**:
- Supports multiple sensor types in one script
- Auto-detects connected hardware
- Future-proof for new sensors
- Comprehensive udev rules

## Legacy Scripts (Still functional)

### `kinect_setup.sh` - Kinect V2 only
**Purpose**: Kinect V2 specific setup and testing

```bash
./kinect_setup.sh              # Auto-detect Kinect V2 setup
./kinect_setup.sh setup        # Setup system for Kinect V2
./kinect_setup.sh test         # Test Kinect V2 connection
./kinect_setup.sh check        # Quick Kinect V2 check
```

### `check_kinect_connection.sh` - Quick diagnostic
**Purpose**: Fast permission and connection check for Kinect V2

```bash
./check_kinect_connection.sh   # Quick diagnostic
```

## Build & Test Scripts

### `build.sh` - Project compilation
```bash
./build.sh                     # Clean build
./build.sh -i                  # Incremental build
```

### `test.sh` - Run test suite
```bash
./test.sh                      # All tests
./build/tests/CalderaTests --gtest_filter="KinectV2*"  # Specific tests
```

## Migration Path

1. **New projects**: Use `sensor_setup.sh` (supports all sensors)
2. **Existing Kinect V2 setups**: Continue using `kinect_setup.sh` or migrate to `sensor_setup.sh`
3. **Multi-sensor projects**: Use `sensor_setup.sh` exclusively

## File Locations

- Main scripts: `backend/sensor_setup.sh`, `backend/kinect_setup.sh`
- Build scripts: `backend/build.sh`, `backend/test.sh`
- Legacy rules: `/etc/udev/rules.d/90-kinect2.rules`
- Universal rules: `/etc/udev/rules.d/90-caldera-sensors.rules`

## Supported Hardware Matrix

| Sensor | USB | `sensor_setup.sh` | `kinect_setup.sh` | Implementation |
|--------|-----|-------------------|-------------------|----------------|
| Kinect V1 (Xbox 360) | USB 2.0 | ✅ | ❌ | Planned |
| Kinect V2 (Xbox One) | USB 3.0 | ✅ | ✅ | Complete |
| Intel RealSense      | USB 3.0 | 🔄 | ❌ | Future |
| Azure Kinect         | USB 3.0 | 🔄 | ❌ | Future |

**Legend**: ✅ Supported, ❌ Not supported, 🔄 Framework ready