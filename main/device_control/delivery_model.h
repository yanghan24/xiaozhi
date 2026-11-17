#pragma once
#include <string>

namespace device_control {
enum class DeliveryStatus { Invalid, Pending, Delivered };
DeliveryStatus ParseDeliveryCheck(const std::string& body, const std::string& device_id);
}  // namespace device_control
