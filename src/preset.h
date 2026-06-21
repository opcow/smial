#pragma once
#include <nlohmann/json.hpp>
#include "hid.h"

// Read full device state into a JSON preset (same schema as q1config.py).
nlohmann::json readConfig(HidDevice& d);

// Apply preset to device, writing only fields that differ. Returns change count.
int writeConfig(HidDevice& d, const nlohmann::json& p);
