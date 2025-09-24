#ifndef CALDERA_BACKEND_HAL_ISENSORDEVICE_H
#define CALDERA_BACKEND_HAL_ISENSORDEVICE_H

#include <functional>
#include <string>
#include <vector>
#include "common/DataTypes.h"

namespace caldera::backend::hal {

using caldera::backend::common::RawDepthFrame;
using caldera::backend::common::RawColorFrame;

using RawFrameCallback = std::function<void(const RawDepthFrame&, const RawColorFrame&)>;

class ISensorDevice {
public:
	virtual ~ISensorDevice() = default;

	virtual bool open() = 0;
	virtual void close() = 0;
	virtual bool isRunning() const = 0;
	virtual std::string getDeviceID() const = 0;

	virtual void setFrameCallback(RawFrameCallback callback) = 0;
};

} // namespace caldera::backend::hal

#endif // CALDERA_BACKEND_HAL_ISENSORDEVICE_H
