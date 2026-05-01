#ifndef FOUNDATION_CONFIG_CONFIGREADER_H_
#define FOUNDATION_CONFIG_CONFIGREADER_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Export.h"
#include "foundation/base/Result.h"
#include "foundation/config/ConfigValue.h"

namespace foundation {
namespace config {

class FOUNDATION_API ConfigReader {
public:
    ConfigReader();

    foundation::base::ErrorCode LoadFromJsonString(
        const std::string& json,
        std::size_t max_depth = 64);
    foundation::base::ErrorCode LoadFromJsonFile(
        const std::string& path,
        std::size_t max_depth = 64);

    foundation::base::ErrorCode LoadFromIniString(const std::string& ini);
    foundation::base::ErrorCode LoadFromIniFile(const std::string& path);

    const ConfigValue& Root() const;

    bool Has(const std::string& dot_path) const;
    foundation::base::Result<ConfigValue> Get(
        const std::string& dot_path) const;
    foundation::base::Result<std::string> GetString(
        const std::string& dot_path) const;
    foundation::base::Result<std::int64_t> GetInt64(
        const std::string& dot_path) const;
    foundation::base::Result<double> GetDouble(
        const std::string& dot_path) const;
    foundation::base::Result<bool> GetBool(
        const std::string& dot_path) const;

    static foundation::base::Result<std::string> WriteJsonString(
        const ConfigValue& value);

private:
    ConfigValue root_;
};

}  // namespace config
}  // namespace foundation

#endif  // FOUNDATION_CONFIG_CONFIGREADER_H_