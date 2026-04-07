#pragma once
// ============================================================
// ArvU3vStream.h
// Aravis arvuvstream.c (sync mode) 포팅
//
// 프레임 조립 (Aravis ArvUvspLeader/Trailer 구조 사용):
//   1. bulkReadData(leaderSize)  -> ArvUvspLeader 파싱 -> W/H/pfnc
//   2. bulkReadData(payloadSize) x payloadCount
//   3. bulkReadData(transfer1Size) (나머지)
//   4. bulkReadData(trailerSize) -> discard
//   -> emit ImageReceived(QImage)
// ============================================================

#include <QObject>
#include <QImage>
#include <atomic>
#include "ArvU3vDevice.h"

class ArvU3vStream : public QObject
{
    Q_OBJECT
public:
    explicit ArvU3vStream(ArvU3vDevice*          device,
                          const ArvStreamParams& params,
                          QObject*               parent = nullptr);
    ~ArvU3vStream() override = default;

    void Stop();

public slots:
    void Start();

signals:
    void ImageReceived(QImage image);
    void errorOccurred(const QString& message);

private:
    QImage convertPixels(const uint8_t* src,
                         int w, int h, uint32_t pfnc) const;
    QImage debayerBilinear(const uint8_t* src,
                           int w, int h, uint32_t pfnc) const;
    QImage yuv422ToRgb   (const uint8_t* src, int w, int h) const;

    ArvU3vDevice*   m_device;
    ArvStreamParams m_params;
    std::atomic<bool> m_running{true};
};
