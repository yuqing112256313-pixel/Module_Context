#include "bmpframesource.h"

#include "foundation/base/ErrorCode.h"

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

using foundation::base::ErrorCode;
using foundation::base::Result;
using module_context::examples::task_flow::MasterSourceFrame;

namespace {

std::string ToUtf8String(const QString& value) {
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

}  // namespace

BmpFrameSource::BmpFrameSource(const QString& image_directory)
    : image_directory_(image_directory),
      frames_(),
      source_prepare_ms_(0.0),
      frame_selected_callback_() {
}

void BmpFrameSource::SetFrameSelectedCallback(
    const FrameSelectedCallback& callback) {
    frame_selected_callback_ = callback;
}

Result<void> BmpFrameSource::Prepare(std::size_t /*fallback_image_size_bytes*/) {
    QElapsedTimer timer;
    timer.start();
    frames_.clear();

    QDir dir(image_directory_);
    if (!dir.exists()) {
        return Result<void>(
            ErrorCode::kNotFound,
            "BMP image directory does not exist");
    }

    const QStringList files = dir.entryList(
        QStringList() << "*.bmp" << "*.BMP",
        QDir::Files,
        QDir::Name);
    if (files.isEmpty()) {
        return Result<void>(
            ErrorCode::kNotFound,
            "BMP image directory does not contain any .bmp file");
    }

    for (int index = 0; index < files.size(); ++index) {
        const QString path = dir.absoluteFilePath(files.at(index));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return Result<void>(
                ErrorCode::kIoError,
                ToUtf8String(QString("failed to open BMP: %1").arg(path)));
        }

        const QByteArray bytes = file.readAll();
        if (bytes.isEmpty()) {
            return Result<void>(
                ErrorCode::kInvalidArgument,
                ToUtf8String(QString("BMP is empty: %1").arg(path)));
        }

        FrameEntry entry;
        entry.frame.data.reset(new std::vector<char>(
            bytes.constData(),
            bytes.constData() + bytes.size()));
        entry.frame.source_path = ToUtf8String(path);
        entry.frame.display_name = ToUtf8String(QFileInfo(path).fileName());
        frames_.push_back(entry);
    }

    source_prepare_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    return foundation::base::MakeSuccess();
}

Result<MasterSourceFrame> BmpFrameSource::GetFrame(int source_index) {
    if (frames_.empty()) {
        return Result<MasterSourceFrame>(
            ErrorCode::kInvalidState,
            "BMP frames were not prepared");
    }

    const std::size_t index =
        static_cast<std::size_t>(
            source_index < 0 ? 0 : source_index) %
        frames_.size();
    MasterSourceFrame frame = frames_[index].frame;
    if (frame_selected_callback_) {
        frame_selected_callback_(source_index, frame);
    }
    return Result<MasterSourceFrame>(frame);
}

std::string BmpFrameSource::SourceBufferMode() const {
    return "reused_bmp_directory";
}

double BmpFrameSource::SourcePrepareMs() const {
    return source_prepare_ms_;
}

int BmpFrameSource::FrameCount() const {
    return static_cast<int>(frames_.size());
}
