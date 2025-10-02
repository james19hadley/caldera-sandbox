#!/bin/bash
# Caldera Backend - Universal Sensor Setup
# Simple, extensible sensor management

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'
CYAN='\033[0;36m'

RULES_FILE="/etc/udev/rules.d/90-caldera-sensors.rules"
TARGET_USER="${SUDO_USER:-$USER}"

# OS detection (ID/ID_LIKE from /etc/os-release)
OS_ID="unknown"
OS_LIKE=""
OS_FAMILY="unknown"
if [ -f /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    OS_ID="$ID"
    OS_LIKE="$ID_LIKE"
fi
case "${OS_ID}:${OS_LIKE}" in
    *ubuntu*:*|ubuntu:*) OS_FAMILY="ubuntu" ;;
    debian:*|*debian*)   OS_FAMILY="ubuntu" ;;
    fedora:*|*fedora*|rhel:*|*rhel*|*centos*) OS_FAMILY="fedora" ;;
    *) OS_FAMILY="other" ;;
esac

# Libfreenect LED helpers (best-effort; no-op if utils are absent)
_led_cmd_exists() {
    command -v freenect-cmd >/dev/null 2>&1 || \
    command -v freenect-set-led >/dev/null 2>&1 || \
    command -v freenect-led >/dev/null 2>&1
}

_led_try() {
    # Usage: _led_try <NAME|CODE>
    local val="$1"
    if command -v freenect-cmd >/dev/null 2>&1; then
        # Accept numeric or known names mapped to numeric
        case "$val" in
            OFF) val=0 ;;
            GREEN) val=1 ;;
            RED) val=2 ;;
            YELLOW) val=3 ;;
            BLINK_GREEN) val=4 ;;
            BLINK_RED_YELLOW) val=6 ;;
        esac
        freenect-cmd led "$val" >/dev/null 2>&1 || true
        return 0
    elif command -v freenect-set-led >/dev/null 2>&1; then
        freenect-set-led "$val" >/dev/null 2>&1 || true
        return 0
    elif command -v freenect-led >/dev/null 2>&1; then
        freenect-led "$val" >/dev/null 2>&1 || true
        return 0
    fi
    return 1
}

_kinect_v1_present() {
    lsusb | grep -q "045e:02b0"
}

_led_blink_pid_file="/tmp/caldera_kinect_v1_led_blink.pid"
_led_blink_yellow_start() {
    _led_cmd_exists || return 0
    _kinect_v1_present || return 0
    # Background blink: YELLOW <-> OFF while check runs
    (
        # small guard to prevent runaway
        for i in $(seq 1 40); do
            _led_try YELLOW || true
            sleep 0.25
            _led_try OFF || true
            sleep 0.25
            # stop early if pidfile removed
            [ -f "$_led_blink_pid_file" ] || exit 0
        done
    ) &
    echo $! > "$_led_blink_pid_file"
}

_led_blink_stop_and_set() {
    # arg: final color name (e.g., GREEN or YELLOW)
    local final="${1:-GREEN}"
    if [ -f "$_led_blink_pid_file" ]; then
        local pid
        pid=$(cat "$_led_blink_pid_file" 2>/dev/null || true)
        rm -f "$_led_blink_pid_file"
        if [ -n "$pid" ]; then
            kill "$pid" >/dev/null 2>&1 || true
            wait "$pid" 2>/dev/null || true
        fi
    fi
    _led_cmd_exists && _kinect_v1_present && _led_try "$final" || true
}

# Supported sensors: vendor:product => description
declare -A SENSORS=(
    # Kinect V2 (Xbox One) - Currently supported
    ["045e:02c4"]="Kinect V2 Sensor"
    ["045e:02d8"]="Kinect V2 Audio"
    
    # Kinect V1 (Xbox 360) - Prepared for future support
    ["045e:02b0"]="Kinect V1 Motor"
    ["045e:02ad"]="Kinect V1 Audio" 
    ["045e:02ae"]="Kinect V1 Camera"
    
    # Future sensors can be added here:
    # ["8086:0aa5"]="Intel RealSense D435"
)

show_usage() {
    echo -e "${BLUE}Caldera Backend - Universal Sensor Setup${NC}"
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  setup    - Setup system for sensors (requires sudo)"
    echo "  check    - Check sensor status"
    echo "  test     - Test sensor connections"
    echo "  diagnose - Diagnose Kinect v1 camera/audio bring-up"
    echo "  list     - List connected sensors"
    echo ""
    echo "Examples:"
    echo "  $0 setup    # Setup system"
    echo "  $0 check    # Quick health check"
    echo "  $0 test     # Test with actual device access"
    echo ""
    echo "Kinect v1 notes: build with libfreenect and run hardware smoke test via:"
    echo "  CALDERA_REQUIRE_KINECT_V1=1 ./test.sh KinectV1_DeviceTest"
}

list_sensors() {
    echo -e "${CYAN}=== Connected Sensors ===${NC}"
    
    local found=false
    local lsusb_output=$(lsusb)
    
    for sensor_id in "${!SENSORS[@]}"; do
        if echo "$lsusb_output" | grep -q "$sensor_id"; then
            echo -e "${GREEN}✓ ${SENSORS[$sensor_id]} ($sensor_id)${NC}"
            found=true
        fi
    done
    
    if [ "$found" = false ]; then
        echo -e "${YELLOW}⚠ No supported sensors found${NC}"
        return 1
    fi
}

check_permissions() {
    local all_ok=true
    local lsusb_output=$(lsusb)
    
    echo -e "${CYAN}=== Checking Permissions ===${NC}"
    # Start LED hint (blink yellow) while we check
    _led_blink_yellow_start
    
    # Check if rules file exists
    if [ ! -f "$RULES_FILE" ]; then
        echo -e "${YELLOW}⚠ Udev rules not found${NC}"
        all_ok=false
    else
        echo -e "${GREEN}✓ Udev rules installed${NC}"
    fi

    # On Ubuntu/Debian we encourage plugdev membership if rules use GROUP="plugdev"
    if [ "$OS_FAMILY" = "ubuntu" ]; then
        if ! id -nG "$TARGET_USER" | grep -qw plugdev; then
            echo -e "${YELLOW}⚠ User '$TARGET_USER' is not in 'plugdev' group (recommended on Ubuntu/Debian).${NC}"
            echo -e "${YELLOW}  Add with: sudo usermod -aG plugdev $TARGET_USER && re-login${NC}"
            # Don't fail outright; device access may still be OK with MODE=0666
        else
            echo -e "${GREEN}✓ User '$TARGET_USER' is in 'plugdev' group${NC}"
        fi
    fi
    
    # Check device access for connected sensors
    for sensor_id in "${!SENSORS[@]}"; do
        if echo "$lsusb_output" | grep -q "$sensor_id"; then
            local bus_dev=$(echo "$lsusb_output" | grep "$sensor_id" | sed 's/.*Bus \([0-9]*\) Device \([0-9]*\).*/\1\/\2/')
            local device_path="/dev/bus/usb/$bus_dev"
            
            if [ -r "$device_path" ] && [ -w "$device_path" ]; then
                echo -e "${GREEN}✓ ${SENSORS[$sensor_id]} access OK${NC}"
            else
                echo -e "${YELLOW}⚠ ${SENSORS[$sensor_id]} access denied${NC}"
                all_ok=false
            fi
        fi
    done
    
    if [ "$all_ok" = true ]; then
        echo -e "${GREEN}✓ All permissions OK${NC}"
        _led_blink_stop_and_set GREEN
        return 0
    else
        echo -e "${YELLOW}Run: $0 setup${NC}"
        _led_blink_stop_and_set YELLOW
        return 1
    fi
}

setup_system() {
    echo -e "${CYAN}=== System Setup ===${NC}"
    # plugdev group for Ubuntu/Debian
    if [ "$OS_FAMILY" = "ubuntu" ]; then
        if ! getent group plugdev >/dev/null; then
            echo -e "${YELLOW}Creating 'plugdev' group (Ubuntu/Debian)...${NC}"
            sudo groupadd plugdev || true
        fi
        if ! id -nG "$TARGET_USER" | grep -qw plugdev; then
            echo -e "${YELLOW}Adding '$TARGET_USER' to 'plugdev' (re-login required)...${NC}"
            sudo usermod -aG plugdev "$TARGET_USER" || true
        fi
    fi

    # Create udev rules (Ubuntu: GROUP=plugdev; Fedora/other: MODE only)
    echo -e "${YELLOW}Creating udev rules...${NC}"
    if [ "$OS_FAMILY" = "ubuntu" ]; then
        sudo tee "$RULES_FILE" > /dev/null << 'EOF'
# Caldera Backend - Sensor udev rules (Ubuntu/Debian)
# Allow access to depth sensors for users in plugdev

# Kinect V2 (Xbox One)
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02c4", GROUP="plugdev", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02d8", GROUP="plugdev", MODE="0666"

# Kinect V1 (Xbox 360)
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02b0", GROUP="plugdev", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02ad", GROUP="plugdev", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02ae", GROUP="plugdev", MODE="0666"

# Intel RealSense (example)
# SUBSYSTEM=="usb", ATTR{idVendor}=="8086", ATTR{idProduct}=="0aa5", GROUP="plugdev", MODE="0666"
EOF
    else
        sudo tee "$RULES_FILE" > /dev/null << 'EOF'
# Caldera Backend - Sensor udev rules (Generic/Fedora)
# Allow access to depth sensors without sudo

# Kinect V2 (Xbox One)
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02c4", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02d8", MODE="0666"

# Kinect V1 (Xbox 360)
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02b0", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02ad", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02ae", MODE="0666"

# Intel RealSense (example)
# SUBSYSTEM=="usb", ATTR{idVendor}=="8086", ATTR{idProduct}=="0aa5", MODE="0666"
EOF
    fi
    
    echo -e "${GREEN}✓ Udev rules created${NC}"
    
    # Reload rules
    echo -e "${YELLOW}Reloading udev rules...${NC}"
    sudo udevadm control --reload-rules
    sudo udevadm trigger
    echo -e "${GREEN}✓ Rules reloaded${NC}"
    
    echo -e "${GREEN}✓ Setup complete${NC}"
    echo ""
    echo -e "${CYAN}Next steps:${NC}"
    echo "1. Unplug and reconnect your sensor"
    echo "2. Re-login if you were added to 'plugdev' (Ubuntu/Debian)"
    echo "3. Run: $0 check"
}

test_sensors() {
    echo -e "${CYAN}=== Testing Sensor Access ===${NC}"
    
    if ! list_sensors; then
        echo -e "${RED}No sensors to test${NC}"
        return 1
    fi
    
    echo ""
    echo -e "${YELLOW}Testing with Kinect devices...${NC}"
    
    local lsusb_output=$(lsusb)

    # Kinect v2 quick test via SensorViewer if built
    if echo "$lsusb_output" | grep -q "045e:02c4"; then
        if [ -f "build/SensorViewer" ]; then
            echo "Running SensorViewer for 2 seconds..."
            if timeout 3 ./build/SensorViewer -t 2 >/dev/null 2>&1; then
                echo -e "${GREEN}✓ Kinect V2 working - got data frames${NC}"
            else
                echo -e "${YELLOW}⚠ SensorViewer test failed - check permissions${NC}"
            fi
        else
            echo -e "${YELLOW}SensorViewer not built - run: ./build.sh SensorViewer${NC}"
        fi
    fi

    # Kinect v1 guidance and smoke test via CalderaTests
    if echo "$lsusb_output" | grep -q "045e:02b0"; then
        echo -e "${YELLOW}Kinect v1 motor detected. Ensure the external power supply is connected so that Camera (045e:02ae) and Audio (045e:02ad) also appear in lsusb.${NC}"
        echo -e "${YELLOW}If camera is claimed by kernel module gspca_kinect, libfreenect may not access it. Consider blacklisting with: echo 'blacklist gspca_kinect' | sudo tee /etc/modprobe.d/blacklist-kinect.conf && sudo depmod -a${NC}"
        if [ -f "build/tests/CalderaTests" ]; then
            echo "Running Kinect v1 smoke test (will skip if device/lib missing)..."
            ./build/tests/CalderaTests --gtest_filter=KinectV1_DeviceTest.OpenCloseSmoke || true
        else
            echo -e "${YELLOW}Test binary not built. Build tests with: ./build.sh CalderaTests${NC}"
        fi
        echo -e "${YELLOW}Install libfreenect if needed. Fedora: sudo dnf install libfreenect libfreenect-devel; Ubuntu/Debian: sudo apt install libfreenect-dev${NC}"
    fi
    
    echo -e "${GREEN}✓ Sensor test completed${NC}"
}

diagnose_kinect1() {
    echo -e "${CYAN}=== Kinect v1 Diagnostics ===${NC}"
    lsusb | grep -E '045e:02b0|045e:02ae|045e:02ad' || true
    if lsusb | grep -q '045e:02b0'; then echo -e "${GREEN}✓ Motor detected (045e:02b0)${NC}"; else echo -e "${YELLOW}⚠ Motor not detected${NC}"; fi
    if lsusb | grep -q '045e:02ae'; then echo -e "${GREEN}✓ Camera detected (045e:02ae)${NC}"; else echo -e "${YELLOW}⚠ Camera not detected — ensure external power is connected${NC}"; fi
    if lsusb | grep -q '045e:02ad'; then echo -e "${GREEN}✓ Audio detected (045e:02ad)${NC}"; else echo -e "${YELLOW}⚠ Audio not detected — ensure external power is connected${NC}"; fi

    echo -e "${CYAN}=== Kernel Module Claims (camera) ===${NC}"
    if lsmod | grep -q gspca_kinect; then
        echo -e "${YELLOW}⚠ gspca_kinect kernel module is loaded; it may block libfreenect access${NC}"
        echo -e "${YELLOW}  To blacklist: echo 'blacklist gspca_kinect' | sudo tee /etc/modprobe.d/blacklist-kinect.conf && sudo depmod -a && sudo modprobe -r gspca_kinect${NC}"
    else
        echo -e "${GREEN}✓ gspca_kinect not loaded${NC}"
    fi

    echo -e "${CYAN}=== LED hint ===${NC}"
    _led_cmd_exists && _kinect_v1_present && { echo "Blinking YELLOW for 3s..."; _led_blink_yellow_start; sleep 3; _led_blink_stop_and_set GREEN; } || echo "LED tool not available or camera not present"
}

# Main logic
case "${1:-check}" in
    setup)
        list_sensors || true
        setup_system
        ;;
    check)
        list_sensors && check_permissions
        ;;
    test)
        test_sensors
        ;;
    diagnose)
        diagnose_kinect1
        ;;
    list)
        list_sensors
        ;;
    -h|--help)
        show_usage
        ;;
    *)
        echo -e "${RED}Unknown command: $1${NC}"
        show_usage
        exit 1
        ;;
esac