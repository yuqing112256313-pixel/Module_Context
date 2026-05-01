#ifndef FOUNDATION_PLUGIN_DYNAMICLIBRARY_H_
#define FOUNDATION_PLUGIN_DYNAMICLIBRARY_H_

#include <string>

#include "foundation/base/Export.h"
#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"

namespace foundation {
namespace plugin {

class FOUNDATION_API DynamicLibrary
    : private foundation::base::NonCopyable {
public:
    DynamicLibrary();
    ~DynamicLibrary();

    DynamicLibrary(DynamicLibrary&& other);
    DynamicLibrary& operator=(DynamicLibrary&& other);

public:
    foundation::base::Result<void> Open(const std::string& path);
    foundation::base::Result<void> Close();

    bool IsOpen() const;
    const std::string& Path() const;

    foundation::base::Result<void*> GetSymbolRaw(
        const std::string& name) const;

    template <typename T>
    foundation::base::Result<T> GetSymbol(const std::string& name) const {
        foundation::base::Result<void*> symbol = GetSymbolRaw(name);
        if (!symbol.IsOk()) {
            return foundation::base::Result<T>(
                symbol.GetError(),
                symbol.GetMessage());
        }
        return foundation::base::Result<T>(
            reinterpret_cast<T>(symbol.Value()));
    }

private:
    void Reset();

private:
    std::string path_;
    void* handle_;
};

}  // namespace plugin
}  // namespace foundation

#endif  // FOUNDATION_PLUGIN_DYNAMICLIBRARY_H_
