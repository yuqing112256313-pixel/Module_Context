#pragma once

#include "iplugin.h"

#include <string>

namespace Hh {
namespace Api {
namespace Plugin {

class TGV_PLUGIN_API IPluginTGVEtching : public IPlugin {
public:
    virtual ~IPluginTGVEtching() {}

    virtual double AlignDetect(std::string& imgPath) = 0;
    virtual std::string MarkDetect(std::string& imgPath) = 0;
    virtual std::string FaceInspection(std::string imgPath) = 0;
    virtual std::string WaistInspection(std::string imgPath) = 0;
    virtual double ReviewDetect(std::string imgPath) = 0;
    virtual std::string InspectionDetect(std::string& img_Path) = 0;
    virtual std::string LineOffsetCalibration(std::string imgPath) = 0;
    virtual double ASCameraAngleCalculation(std::string imgPath) = 0;
    virtual double LSCameraAngleCalculation(std::string imgPath) = 0;
    virtual std::string StageCenterCalibration(std::string imgPath) = 0;
    virtual std::string ASCameraAngleCalibration(std::string imgPath) = 0;
    virtual std::string LSCameraAngleCalibration(std::string imgPath) = 0;
};

}  // namespace Plugin
}  // namespace Api
}  // namespace Hh