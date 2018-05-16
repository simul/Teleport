// Copyright 2018 Simul.co

#pragma once

#if PLATFORM_WINDOWS
# if PLATFORM_64BITS
#  define REMOTEPLAY_PLATFORM TEXT("Win64")
#  define REMOTEPLAY_LIBAVSTREAM TEXT("libavstream.dll")
# else
#  define REMOTEPLAY_PLATFORM TEXT("Win32")
#  define REMOTEPLAY_LIBAVSTREAM TEXT("libavstream.dll")
# endif
#else
# error "RemotePlay: Unsupported platform!"
#endif
