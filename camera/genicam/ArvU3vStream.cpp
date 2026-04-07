#include "ArvU3vStream.h"
#include <QThread>
#include <QDebug>
#include <cstring>
#include <vector>
#include <algorithm>

ArvU3vStream::ArvU3vStream(ArvU3vDevice* device,
                             const ArvStreamParams& params,
                             QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_params(params)
{}

void ArvU3vStream::Stop()
{
    m_running = false;
}

// ============================================================
// Start() - Aravis sync stream thread 포팅
//
// Aravis arv_uv_stream_thread_sync():
//   1. leader 읽기 -> ArvUvspLeader magic 확인 + W/H/pfnc 추출
//   2. payload를 payloadCount 번 전송 크기(payloadSize)씩 읽기
//   3. transfer1Size > 0 이면 나머지 읽기
//   4. trailer 읽기 (discard)
//   5. 이미지 변환 emit
// ============================================================
void ArvU3vStream::Start()
{
    qDebug("ArvU3vStream::Start() "
           "reqPayload=%llu pSize=%u pCount=%u t1=%u "
           "leader=%u trailer=%u",
           (unsigned long long)m_params.reqPayloadSize,
           m_params.payloadSize, m_params.payloadCount,
           m_params.transfer1Size,
           m_params.leaderSize, m_params.trailerSize);

    if (m_params.leaderSize == 0 || m_params.payloadSize == 0) {
        emit errorOccurred("Invalid stream params (leader/payload size = 0)");
        return;
    }

    std::vector<uint8_t> leaderBuf(m_params.leaderSize);
    std::vector<uint8_t> trailerBuf(m_params.trailerSize > 0
                                    ? m_params.trailerSize : 256);
    std::vector<uint8_t> payloadBuf(m_params.reqPayloadSize > 0
                                    ? static_cast<size_t>(m_params.reqPayloadSize)
                                    : m_params.payloadSize);

    int frameCount = 0;

    while (m_running) {
        // ── 1. Leader ─────────────────────────────────────────────────────
        int xfer = 0;
        if (!m_device->bulkReadData(leaderBuf.data(),
                                     static_cast<int>(leaderBuf.size()),
                                     xfer, 5000)) {
            if (!m_running) break;
            qDebug("ArvU3vStream: leader read timeout/error");
            QThread::msleep(5);
            continue;
        }

        if (xfer < static_cast<int>(sizeof(ArvUvspLeader))) {
            qWarning("ArvU3vStream: leader too short (%d)", xfer);
            continue;
        }

        const auto* leader =
            reinterpret_cast<const ArvUvspLeader*>(leaderBuf.data());

        if (leader->header.magic != ARV_UVSP_LEADER_MAGIC) {
            qWarning("ArvU3vStream: bad leader magic 0x%08X",
                     leader->header.magic);
            continue;
        }

        if (leader->infos.payload_type != ARV_UVSP_PAYLOAD_TYPE_IMAGE) {
            qWarning("ArvU3vStream: non-image payload 0x%04X",
                     leader->infos.payload_type);
            // drain remaining transfers anyway
        }

        const int      W    = static_cast<int>(leader->infos.width);
        const int      H    = static_cast<int>(leader->infos.height);
        const uint32_t pfnc = leader->infos.pixel_format;

        if (W <= 0 || H <= 0) {
            qWarning("ArvU3vStream: leader invalid size %dx%d", W, H);
            continue;
        }

        if (frameCount == 0)
            qDebug("ArvU3vStream: first frame %dx%d pfnc=0x%08X "
                   "fid=%llu",
                   W, H, pfnc,
                   (unsigned long long)leader->header.frame_id);

        // ── 2. Payload chunks ─────────────────────────────────────────────
        // Aravis: reads payloadCount full chunks then transfer1
        size_t totalReceived = 0;
        const size_t expectedTotal =
            static_cast<size_t>(m_params.reqPayloadSize);

        if (payloadBuf.size() < expectedTotal)
            payloadBuf.resize(expectedTotal);

        bool frameOk = true;

        for (uint32_t p = 0; p < m_params.payloadCount && m_running; ++p) {
            const int chunkLen = static_cast<int>(m_params.payloadSize);
            int chunkXfer = 0;
            m_device->bulkReadData(
                payloadBuf.data() + totalReceived,
                chunkLen, chunkXfer, 5000);
            totalReceived += static_cast<size_t>(chunkXfer);
            if (chunkXfer < chunkLen) {
                // short packet: end of data
                break;
            }
        }

        // transfer1 (remainder, Aravis: si_transfer1_size)
        if (m_running && m_params.transfer1Size > 0
            && totalReceived < expectedTotal) {
            const int t1 = static_cast<int>(m_params.transfer1Size);
            int t1Xfer = 0;
            m_device->bulkReadData(
                payloadBuf.data() + totalReceived,
                t1, t1Xfer, 5000);
            totalReceived += static_cast<size_t>(t1Xfer);
        }

        if (!m_running) break;

        // ── 3. Trailer ────────────────────────────────────────────────────
        {
            int tXfer = 0;
            m_device->bulkReadData(trailerBuf.data(),
                                    static_cast<int>(trailerBuf.size()),
                                    tXfer, 2000);
            // Trailer magic check (informational only)
            if (tXfer >= static_cast<int>(sizeof(ArvUvspTrailer))) {
                const auto* tr =
                    reinterpret_cast<const ArvUvspTrailer*>(trailerBuf.data());
                if (tr->header.magic != ARV_UVSP_TRAILER_MAGIC)
                    qDebug("ArvU3vStream: unexpected trailer magic 0x%08X",
                           tr->header.magic);
            }
        }

        // ── 4. Convert and emit ───────────────────────────────────────────
        QImage img = convertPixels(payloadBuf.data(), W, H, pfnc);
        if (!img.isNull()) {
            ++frameCount;
            emit ImageReceived(std::move(img));
        } else {
            qWarning("ArvU3vStream: convertPixels returned null "
                     "%dx%d pfnc=0x%08X", W, H, pfnc);
        }
    }

    qDebug("ArvU3vStream: stopped (total frames=%d)", frameCount);
}

// ---- Pixel format conversion ------------------------------------

QImage ArvU3vStream::convertPixels(const uint8_t* src,
                                    int w, int h, uint32_t pfnc) const
{
    switch (pfnc) {
    case PFNC_Mono8: {
        QImage img(w, h, QImage::Format_Grayscale8);
        std::memcpy(img.bits(), src,
                    static_cast<size_t>(img.sizeInBytes()));
        return img;
    }
    case PFNC_Mono10:
    case PFNC_Mono12: {
        QImage img(w, h, QImage::Format_Grayscale8);
        const uint16_t* s16 = reinterpret_cast<const uint16_t*>(src);
        uint8_t* dst = img.bits();
        const int shift = (pfnc == PFNC_Mono10) ? 2 : 4;
        for (int i = 0; i < w * h; ++i)
            dst[i] = static_cast<uint8_t>(s16[i] >> shift);
        return img;
    }
    case PFNC_BayerRG8:
    case PFNC_BayerGR8:
    case PFNC_BayerGB8:
    case PFNC_BayerBG8:
        return debayerBilinear(src, w, h, pfnc);
    case PFNC_RGB8Packed: {
        QImage img(w, h, QImage::Format_RGB888);
        std::memcpy(img.bits(), src,
                    static_cast<size_t>(img.sizeInBytes()));
        return img;
    }
    case PFNC_BGR8Packed: {
        QImage img(w, h, QImage::Format_RGB888);
        std::memcpy(img.bits(), src,
                    static_cast<size_t>(img.sizeInBytes()));
        return img.rgbSwapped();
    }
    case PFNC_YUV422_8_UYVY:
    case PFNC_YUV422_8:
        return yuv422ToRgb(src, w, h);
    default:
        qWarning("ArvU3vStream: unknown pfnc 0x%08X -> Mono8 fallback", pfnc);
        QImage img(w, h, QImage::Format_Grayscale8);
        std::memcpy(img.bits(), src,
                    static_cast<size_t>(img.sizeInBytes()));
        return img;
    }
}

QImage ArvU3vStream::debayerBilinear(const uint8_t* src,
                                      int w, int h, uint32_t pfnc) const
{
    // Row 0 / Col 0 first pixel:
    //   BayerRG: R G R G ...   BayerGR: G R G R ...
    //            G B G B ...            B G B G ...
    //   BayerGB: G B G B ...   BayerBG: B G B G ...
    //            R G R G ...            G R G R ...
    const bool r0c0_isR  = (pfnc == PFNC_BayerRG8);
    const bool r0c0_isGR = (pfnc == PFNC_BayerGR8);
    const bool r0c0_isGB = (pfnc == PFNC_BayerGB8);
    // BayerBG8: r0c0 = B

    auto clampX = [&](int x) -> int { return std::max<int>(0, std::min<int>(x, w-1)); };
    auto clampY = [&](int y) -> int { return std::max<int>(0, std::min<int>(y, h-1)); };
    auto px     = [&](int x, int y) -> uint8_t {
        return src[clampY(y)*w + clampX(x)];
    };

    QImage dst(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = dst.scanLine(y);
        for (int x = 0; x < w; ++x) {
            uint8_t r, g, b;
            const bool er = (y % 2 == 0);
            const bool ec = (x % 2 == 0);

            // Determine which color is at (y,x) based on pattern
            bool atR, atG, atB;
            if (r0c0_isR) {
                atR = (er && ec); atB = (!er && !ec);
            } else if (r0c0_isGR) {
                atR = (er && !ec); atB = (!er && ec);
            } else if (r0c0_isGB) {
                atR = (!er && ec); atB = (er && !ec);
            } else { // BayerBG
                atR = (!er && !ec); atB = (er && ec);
            }
            atG = !atR && !atB;

            if (atR) {
                r = px(x,y);
                g = (px(x-1,y)+px(x+1,y)+px(x,y-1)+px(x,y+1)) / 4;
                b = (px(x-1,y-1)+px(x+1,y-1)+
                     px(x-1,y+1)+px(x+1,y+1)) / 4;
            } else if (atB) {
                b = px(x,y);
                g = (px(x-1,y)+px(x+1,y)+px(x,y-1)+px(x,y+1)) / 4;
                r = (px(x-1,y-1)+px(x+1,y-1)+
                     px(x-1,y+1)+px(x+1,y+1)) / 4;
            } else {
                // Green pixel: neighbor direction determines R/B
                g = px(x,y);
                if ((er && ec) || (!er && !ec)) {
                    // G in R row: R left/right, B up/down
                    r = (px(x-1,y) + px(x+1,y)) / 2;
                    b = (px(x,y-1) + px(x,y+1)) / 2;
                } else {
                    // G in B row: B left/right, R up/down
                    b = (px(x-1,y) + px(x+1,y)) / 2;
                    r = (px(x,y-1) + px(x,y+1)) / 2;
                }
            }
            row[x*3]=r; row[x*3+1]=g; row[x*3+2]=b;
        }
    }
    return dst;
}

QImage ArvU3vStream::yuv422ToRgb(const uint8_t* src,
                                   int w, int h) const
{
    QImage dst(w, h, QImage::Format_RGB888);
    auto clamp = [](int v) -> uint8_t {
        return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
    };
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src + y * w * 2;
        uint8_t* d = dst.scanLine(y);
        for (int x = 0; x < w; x += 2, s += 4, d += 6) {
            const int u  = s[0] - 128;
            const int y0 = s[1];
            const int v  = s[2] - 128;
            const int y1 = s[3];
            d[0]=clamp(y0+1402*v/1000);
            d[1]=clamp(y0- 344*u/1000 - 714*v/1000);
            d[2]=clamp(y0+1772*u/1000);
            d[3]=clamp(y1+1402*v/1000);
            d[4]=clamp(y1- 344*u/1000 - 714*v/1000);
            d[5]=clamp(y1+1772*u/1000);
        }
    }
    return dst;
}
