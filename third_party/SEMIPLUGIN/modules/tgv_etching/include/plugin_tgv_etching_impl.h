#ifndef PLUGIN_TGV_ETCHING_IMPL_H_
#define PLUGIN_TGV_ETCHING_IMPL_H_

#include "iplugin_tgv_etching.h"

#if (defined(_WIN32) || defined(__CYGWIN__)) && defined(TGV_ETCHING_IMPL_EXPORTS)
#define TGV_ETCHING_IMPL_API __declspec(dllexport)
#elif (defined(_WIN32) || defined(__CYGWIN__)) && defined(TGV_ETCHING_IMPL_IMPORTS)
#define TGV_ETCHING_IMPL_API __declspec(dllimport)
#elif defined(__GNUC__)
#define TGV_ETCHING_IMPL_API __attribute__((visibility("default")))
#else
#define TGV_ETCHING_IMPL_API
#endif

namespace Hh {
namespace Plugin {

class TGV_ETCHING_IMPL_API PluginTgvEtchingImpl
    : public Hh::Api::Plugin::IPluginTGVEtching {
public:
    PluginTgvEtchingImpl();
    ~PluginTgvEtchingImpl() override;

    bool Init() override;
    bool Start() override;
    bool Stop() override;
    bool Fini() override;
    std::string Name() override;

    double AlignDetect(std::string& imgPath) override;
    std::string MarkDetect(std::string& imgPath) override;
    std::string FaceInspection(std::string imgPath) override;
    std::string WaistInspection(std::string imgPath) override;
    double ReviewDetect(std::string imgPath) override;
    std::string InspectionDetect(std::string& img_Path) override;
    std::string LineOffsetCalibration(std::string imgPath) override;
    double ASCameraAngleCalculation(std::string imgPath) override;
    double LSCameraAngleCalculation(std::string imgPath) override;
    std::string StageCenterCalibration(std::string imgPath) override;
    std::string ASCameraAngleCalibration(std::string imgPath) override;
    std::string LSCameraAngleCalibration(std::string imgPath) override;
};

}  // namespace Plugin
}  // namespace Hh

extern "C" {
TGV_ETCHING_IMPL_API Hh::Api::Plugin::IPlugin* CreatePluginEtching();
TGV_ETCHING_IMPL_API void DestroyPluginEtching(Hh::Api::Plugin::IPlugin* plugin);
}

#endif  // PLUGIN_TGV_ETCHING_IMPL_H_