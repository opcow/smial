#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

std::string nameOf(uint16_t code);
uint16_t    kcParse(const std::string& s);  // throws std::runtime_error on unknown
const std::vector<std::pair<std::string, uint16_t>>& allKeycodes();
