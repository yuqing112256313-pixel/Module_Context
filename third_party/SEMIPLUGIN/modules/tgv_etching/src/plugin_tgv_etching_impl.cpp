#include "plugin_tgv_etching_impl.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

int ReadTaskFlowDelayMs() {
    const char* value = std::getenv("TASK_FLOW_ALGORITHM_DELAY_MS");
    if (value == NULL || *value == '\0') {
        return 0;
    }

    char* end = NULL;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || (end != NULL && *end != '\0') || parsed <= 0) {
        return 0;
    }
    return static_cast<int>(parsed);
}

void SimulateTaskFlowAlgorithmDelay() {
    const int delay_ms = ReadTaskFlowDelayMs();
    if (delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
}

}  // namespace

namespace Hh {
namespace Plugin {

PluginTgvEtchingImpl::PluginTgvEtchingImpl() {
    std::cout << "[PluginTgvEtchingImpl] Constructor" << std::endl;
}

PluginTgvEtchingImpl::~PluginTgvEtchingImpl() {
    std::cout << "[PluginTgvEtchingImpl] Destructor" << std::endl;
}

bool PluginTgvEtchingImpl::Init() {
    std::cout << "[PluginTgvEtchingImpl] Init" << std::endl;
    return true;
}

bool PluginTgvEtchingImpl::Start() {
    std::cout << "[PluginTgvEtchingImpl] Start" << std::endl;
    return true;
}

bool PluginTgvEtchingImpl::Stop() {
    std::cout << "[PluginTgvEtchingImpl] Stop" << std::endl;
    return true;
}

bool PluginTgvEtchingImpl::Fini() {
    std::cout << "[PluginTgvEtchingImpl] Fini" << std::endl;
    return true;
}

std::string PluginTgvEtchingImpl::Name() {
    return "tgv_etching";
}

double PluginTgvEtchingImpl::AlignDetect(std::string& imgPath) {
    (void)imgPath;
    SimulateTaskFlowAlgorithmDelay();
    return 0.0;
}

std::string PluginTgvEtchingImpl::MarkDetect(std::string& imgPath) {
    (void)imgPath;
    return std::string();
}

std::string PluginTgvEtchingImpl::FaceInspection(std::string imgPath) {
    (void)imgPath;
    return std::string();
}

std::string PluginTgvEtchingImpl::WaistInspection(std::string imgPath) {
    (void)imgPath;
    return std::string();
}

double PluginTgvEtchingImpl::ReviewDetect(std::string imgPath) {
    (void)imgPath;
    return 0.0;
}

std::string PluginTgvEtchingImpl::InspectionDetect(std::string& img_Path) {
    (void)img_Path;
    return std::string();
}

std::string PluginTgvEtchingImpl::LineOffsetCalibration(std::string imgPath) {
    (void)imgPath;
    return std::string();
}

double PluginTgvEtchingImpl::ASCameraAngleCalculation(std::string imgPath) {
    (void)imgPath;
    return 0.0;
}

double PluginTgvEtchingImpl::LSCameraAngleCalculation(std::string imgPath) {
    (void)imgPath;
    return 0.0;
}

std::string PluginTgvEtchingImpl::StageCenterCalibration(std::string imgPath) {
    (void)imgPath;
    return std::string();
}

std::string PluginTgvEtchingImpl::ASCameraAngleCalibration(std::string imgPath) {
    (void)imgPath;
    return std::string();
}

std::string PluginTgvEtchingImpl::LSCameraAngleCalibration(std::string imgPath) {
    (void)imgPath;
    return std::string();
}

}  // namespace Plugin
}  // namespace Hh

extern "C" Hh::Api::Plugin::IPlugin* CreatePluginEtching() {
    try {
        return new Hh::Plugin::PluginTgvEtchingImpl();
    } catch (...) {
        return NULL;
    }
}

extern "C" void DestroyPluginEtching(Hh::Api::Plugin::IPlugin* plugin) {
    try {
        delete plugin;
    } catch (...) {
    }
}
