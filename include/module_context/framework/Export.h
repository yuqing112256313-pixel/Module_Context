#pragma once

#include "foundation/base/Platform.h"

/**
 * @brief `mc_core_framework` 运行时共享库的符号导出宏。
 *
 * 公开 API 头文件应使用 `MC_FRAMEWORK_API` 标记需要跨动态库边界暴露的类型与函数。
 * 插件工厂导出函数使用 `MC_PLUGIN_EXPORT`。
 */

#if defined(MC_FRAMEWORK_SHARED_LIBRARY)
    #if FOUNDATION_PLATFORM_WINDOWS
        #if defined(MC_FRAMEWORK_BUILDING_LIBRARY)
            #define MC_FRAMEWORK_API __declspec(dllexport)
        #else
            #define MC_FRAMEWORK_API __declspec(dllimport)
        #endif
    #else
        #if defined(MC_FRAMEWORK_BUILDING_LIBRARY)
            #define MC_FRAMEWORK_API __attribute__((visibility("default")))
        #else
            #define MC_FRAMEWORK_API
        #endif
    #endif
#else
    #define MC_FRAMEWORK_API
#endif

#if FOUNDATION_PLATFORM_WINDOWS
    #define MC_PLUGIN_EXPORT __declspec(dllexport)
#else
    #define MC_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
