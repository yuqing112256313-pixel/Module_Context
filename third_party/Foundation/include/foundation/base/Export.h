#ifndef FOUNDATION_BASE_EXPORT_H_
#define FOUNDATION_BASE_EXPORT_H_

#include "foundation/base/Platform.h"

#if defined(FOUNDATION_SHARED_LIBRARY)

    #if FOUNDATION_PLATFORM_WINDOWS

        #if defined(FOUNDATION_BUILDING_LIBRARY)
            #define FOUNDATION_API __declspec(dllexport)
        #else
            #define FOUNDATION_API __declspec(dllimport)
        #endif

        #define FOUNDATION_LOCAL

    #else

        #if defined(FOUNDATION_BUILDING_LIBRARY)
            #define FOUNDATION_API __attribute__((visibility("default")))
        #else
            #define FOUNDATION_API
        #endif

        #define FOUNDATION_LOCAL __attribute__((visibility("hidden")))

    #endif

#else

    #define FOUNDATION_API
    #define FOUNDATION_LOCAL

#endif

#endif  // FOUNDATION_BASE_EXPORT_H_
