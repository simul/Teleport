// libavstream
// (c) Copyright 2018-2024 Teleport XR Ltd

#pragma once

#if defined(_MSC_VER)
	#include <libavstream/platforms/platform_windows.hpp>
#else
	#include <libavstream/platforms/platform_posix.hpp>
#endif
