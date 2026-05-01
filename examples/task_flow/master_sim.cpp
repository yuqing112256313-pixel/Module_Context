#include "master_runner.h"

#include "common.h"

#include <iostream>
#include <map>
#include <string>

int main(int argc, char** argv) {
    using foundation::base::Result;
    using module_context::examples::task_flow::MasterRunConfig;
    using module_context::examples::task_flow::ParseArguments;
    using module_context::examples::task_flow::ParseMasterRunConfigFromArgs;
    using module_context::examples::task_flow::PatternFrameProvider;
    using module_context::examples::task_flow::RunTaskFlowMaster;

    const std::map<std::string, std::string> args = ParseArguments(argc, argv);
    Result<MasterRunConfig> config = ParseMasterRunConfigFromArgs(args);
    if (!config.IsOk()) {
        std::cerr << config.GetMessage() << std::endl;
        return 2;
    }

    PatternFrameProvider source_provider;
    return RunTaskFlowMaster(config.Value(), &source_provider, NULL, NULL);
}
