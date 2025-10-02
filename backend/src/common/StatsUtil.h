#pragma once
#include <string>
#include <sstream>
#include <iomanip>

namespace caldera::backend::common {
inline std::string prettyFps(double v) {
    std::ostringstream o;
    if (v >= 1e6)      o << std::fixed << std::setprecision(2) << (v/1e6) << "M";
    else if (v >= 1e3) o << std::fixed << std::setprecision(2) << (v/1e3) << "k";
    else               o << std::fixed << std::setprecision(2) << v;
    return o.str();
}
}
