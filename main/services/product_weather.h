#pragma once

#include "environment_snapshot.h"

namespace product_weather {
// Parse the authenticated device environment envelope into a replacement snapshot.
bool Parse(const std::string& body, EnvironmentSnapshot& snapshot, std::string& candidates);
std::string CityQuery(const std::string& city, const std::string& province);
// Retain the last dated component when that component failed for the same city.
void RetainUnavailable(std::string& response, const std::string& previous);
void MarkCached(EnvironmentSnapshot& snapshot);
}
