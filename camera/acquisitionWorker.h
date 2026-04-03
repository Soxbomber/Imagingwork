#pragma once

#include <QObject>
#include <peak/peak.hpp>
#include <peak_common/types/peak_common_pixel_format.hpp>
#include <peak_icv/pipeline/peak_icv_default_pipeline.hpp>
#include <QLabel>

class AcquisitionWorker : public QObject
{
    Q_OBJECT

public:
    AcquisitionWorker(std::shared_ptr<peak::core::DataStream> dataStream,
        peak::common::PixelFormat pixelFormat, QSize imageSize);
    ~AcquisitionWorker() override = default;

    void Stop();
//
public slots:
    void Start();
//
private:
    std::shared_ptr<peak::core::DataStream> m_dataStream;
    peak::common::PixelFormat m_outputPixelFormat;
    std::atomic<bool> m_running{ true };
    bool m_customNodesAvailable{ false };
    QSize m_size{};
    peak::pipeline::DefaultPipeline m_pipeline;

signals:
    void ImageReceived(QImage image);
};