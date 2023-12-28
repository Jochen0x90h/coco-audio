#pragma once

#ifdef _WIN32
#include "Audio_Win32.hpp"
namespace coco {
using Audio_native = Audio_Win32;
}
#endif
