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

RULES_FILE="/etc/udev/rules.d/90-caldera-sensors.rules"

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
    echo "  list     - List connected sensors"
    echo ""
    echo "Examples:"
    echo "  $0 setup    # Setup system"
    echo "  $0 check    # Quick health check"
    echo "  $0 test     # Test with actual device access"
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
    
    # Check if rules file exists
    if [ ! -f "$RULES_FILE" ]; then
        echo -e "${YELLOW}⚠ Udev rules not found${NC}"
        all_ok=false
    else
        echo -e "${GREEN}✓ Udev rules installed${NC}"
    fi
    
    # Check plugdev group
    if groups | grep -q plugdev; then
        echo -e "${GREEN}✓ User in plugdev group${NC}"
    else
        echo -e "${YELLOW}⚠ User not in plugdev group${NC}"
        all_ok=false
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
        return 0
    else
        echo -e "${YELLOW}Run: $0 setup${NC}"
        return 1
    fi
}

setup_system() {
    echo -e "${CYAN}=== System Setup ===${NC}"
    
    # Add user to plugdev group
    if ! groups | grep -q plugdev; then
        echo -e "${YELLOW}Adding user to plugdev group...${NC}"
        sudo usermod -a -G plugdev "$USER"
        echo -e "${GREEN}✓ Added to plugdev group${NC}"
    fi
    
    # Create udev rules
    echo -e "${YELLOW}Creating udev rules...${NC}"
    sudo tee "$RULES_FILE" > /dev/null << 'EOF'
# Caldera Backend - Sensor udev rules
# Allow access to depth sensors without sudo

# Kinect V2 (Xbox One) - Currently supported
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02c4", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02d8", MODE="0666"

# Kinect V1 (Xbox 360) - Prepared for future support
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02b0", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02ad", MODE="0666" 
SUBSYSTEM=="usb", ATTR{idVendor}=="045e", ATTR{idProduct}=="02ae", MODE="0666"

# Intel RealSense
# SUBSYSTEM=="usb", ATTR{idVendor}=="8086", ATTR{idProduct}=="0aa5", MODE="0666"
EOF
    
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
    echo "2. Run: $0 check"
}

test_sensors() {
    echo -e "${CYAN}=== Testing Sensor Access ===${NC}"
    
    if ! list_sensors; then
        echo -e "${RED}No sensors to test${NC}"
        return 1
    fi
    
    echo ""
    echo -e "${YELLOW}Testing with KinectViewer...${NC}"
    
    # Test with actual Kinect viewer if available
    if [ -f "build/KinectViewer" ]; then
        echo "Running KinectViewer for 2 seconds..."
        if timeout 3 ./build/KinectViewer -t 2 >/dev/null 2>&1; then
            echo -e "${GREEN}✓ Kinect V2 working - got data frames${NC}"
        else
            echo -e "${YELLOW}⚠ KinectViewer test failed - check permissions${NC}"
            return 1
        fi
    else
        echo -e "${YELLOW}KinectViewer not built - run: ./build.sh KinectViewer${NC}"
        
        # Fallback: basic device access test
        echo -e "${YELLOW}Testing basic device access...${NC}"
        local lsusb_output=$(lsusb)
        
        for sensor_id in "${!SENSORS[@]}"; do
            if echo "$lsusb_output" | grep -q "$sensor_id"; then
                local bus_dev=$(echo "$lsusb_output" | grep "$sensor_id" | sed 's/.*Bus \([0-9]*\) Device \([0-9]*\).*/\1\/\2/')
                local device_path="/dev/bus/usb/$bus_dev"
                
                echo -n "Testing ${SENSORS[$sensor_id]}... "
                if [ -r "$device_path" ] && [ -w "$device_path" ]; then
                    echo -e "${GREEN}OK${NC}"
                else
                    echo -e "${RED}NO ACCESS${NC}"
                    return 1
                fi
            fi
        done
    fi
    
    echo -e "${GREEN}✓ Sensor test completed${NC}"
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