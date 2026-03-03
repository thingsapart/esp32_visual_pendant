// Minimal stub for chip/esp_expander_base.hpp
// Redirect to our project-level esp_io_expander.hpp which defines the
// necessary `esp_expander::Base` class and related types used by the
// Display_Panel drivers. When the stub is copied into the panel library
// tree the relative include resolves to the copied `esp_io_expander.hpp`.
#pragma once

#include "../esp_io_expander.hpp"
