#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cstring>

#ifdef __has_include
#  if __has_include(<libfreenect/libfreenect.h>)
#    include <libfreenect/libfreenect.h>
#  elif __has_include(<libfreenect.h>)
#    include <libfreenect.h>
#  else
#    error "libfreenect headers not found"
#  endif
#else
#  include <libfreenect/libfreenect.h>
#endif

static void usage(const char* prog){
    std::cout << "KinectV1Ctl - control Kinect v1 via vendored libfreenect\n"
              << "Usage: " << prog << " [--info] [--led COLOR] [--blink COLOR --seconds N --final COLOR] [--tilt ANGLE]\n"
              << "  COLOR: off, green, red, yellow, blink-green, blink-red-yellow\n"
              << "  ANGLE: tilt degrees (-30..30)\n";
}

static freenect_led_options parse_led(const std::string& s, bool& ok){
    std::string v=s; for(char& c: v) c=tolower(c);
    ok=true;
    if (v=="off") return LED_OFF;
    if (v=="green") return LED_GREEN;
    if (v=="red") return LED_RED;
    if (v=="yellow") return LED_YELLOW;
    if (v=="blink-green") return LED_BLINK_GREEN;
    if (v=="blink-red-yellow") return LED_BLINK_RED_YELLOW;
    ok=false; return LED_OFF;
}

static bool open_device(freenect_context** ctx, freenect_device** dev, freenect_device_flags flags, const char* serial){
    if (freenect_init(ctx, nullptr) < 0) return false;
    freenect_set_log_level(*ctx, FREENECT_LOG_ERROR);
    freenect_select_subdevices(*ctx, flags);
    int rc = -1;
    if ((flags & FREENECT_DEVICE_CAMERA) && serial && std::strlen(serial)>0) {
        rc = freenect_open_device_by_camera_serial(*ctx, dev, serial);
        if (rc<0) { freenect_shutdown(*ctx); *ctx=nullptr; return false; }
        return true;
    }
    rc = freenect_open_device(*ctx, dev, 0);
    if (rc<0) { freenect_shutdown(*ctx); *ctx=nullptr; return false; }
    return true;
}

int main(int argc, char** argv){
    if (argc==1) { usage(argv[0]); return 1; }
    bool do_info=false, do_led=false, do_blink=false, do_tilt=false;
    std::string led_color, blink_color, final_color="green";
    int blink_seconds=2; double tilt_angle=0.0;

    for (int i=1;i<argc;i++){
        std::string a=argv[i];
        if (a=="--help"||a=="-h"){ usage(argv[0]); return 0; }
        else if (a=="--info") do_info=true;
        else if (a=="--led" && i+1<argc){ do_led=true; led_color=argv[++i]; }
        else if (a=="--blink" && i+1<argc){ do_blink=true; blink_color=argv[++i]; }
        else if (a=="--seconds" && i+1<argc){ blink_seconds=std::max(1, std::atoi(argv[++i])); }
        else if (a=="--final" && i+1<argc){ final_color=argv[++i]; }
        else if (a=="--tilt" && i+1<argc){ do_tilt=true; tilt_angle=std::stod(argv[++i]); }
        else { std::cerr << "Unknown or incomplete option: " << a << "\n"; usage(argv[0]); return 1; }
    }

    if (do_info){
        freenect_context* ctx=nullptr; freenect_device_attributes* list=nullptr;
        if (freenect_init(&ctx, nullptr) < 0){ std::cerr << "freenect_init failed\n"; return 2; }
        freenect_set_log_level(ctx, FREENECT_LOG_ERROR);
        int n = freenect_list_device_attributes(ctx, &list);
        std::cout << "Devices: " << n << "\n";
        for (auto* it=list; it; it=it->next){ if (it->camera_serial) std::cout << " camera_serial=" << it->camera_serial << "\n"; }
        if (list) freenect_free_device_attributes(list);
        freenect_shutdown(ctx);
        // continue to other ops if requested
    }

    if (do_led || do_blink){
        bool ok=false; freenect_led_options c = parse_led(do_blink?blink_color:led_color, ok);
        if (!ok){ std::cerr << "Invalid LED color\n"; return 3; }
        freenect_context* ctx=nullptr; freenect_device* dev=nullptr;
        const char* want_serial = std::getenv("CALDERA_KINECT1_SERIAL");
        if (!open_device(&ctx, &dev, FREENECT_DEVICE_CAMERA, want_serial)){
            std::cerr << "Open camera failed (is 045e:02ae present?)\n"; return 4;
        }
        int rc=0;
        if (do_blink){
            auto end = std::chrono::steady_clock::now() + std::chrono::seconds(blink_seconds);
            while (std::chrono::steady_clock::now() < end){
                rc = freenect_set_led(dev, c);
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                rc = freenect_set_led(dev, LED_OFF);
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            bool ok2=false; freenect_led_options final = parse_led(final_color, ok2);
            if (ok2) freenect_set_led(dev, final);
        } else {
            rc = freenect_set_led(dev, c);
        }
        freenect_close_device(dev); freenect_shutdown(ctx);
        if (rc<0) { std::cerr << "LED command failed\n"; return 5; }
    }

    if (do_tilt){
        freenect_context* ctx=nullptr; freenect_device* dev=nullptr;
        if (!open_device(&ctx, &dev, FREENECT_DEVICE_MOTOR, nullptr)){
            std::cerr << "Open motor failed (is 045e:02b0 present?)\n"; return 6;
        }
        int rc = freenect_set_tilt_degs(dev, tilt_angle);
        freenect_close_device(dev); freenect_shutdown(ctx);
        if (rc<0) { std::cerr << "Tilt command failed\n"; return 7; }
    }

    return 0;
}
