#pragma once

#include <string>

#if (defined(_WIN32) || defined(__CYGWIN__)) && defined(TGV_PLUGIN_API_EXPORTS)
#define TGV_PLUGIN_API __declspec(dllexport)
#elif (defined(_WIN32) || defined(__CYGWIN__)) && defined(TGV_PLUGIN_API_IMPORTS)
#define TGV_PLUGIN_API __declspec(dllimport)
#elif defined(__GNUC__)
#define TGV_PLUGIN_API __attribute__((visibility("default")))
#else
#define TGV_PLUGIN_API
#endif

namespace Hh {
namespace Api {
namespace Plugin {

class TGV_PLUGIN_API IPlugin {
public:
    virtual ~IPlugin() = default;
    virtual bool Init() = 0;
    virtual bool Start() = 0;
    virtual bool Stop() = 0;
    virtual bool Fini() = 0;
    virtual std::string Name() = 0;
};

}  // namespace Plugin
}  // namespace Api
}  // namespace Hh
