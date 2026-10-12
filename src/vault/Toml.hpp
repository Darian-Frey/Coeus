#pragma once

// toml++ header-only with exceptions off (D-016). Every file using toml++
// includes it through here, so all agree on the configuration; the packaged
// shared library is not linked.
#define TOML_HEADER_ONLY 1
#define TOML_EXCEPTIONS 0
#include <toml++/toml.hpp>
