#pragma once
#ifdef DOXYGEN
#define TELEPORT_EXPORT
#else
#if PLATFORM_WINDOWS
#define TELEPORT_EXPORT extern "C" __declspec(dllexport)
#else
#define TELEPORT_EXPORT extern "C" __attribute__((visibility("default")))
#endif
#endif

#if defined(_MSC_VER)
    //  Microsoft
    #define TELEPORT_EXPORT_DEC __declspec(dllexport)
    #define TELEPORT_IMPORT_DEC __declspec(dllimport)
#elif defined(__GNUC__)
    //  GCC or Clang
#define TELEPORT_EXPORT_DEC __attribute__((visibility("default")))
#define TELEPORT_IMPORT_DEC
#else
    //  do nothing and hope for the best?
#define TELEPORT_EXPORT_DEC
#define TELEPORT_IMPORT_DEC
    #pragma warning Unknown dynamic link import/export semantics.
#endif

#if defined(TELEPORT_EXPORT_SERVER_DLL) && !defined(DOXYGEN)
	// In this lib:
	// If we're building dll libraries but not in this library IMPORT the classes
	#if !defined(TELEPORT_SERVER_DLL) 
		#define TELEPORT_SERVER_API TELEPORT_IMPORT_DEC
	#else
	// In ALL OTHER CASES we EXPORT the classes!
#define TELEPORT_SERVER_API TELEPORT_EXPORT_DEC
	#endif
#else
#define TELEPORT_SERVER_API
#endif

#pragma warning(disable : 4251)
#pragma warning(disable :  4275)