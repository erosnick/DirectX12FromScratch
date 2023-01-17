#pragma once

#include <stdexcept>

#define DXThrow(message) throw std::runtime_error(message);

#define DXCheck(result, message) if (FAILED(result)) { DXThrow(message); }