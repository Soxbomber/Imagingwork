#include "acquisitionworker.h"
#include <QImage>
#include <QThread>

AcquisitionWorker::AcquisitionWorker(std::shared_ptr<peak::core::DataStream> dataStream,
    peak::common::PixelFormat pixelFormat, QSize imageSize)
    : QObject(nullptr), m_dataStream(std::move(dataStream)), m_outputPixelFormat(pixelFormat), m_size(imageSize)
{
    auto isNodeReadable = [this](std::string const& nodeName) {
        if (!m_dataStream->NodeMaps().at(0)->HasNode(nodeName))
        {
            return false;
        }

        return (m_dataStream->NodeMaps().at(0)->FindNode(nodeName)->AccessStatus()
            == peak::core::nodes::NodeAccessStatus::ReadOnly)
            || (m_dataStream->NodeMaps().at(0)->FindNode(nodeName)->AccessStatus()
                == peak::core::nodes::NodeAccessStatus::ReadWrite);
        };

    m_customNodesAvailable = isNodeReadable("StreamIncompleteFrameCount")
        && isNodeReadable("StreamDroppedFrameCount") && isNodeReadable("StreamLostFrameCount");

    //qRegisterMetaType<FrameStatistics>();
}

void AcquisitionWorker::Start()
{
    QImage::Format qImageFormat;

    switch (m_outputPixelFormat)
    {
    case peak::common::PixelFormat::BGRa8:
    {
        qImageFormat = QImage::Format_RGB32;
        break;
    }
    case peak::common::PixelFormat::RGB8:
    {
        qImageFormat = QImage::Format_RGB888;
        break;
    }
    case peak::common::PixelFormat::RGB10p32: {
        qImageFormat = QImage::Format_BGR30;
        break;
    }
    case peak::common::PixelFormat::BayerRG8:
    case peak::common::PixelFormat::Mono8:
    {
        qImageFormat = QImage::Format_Grayscale8;
        break;
    }
    default:
    {
        qImageFormat = QImage::Format_RGB32;
    }
    }

    // Pre-allocate images for conversion that can be used simultaneously
    // This is not mandatory, but it can increase the speed of image conversions
    auto nodemapRemoteDevice = m_dataStream->ParentDevice()->RemoteDevice()->NodeMaps().at(0);

    m_pipeline.SetOutputPixelFormat(m_outputPixelFormat);

    //QElapsedTimer chronometerConversion;
    //QElapsedTimer chronometerFrameTime;
    //chronometerFrameTime.start();

    while (m_running)
    {
        try
        {
            // Wait 5 seconds for an image from the camera
            auto buffer = m_dataStream->WaitForFinishedBuffer(5000);

            //chronometerConversion.restart();

            QImage qImage(m_size, qImageFormat);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
            auto qImageSize = static_cast<size_t>(qImage.sizeInBytes());
#else
            auto qImageSize = static_cast<size_t>(qImage.byteCount());
#endif

            // Use the DefaultPipeline to automatically debayer and convert the
            // image to the requested output pixel format from an image view.

            auto image = m_pipeline.Process(buffer->ToImageView());
            // While the pipeline.Process() function guarantees making a copy,
            // we still copy over the image data into the QImage so we
            // we can release `image` while only keeping the `qImage`.
            std::memcpy(qImage.bits(), image.GetData(), qImageSize);

            //m_statistics.conversionTime_ms = chronometerConversion.elapsed();

            // Requeue buffer
            m_dataStream->QueueBuffer(buffer);
            ImageReceived(qImage);
            // Send signal to update the display
            //ImageReceived(qImage);

            //m_statistics.frameTime_ms = chronometerFrameTime.restart();

            //m_statistics.frameCounter++;

            //if (m_customNodesAvailable)
            //{
            //    // Missing packets on the interface, event after 1 resend
            //    m_statistics.incomplete = static_cast<int>(
            //        m_dataStream->NodeMaps()
            //        .at(0)
            //        ->FindNode<peak::core::nodes::IntegerNode>("StreamIncompleteFrameCount")
            //        ->Value());

            //    // Camera buffer overrun (sensor data too fast for interface)
            //    m_statistics.dropped = static_cast<int>(
            //        m_dataStream->NodeMaps()
            //        .at(0)
            //        ->FindNode<peak::core::nodes::IntegerNode>("StreamDroppedFrameCount")
            //        ->Value());

            //    // User buffer overrun (application too slow to process the camera data)
            //    m_statistics.lost = static_cast<int>(m_dataStream->NodeMaps()
            //        .at(0)
            //        ->FindNode<peak::core::nodes::IntegerNode>("StreamLostFrameCount")
            //        ->Value());
            //}
        }
        catch (const std::exception&)
        {
            // Without a sleep the GUI will be blocked completely and the CPU load will rise!
            QThread::msleep(1000);

            //m_statistics.errorCounter++;
        }

        //UpdateCounters(m_statistics);
    }
}

void AcquisitionWorker::Stop()
{
    m_running = false;
}