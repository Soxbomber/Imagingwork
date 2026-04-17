#include "ArvU3vStream.h"
#include "DebayerAVX2.h"
#include <QDebug>
#include <cstring>
#include <algorithm>

// ============================================================
// 생성자: N개 버퍼 pre-allocate (Aravis create_buffers 포팅)
// ============================================================
ArvU3vStream::ArvU3vStream(ArvU3vDevice*          device,
                            const ArvStreamParams& params,
                            QObject*               parent)
    : QObject(parent)
    , m_device(device)
    , m_params(params)
{
    const size_t bufSize = params.reqPayloadSize > 0
                           ? static_cast<size_t>(params.reqPayloadSize)
                           : static_cast<size_t>(params.payloadSize);

    // N개 버퍼 사전 할당 (Aravis: DSAllocAndAnnounceBuffer × N)
    m_freePool  = std::make_unique<FramePool>(N_BUFFERS, bufSize);
    m_fullQueue = std::make_unique<FrameQueue>();
    m_fullQueue->setPool(m_freePool.get());

    qDebug("ArvU3vStream: %d buffers pre-allocated, each %zu bytes",
           N_BUFFERS, bufSize);
}

ArvU3vStream::~ArvU3vStream()
{
    Stop();
    // std::thread join은 Start() 내부에서 처리
}

void ArvU3vStream::Stop()
{
    m_running = false;
    m_fullQueue->stop();   // 변환 스레드의 popWait 해제
}

// ============================================================
// Start() — USB 수신 스레드 (QThread::started 에 연결)
//
// Aravis _loop() 포팅:
//   1. FreePool에서 빈 버퍼 획득
//   2. bulkRead(leader + payload + trailer) → raw 데이터 채움
//   3. FullQueue push → 변환 스레드가 처리
//   4. 즉시 다음 수신 (변환 대기 없음)
// ============================================================
void ArvU3vStream::Start()
{
    qDebug("ArvU3vStream::Start() "
           "reqPayload=%llu pSize=%u pCount=%u t1=%u leader=%u trailer=%u",
           (unsigned long long)m_params.reqPayloadSize,
           m_params.payloadSize, m_params.payloadCount,
           m_params.transfer1Size, m_params.leaderSize, m_params.trailerSize);

    if (m_params.leaderSize == 0 || m_params.payloadSize == 0) {
        emit errorOccurred("Invalid stream params");
        return;
    }

    // convertLoop를 std::thread로 실행
    // Qt signal emission은 thread-safe이므로 OK
    // (emit ImageReceived → QueuedConnection → UI 스레드)
    std::thread convertThr([this]{ convertLoop(); });

    // ── 수신 루프 (Aravis _loop의 수신 파트) ─────────────────────────────
    std::vector<uint8_t> leaderBuf(m_params.leaderSize);
    std::vector<uint8_t> trailerBuf(
        m_params.trailerSize > 0 ? m_params.trailerSize : 256);

    int frameCount = 0, dropCount = 0;

    while (m_running) {
        // ── 1. Leader 수신 ───────────────────────────────────────────────
        int xfer = 0;
        if (!m_device->bulkReadData(leaderBuf.data(),
                                     static_cast<int>(leaderBuf.size()),
                                     xfer, 5000)) {
            if (!m_running) break;
            QThread::msleep(2);
            continue;
        }
        if (xfer < static_cast<int>(sizeof(ArvUvspLeader))) continue;

        const auto* leader =
            reinterpret_cast<const ArvUvspLeader*>(leaderBuf.data());
        if (leader->header.magic != ARV_UVSP_LEADER_MAGIC)  continue;
        if (leader->infos.payload_type != ARV_UVSP_PAYLOAD_TYPE_IMAGE) continue;

        const int      W    = static_cast<int>(leader->infos.width);
        const int      H    = static_cast<int>(leader->infos.height);
        const uint32_t pfnc = leader->infos.pixel_format;
        const uint64_t fid  = leader->header.frame_id;
        if (W <= 0 || H <= 0) continue;

        // ── 2. FreePool에서 버퍼 획득 ────────────────────────────────────
        // (Aravis: arv_stream_pop_input_buffer)
        auto fb = m_freePool->tryPop();
        if (!fb) {
            // 모든 버퍼가 변환 중 → 이 프레임 드롭 (payload 읽어서 버림)
            ++dropCount;
            const size_t sz =
                static_cast<size_t>(m_params.reqPayloadSize);
            std::vector<uint8_t> dummy(sz);
            // drop: payload를 읽어서 버림 (USB 버퍼 클리어)
            size_t rx = 0;
            const unsigned int dropTimeout =
                (m_params.payloadSize / 1000u > 2000u)
                ? static_cast<unsigned int>(m_params.payloadSize / 1000u)
                : 2000u;
            for (uint32_t p = 0;
                 p < m_params.payloadCount && m_running; ++p) {
                int cx = 0;
                m_device->bulkReadData(
                    dummy.data() + rx,
                    static_cast<int>(m_params.payloadSize), cx, dropTimeout);
                rx += static_cast<size_t>(cx);
                if (cx < static_cast<int>(m_params.payloadSize)) break;
            }
            if (m_params.transfer1Size > 0 && rx < sz) {
                int cx = 0;
                m_device->bulkReadData(
                    dummy.data() + rx,
                    static_cast<int>(m_params.transfer1Size), cx, dropTimeout);
            }
            int t = 0;
            m_device->bulkReadData(trailerBuf.data(),
                static_cast<int>(trailerBuf.size()), t, 2000);
            continue;
        }

        // ── 3. Payload 수신 → fb->data 에 직접 기록 ──────────────────────
        const size_t expected =
            static_cast<size_t>(m_params.reqPayloadSize);

        // 버퍼 크기 보장
        if (fb->data.size() < expected) fb->data.resize(expected);

        // ── Payload 수신 ────────────────────────────────────────────────
        // payloadCount=1(최적화), 또는 분할 수신도 지원
        // 타임아웃: USB3 @ 5Gbps, 5MP=4.8MB → 이론 8ms
        //          여유 포함 2000ms (단, 실제 WinUSB 오버헤드는 1회 호출당 ~15ms)
        size_t received = 0;
        const unsigned int payloadTimeoutMs =
            (m_params.payloadSize / 1000u > 2000u)
            ? static_cast<unsigned int>(m_params.payloadSize / 1000u)
            : 2000u;

        for (uint32_t p = 0;
             p < m_params.payloadCount && m_running; ++p) {
            const int chunk = static_cast<int>(m_params.payloadSize);
            int cx = 0;
            m_device->bulkReadData(fb->data.data() + received,
                                    chunk, cx, payloadTimeoutMs);
            received += static_cast<size_t>(cx);
            if (cx < chunk) break;
        }

        if (!m_running) {
            m_freePool->push(std::move(fb));
            break;
        }

        if (m_params.transfer1Size > 0 && received < expected) {
            int cx = 0;
            m_device->bulkReadData(
                fb->data.data() + received,
                static_cast<int>(m_params.transfer1Size), cx, 5000);
            received += static_cast<size_t>(cx);
        }

        // ── 4. Trailer 수신 ───────────────────────────────────────────────
        { int t = 0; m_device->bulkReadData(trailerBuf.data(),
            static_cast<int>(trailerBuf.size()), t, 2000); }

        // ── 5. 메타데이터 기록 후 FullQueue push ─────────────────────────
        // (Aravis: arv_stream_push_output_buffer)
        fb->width   = W;
        fb->height  = H;
        fb->pfnc    = pfnc;
        fb->frameId = fid;

        m_fullQueue->push(std::move(fb));
        ++frameCount;
    }

    // ── 수신 종료: 변환 스레드 종료 대기 ────────────────────────────────
    m_fullQueue->stop();
    if (convertThr.joinable()) convertThr.join();

    qDebug("ArvU3vStream: stopped recv=%d dropped=%d "
           "queueDropped=%llu",
           frameCount, dropCount,
           (unsigned long long)m_fullQueue->droppedCount());
}

// ============================================================
// convertLoop() — 변환 스레드
// (Aravis: _loop의 변환+콜백 파트)
//
//   FullQueue에서 채워진 버퍼 꺼냄
//   → AVX2 debayer → QImage
//   → emit ImageReceived (Qt::AutoConnection → UI 스레드)
//   → FreePool 반환 (Aravis: arv_stream_pop_buffer 후 재큐잉)
// ============================================================
void ArvU3vStream::convertLoop()
{
    qDebug("ArvU3vStream::convertLoop started");
    int converted = 0;

    while (true) {
        // FullQueue에서 버퍼 대기 (최대 200ms)
        auto fb = m_fullQueue->popWait(200);
        if (!fb) {
            // 타임아웃: m_running 확인
            if (!m_running) break;
            continue;
        }

        // QImage 재사용: 해상도/포맷이 같으면 재할당 없이 기존 버퍼 사용
        // (5MP BGRA = 20MB → 매 프레임 malloc 방지)
        const int W = fb->width, H = fb->height;
        if (fb->outImage.isNull()
            || fb->outImage.width()  != W
            || fb->outImage.height() != H
            || fb->outImage.format() != QImage::Format_ARGB32) {
            fb->outImage = QImage(W, H, QImage::Format_ARGB32);
        }

        // AVX2 debayer → BGRA32 (non-temporal store)
        convertPixelsBGRA(fb->data.data(), W, H, fb->pfnc,
                           fb->outImage.bits());

        // emit: QImage는 copy-on-write이므로 bits()가 공유됨
        // → 수신 스레드가 fb를 재사용하기 전에 UI가 QPixmap 변환 완료해야 함
        // 안전하게: emit 전에 detach (내부 복사 한 번)
        // ── copy() 제거: swap으로 소유권 이전 ──────────────────────────
        // copy() = 20MB malloc + memcpy → ~15-20ms (핵심 병목)
        // swap() = 포인터 교환 = ~0ns
        // fb->outImage → emitImg로 이전, fb는 null QImage
        // 다음 프레임에서 fb->outImage를 새로 할당 (20MB malloc 1회)
        QImage emitImg;
        emitImg.swap(fb->outImage);   // fb->outImage = null

        m_freePool->push(std::move(fb));  // fb 즉시 반환

        if (!emitImg.isNull()) {
            ++converted;
            emit ImageReceived(std::move(emitImg));
        }
    }

    qDebug("ArvU3vStream::convertLoop stopped converted=%d", converted);
}

// ============================================================
// 픽셀 변환 (BGRA32 직접 출력)
// ============================================================
void ArvU3vStream::convertPixelsBGRA(const uint8_t* src,
                                      int w, int h,
                                      uint32_t pfnc,
                                      uint8_t* dstBits) const
{
    static const bool useAVX2 = debayerAVX2Supported();

    switch (pfnc) {
    // ── Bayer → BGRA (AVX2 non-temporal store) ──────────────────────────
    case PFNC_BayerRG8: case PFNC_BayerGR8:
    case PFNC_BayerGB8: case PFNC_BayerBG8:
        if (useAVX2) {
            debayerAVX2_BGRA(src, dstBits, w, h, pfnc);
        } else {
            // 스칼라 폴백
            auto cl = [](int v, int mx){ return v<0?0:v>=mx?mx-1:v; };
            auto P  = [&](int x, int y) -> int {
                return src[cl(y,h)*w + cl(x,w)]; };
            for (int y = 0; y < h; ++y) {
                uint32_t* row = reinterpret_cast<uint32_t*>(dstBits + y*w*4);
                for (int x = 0; x < w; ++x) {
                    const bool er=(y%2==0), ec=(x%2==0);
                    bool rawR=(pfnc==PFNC_BayerRG8)?(er&&ec)
                              :(pfnc==PFNC_BayerGR8)?(er&&!ec)
                              :(pfnc==PFNC_BayerGB8)?(!er&&ec):(!er&&!ec);
                    bool rawB=(pfnc==PFNC_BayerRG8)?(!er&&!ec)
                              :(pfnc==PFNC_BayerGR8)?(!er&&ec)
                              :(pfnc==PFNC_BayerGB8)?(er&&!ec):(er&&ec);
                    uint8_t r,g,b;
                    if (rawR) {
                        r=src[y*w+x];
                        g=(P(x-1,y)+P(x+1,y)+P(x,y-1)+P(x,y+1))/4;
                        b=(P(x-1,y-1)+P(x+1,y-1)+P(x-1,y+1)+P(x+1,y+1))/4;
                    } else if (rawB) {
                        b=src[y*w+x];
                        g=(P(x-1,y)+P(x+1,y)+P(x,y-1)+P(x,y+1))/4;
                        r=(P(x-1,y-1)+P(x+1,y-1)+P(x-1,y+1)+P(x+1,y+1))/4;
                    } else {
                        g=src[y*w+x];
                        bool gInR=(pfnc==PFNC_BayerRG8||pfnc==PFNC_BayerGB8)
                                  ?(er==ec):(er!=ec);
                        if(gInR){r=(P(x-1,y)+P(x+1,y))/2;b=(P(x,y-1)+P(x,y+1))/2;}
                        else    {b=(P(x-1,y)+P(x+1,y))/2;r=(P(x,y-1)+P(x,y+1))/2;}
                    }
                    row[x] = 0xFF000000u
                           | (static_cast<uint32_t>(r) << 16)
                           | (static_cast<uint32_t>(g) <<  8)
                           |  static_cast<uint32_t>(b);
                }
            }
        }
        return;

    // ── Mono8 → ARGB32 ──────────────────────────────────────────────────
    case PFNC_Mono8: {
        const uint32_t* end32 = reinterpret_cast<const uint32_t*>(dstBits) + w*h;
        uint32_t* d = reinterpret_cast<uint32_t*>(dstBits);
        for (int i = 0; i < w*h; ++i) {
            const uint8_t v = src[i];
            d[i] = 0xFF000000u
                 | (static_cast<uint32_t>(v) << 16)
                 | (static_cast<uint32_t>(v) <<  8)
                 |  static_cast<uint32_t>(v);
        }
        (void)end32;
        return;
    }

    // ── Mono10/12 → ARGB32 ──────────────────────────────────────────────
    case PFNC_Mono10: case PFNC_Mono12: {
        const uint16_t* s16 = reinterpret_cast<const uint16_t*>(src);
        const int shift = (pfnc == PFNC_Mono10) ? 2 : 4;
        uint32_t* d = reinterpret_cast<uint32_t*>(dstBits);
        for (int i = 0; i < w*h; ++i) {
            const uint8_t v = static_cast<uint8_t>(s16[i] >> shift);
            d[i] = 0xFF000000u
                 | (static_cast<uint32_t>(v) << 16)
                 | (static_cast<uint32_t>(v) <<  8)
                 |  static_cast<uint32_t>(v);
        }
        return;
    }

    // ── RGB8 → ARGB32 ───────────────────────────────────────────────────
    case PFNC_RGB8Packed: {
        uint32_t* d = reinterpret_cast<uint32_t*>(dstBits);
        for (int i = 0; i < w*h; ++i)
            d[i] = 0xFF000000u
                 | (static_cast<uint32_t>(src[i*3+0]) << 16)
                 | (static_cast<uint32_t>(src[i*3+1]) <<  8)
                 |  static_cast<uint32_t>(src[i*3+2]);
        return;
    }

    // ── BGR8 → ARGB32 ───────────────────────────────────────────────────
    case PFNC_BGR8Packed: {
        uint32_t* d = reinterpret_cast<uint32_t*>(dstBits);
        for (int i = 0; i < w*h; ++i)
            d[i] = 0xFF000000u
                 | (static_cast<uint32_t>(src[i*3+2]) << 16)
                 | (static_cast<uint32_t>(src[i*3+1]) <<  8)
                 |  static_cast<uint32_t>(src[i*3+0]);
        return;
    }

    // ── YUV422 → ARGB32 ─────────────────────────────────────────────────
    case PFNC_YUV422_8_UYVY: case PFNC_YUV422_8: {
        auto clamp = [](int v) -> uint8_t {
            return static_cast<uint8_t>(v<0?0:v>255?255:v); };
        uint32_t* d = reinterpret_cast<uint32_t*>(dstBits);
        for (int y = 0; y < h; ++y) {
            const uint8_t* s = src + y*w*2;
            uint32_t* row = d + y*w;
            for (int x = 0; x < w; x+=2, s+=4, row+=2) {
                const int u=s[0]-128,y0=s[1],v=s[2]-128,y1=s[3];
                auto mk=[&](int yy)->uint32_t{
                    return 0xFF000000u
                        |(static_cast<uint32_t>(clamp(yy+1402*v/1000))<<16)
                        |(static_cast<uint32_t>(clamp(yy-344*u/1000-714*v/1000))<<8)
                        | static_cast<uint32_t>(clamp(yy+1772*u/1000));};
                row[0]=mk(y0); row[1]=mk(y1);
            }
        }
        return;
    }

    default:
        // 알 수 없는 포맷: Grayscale 폴백
        qWarning("ArvU3vStream: unknown pfnc 0x%08X", pfnc);
        std::memset(dstBits, 0x80, static_cast<size_t>(w)*h*4);
        return;
    }
}
