#include "GigEStream.h"
#include "../genicam/DebayerAVX2.h"
#include "../genicam/ArvU3vProtocol.h"
#include <QDebug>
#include <cstring>
#include <thread>

GigEStream::GigEStream(QObject* parent)
    : QObject(parent)
{
    // 버퍼 초기화는 bind() 시 실제 해상도가 정해지지 않으므로
    // 기본 크기로 할당 (Start()에서 첫 프레임 수신 시 resize)
    m_freePool  = std::make_unique<GigEFramePool>(N_BUFFERS);
    m_fullQueue = std::make_unique<GigEFrameQueue>();
    m_fullQueue->setPool(m_freePool.get());
}

GigEStream::~GigEStream()
{
    Stop();
    m_socket.close();
}

bool GigEStream::bind(uint16_t port)
{
    // port=0: 임의 포트 자동 선택
    if (!m_socket.bind(QHostAddress(QHostAddress::AnyIPv4), port,
                       QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning("GigEStream::bind: %s",
                 qPrintable(m_socket.errorString()));
        return false;
    }
    m_port = m_socket.localPort();
    qDebug("GigEStream: bound to port %u", m_port);
    return true;
}

void GigEStream::Stop()
{
    m_running = false;
    m_fullQueue->stop();
    m_socket.close();
}

// ============================================================
// Start() — 수신 스레드 (별도 QThread에서 실행)
// ============================================================
void GigEStream::Start()
{
    qDebug("GigEStream::Start() on port %u", m_port);

    // 변환 스레드 시작
    std::thread convertThr([this]{ convertLoop(); });

    const int MTU = 9000 + 28;  // Jumbo frame 최대
    QByteArray buf(MTU, 0);

    while (m_running) {
        if (!m_socket.waitForReadyRead(200)) continue;

        while (m_socket.hasPendingDatagrams() && m_running) {
            const qint64 sz = m_socket.readDatagram(
                buf.data(), buf.size());
            if (sz > 0)
                processPacket(reinterpret_cast<const uint8_t*>(
                    buf.constData()), static_cast<int>(sz));
        }
    }

    m_fullQueue->stop();
    if (convertThr.joinable()) convertThr.join();

    qDebug("GigEStream: stopped, queue_dropped=%llu",
           (unsigned long long)m_fullQueue->droppedCount());
}

// ── processPacket: GVSP 패킷 파싱 및 프레임 조립 ─────────────────────────────
void GigEStream::processPacket(const uint8_t* data, int size)
{
    // GVSP 최소 크기: status(2) + block_id(2) + packet_infos(4) = 8 bytes
    if (size < 8) return;

    const uint16_t status = (uint16_t(data[0]) << 8) | data[1];
    if (status != GVSP_STATUS_SUCCESS) return;

    const uint16_t blockId    = (uint16_t(data[2]) << 8) | data[3];
    const uint32_t pktInfos   = (uint32_t(data[4]) << 24) |
                                 (uint32_t(data[5]) << 16) |
                                 (uint32_t(data[6]) << 8)  |
                                  uint32_t(data[7]);

    const uint8_t  contentType = uint8_t((pktInfos >> 24) & 0x7F);
    const uint32_t packetId    = pktInfos & 0x00FFFFFF;

    const uint8_t* payload = data + 8;
    const int      payloadSize = size - 8;

    switch (contentType) {

    // ── LEADER ───────────────────────────────────────────────────────────────
    case GVSP_CONTENT_LEADER: {
        if (payloadSize < int(sizeof(GvspImageLeader))) return;

        const auto* ldr = reinterpret_cast<const GvspImageLeader*>(payload);
        const uint16_t payloadType =
            (uint16_t(ldr->payload_type >> 8) & 0xFF) |
            ((uint16_t(ldr->payload_type) & 0xFF) << 8);  // big-endian
        // 직접 파싱 (네트워크 바이트 순서)
        const uint16_t pt   = uint16_t((payload[0] << 8) | payload[1]);
        (void)pt;

        auto readBE32 = [&](const uint8_t* p) -> uint32_t {
            return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|
                   (uint32_t(p[2])<<8)|uint32_t(p[3]);
        };
        const uint32_t pfnc = readBE32(payload + 8);   // pixel_format
        const uint32_t w    = readBE32(payload + 12);  // width
        const uint32_t h    = readBE32(payload + 16);  // height

        if (w == 0 || h == 0) return;

        // 새 프레임 시작 → 버퍼 획득
        auto fb = m_freePool->tryPop();
        if (!fb) {
            // 버퍼 부족: 드롭
            qDebug("GigEStream: frame drop (no free buffer)");
            m_current.reset();
            m_currentBlockId = blockId;
            return;
        }

        // 버퍼 초기화
        const size_t rawSize = static_cast<size_t>(w) * h * 2; // 최대 16bit/px
        if (fb->raw.size() < rawSize) fb->raw.resize(rawSize);
        const size_t pktCount = (rawSize + 8191) / 8192 + 2; // 추정
        fb->received.assign(pktCount, false);

        fb->width             = static_cast<int>(w);
        fb->height            = static_cast<int>(h);
        fb->pfnc              = pfnc;
        fb->blockId           = blockId;
        fb->leaderReceived    = true;
        fb->trailerReceived   = false;
        fb->totalPackets      = 0;

        m_current        = std::move(fb);
        m_currentBlockId = blockId;
        break;
    }

    // ── DATA_BLOCK ────────────────────────────────────────────────────────────
    case GVSP_CONTENT_PAYLOAD: {
        if (!m_current || m_currentBlockId != blockId) return;
        if (payloadSize <= 0) return;

        // packetId는 1-based (1 = 첫 데이터 패킷, leader=0)
        // 데이터 오프셋 계산
        // 각 데이터 패킷 크기는 일정 (마지막 제외)
        // 첫 패킷(packetId=1)이 오면 패킷 크기를 확정
        if (m_current->packetSize == 0 && packetId == 1)
            m_current->packetSize = static_cast<uint32_t>(payloadSize);

        const uint32_t pktSize = m_current->packetSize > 0
                                 ? m_current->packetSize
                                 : static_cast<uint32_t>(payloadSize);
        const size_t offset = static_cast<size_t>(packetId - 1) * pktSize;

        if (offset + payloadSize > m_current->raw.size())
            m_current->raw.resize(offset + payloadSize);
        if (packetId >= m_current->received.size())
            m_current->received.resize(packetId + 1, false);

        std::memcpy(m_current->raw.data() + offset, payload,
                    static_cast<size_t>(payloadSize));
        m_current->received[packetId] = true;
        break;
    }

    // ── TRAILER ───────────────────────────────────────────────────────────────
    case GVSP_CONTENT_TRAILER: {
        if (!m_current || m_currentBlockId != blockId) return;

        m_current->trailerReceived = true;
        // totalPackets = last data packet_id (TRAILER 직전)
        m_current->totalPackets = packetId; // TRAILER의 packetId는 마지막 데이터 다음

        // 프레임 완성 → 변환 큐로 push
        m_fullQueue->push(std::move(m_current));
        m_current.reset();
        break;
    }

    default:
        break;
    }
}

// ── convertLoop: 변환 스레드 ─────────────────────────────────────────────────
void GigEStream::convertLoop()
{
    qDebug("GigEStream::convertLoop started");
    int converted = 0;

    while (true) {
        auto fb = m_fullQueue->popWait(200);
        if (!fb) {
            if (!m_running) break;
            continue;
        }

        QImage img = convertFrame(*fb);
        m_freePool->push(std::move(fb));

        if (!img.isNull()) {
            ++converted;
            emit ImageReceived(std::move(img));
        }
    }

    qDebug("GigEStream::convertLoop stopped converted=%d", converted);
}

// ── convertFrame: 픽셀 변환 (ArvU3vStream과 동일 PFNC 사용) ─────────────────
QImage GigEStream::convertFrame(GigEFrameBuffer& fb)
{
    const int w = fb.width, h = fb.height;
    if (w <= 0 || h <= 0 || fb.raw.empty()) return {};

    static const bool useAVX2 = debayerAVX2Supported();

    // QImage 재사용
    if (fb.outImage.isNull() || fb.outImage.width() != w ||
        fb.outImage.height() != h ||
        fb.outImage.format() != QImage::Format_ARGB32)
        fb.outImage = QImage(w, h, QImage::Format_ARGB32);

    const uint8_t* src = fb.raw.data();
    uint8_t*       dst = fb.outImage.bits();

    switch (fb.pfnc) {
    case PFNC_BayerRG8: case PFNC_BayerGR8:
    case PFNC_BayerGB8: case PFNC_BayerBG8:
        if (useAVX2)
            debayerAVX2_BGRA(src, dst, w, h, fb.pfnc);
        else {
            // 스칼라 폴백
            auto cl = [](int v, int mx){ return v<0?0:v>=mx?mx-1:v; };
            auto P  = [&](int x, int y) -> int {
                return src[cl(y,h)*w + cl(x,w)]; };
            for (int y = 0; y < h; ++y) {
                uint32_t* row = reinterpret_cast<uint32_t*>(
                    fb.outImage.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const bool er=(y%2==0), ec=(x%2==0);
                    bool rawR=(fb.pfnc==PFNC_BayerRG8)?(er&&ec)
                             :(fb.pfnc==PFNC_BayerGR8)?(er&&!ec)
                             :(fb.pfnc==PFNC_BayerGB8)?(!er&&ec):(!er&&!ec);
                    bool rawB=(fb.pfnc==PFNC_BayerRG8)?(!er&&!ec)
                             :(fb.pfnc==PFNC_BayerGR8)?(!er&&ec)
                             :(fb.pfnc==PFNC_BayerGB8)?(er&&!ec):(er&&ec);
                    uint8_t r,g,b;
                    if(rawR){r=src[y*w+x];g=(P(x-1,y)+P(x+1,y)+P(x,y-1)+P(x,y+1))/4;b=(P(x-1,y-1)+P(x+1,y-1)+P(x-1,y+1)+P(x+1,y+1))/4;}
                    else if(rawB){b=src[y*w+x];g=(P(x-1,y)+P(x+1,y)+P(x,y-1)+P(x,y+1))/4;r=(P(x-1,y-1)+P(x+1,y-1)+P(x-1,y+1)+P(x+1,y+1))/4;}
                    else{g=src[y*w+x];bool gInR=(fb.pfnc==PFNC_BayerRG8||fb.pfnc==PFNC_BayerGB8)?(er==ec):(er!=ec);
                         if(gInR){r=(P(x-1,y)+P(x+1,y))/2;b=(P(x,y-1)+P(x,y+1))/2;}else{b=(P(x-1,y)+P(x+1,y))/2;r=(P(x,y-1)+P(x,y+1))/2;}}
                    row[x]=0xFF000000u|(uint32_t(r)<<16)|(uint32_t(g)<<8)|b;
                }
            }
        }
        break;

    case PFNC_Mono8: {
        QImage gray(w, h, QImage::Format_Grayscale8);
        std::memcpy(gray.bits(), src, static_cast<size_t>(w * h));
        fb.outImage = gray.convertToFormat(QImage::Format_ARGB32);
        break;
    }

    case PFNC_Mono10: case PFNC_Mono12: {
        const uint16_t* s16 = reinterpret_cast<const uint16_t*>(src);
        const int shift = (fb.pfnc == PFNC_Mono10) ? 2 : 4;
        uint32_t* d = reinterpret_cast<uint32_t*>(dst);
        for (int i = 0; i < w*h; ++i) {
            const uint8_t v = uint8_t(s16[i] >> shift);
            d[i] = 0xFF000000u|(uint32_t(v)<<16)|(uint32_t(v)<<8)|v;
        }
        break;
    }

    case PFNC_RGB8Packed: {
        uint32_t* d = reinterpret_cast<uint32_t*>(dst);
        for (int i = 0; i < w*h; ++i)
            d[i] = 0xFF000000u|(uint32_t(src[i*3])<<16)|(uint32_t(src[i*3+1])<<8)|src[i*3+2];
        break;
    }

    default:
        qWarning("GigEStream: unsupported pfnc 0x%08X", fb.pfnc);
        return {};
    }

    return fb.outImage;
}
