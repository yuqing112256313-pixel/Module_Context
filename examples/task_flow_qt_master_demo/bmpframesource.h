#pragma once

#include "../task_flow/master_runner.h"

#include <QtCore/QDir>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <functional>
#include <vector>

class BmpFrameSource
    : public module_context::examples::task_flow::ISourceFrameProvider {
public:
    typedef std::function<void(
        int,
        const module_context::examples::task_flow::MasterSourceFrame&)>
        FrameSelectedCallback;

    explicit BmpFrameSource(const QString& image_directory);

    void SetFrameSelectedCallback(const FrameSelectedCallback& callback);

    foundation::base::Result<void> Prepare(
        std::size_t fallback_image_size_bytes) override;
    foundation::base::Result<module_context::examples::task_flow::MasterSourceFrame>
    GetFrame(int source_index) override;
    std::string SourceBufferMode() const override;
    double SourcePrepareMs() const override;

    int FrameCount() const;

private:
    struct FrameEntry {
        module_context::examples::task_flow::MasterSourceFrame frame;
    };

    QString image_directory_;
    std::vector<FrameEntry> frames_;
    double source_prepare_ms_;
    FrameSelectedCallback frame_selected_callback_;
};
