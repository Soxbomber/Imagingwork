#include "GigEStream.h"
#include "../genicam/DebayerAVX2.h"
#include "../genicam/ArvU3vProtocol.h"
#include <QDebug>
#include <QAbstractSocket>
#include <cstring>
#include <thread>

// NDIS GvspCapture는 별도 드라이버 빌드 필요 → 현재는 Qt 소켓 방식 사용
// 드라이버 준비 후 HAVE_NDIS_CAPTURE 를 정의하고 GvspCapture.cpp를 추가할 것

GigEStream::GigEStream(QObject* parent)
    : QObject(parent)
{
    m_freePool  = std::make_unique<GigEFramePool>(N_BUFFERS);
    m_fullQueue = std::make_unique<GigEFrameQueue>();
    m_fullQueue->setPool(m_freePool.get());
}

GigEStream::~GigEStream()
{
    Stop();
}

void GigEStream::Stop()
{
    m_running = false;
    m_fullQueue->stop();
}

void GigEStream::Start()
{
    // Qt QUdpSocket + SO_RCVBUF 8MB
    // NDIS 드라이버 사용 시: HAVE_NDIS_CAPTURE 정의 + GvspCapture.cpp 빌드에 추가
    QUdpSocket sock;
    sock.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,
                         8 * 1024 * 1024);
    if (!sock.bind(QHostAddress(QHostAddress::AnyIPv4), m_bindPort,
                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning("GigEStream::Start: bind failed: %s",
                 qPrintable(sock.errorString()));
        emit bindCompleted(0, false);
        return;
    }
    m_port = sock.localPort();
    qDebug("GigEStream: Qt socket bound port=%u (NDIS driver not installed)", m_port);
    emit bindCompleted(m_port, true);

    std::thread cvt([this]{ convertLoop(); });
    QByteArray buf(GVSP_MTU, 0);

    while (m_running) {
        if (!sock.waitForReadyRead(200)) continue;
        while (sock.hasPendingDatagrams() && m_running) {
            const qint64 sz = sock.readDatagram(buf.data(), buf.size());
            if (sz > 0)
                processPacket(reinterpret_cast<const uint8_t*>(buf.constData()),
                              static_cast<int>(sz));
        }
    }

    sock.close();
    m_fullQueue->stop();
    if (cvt.joinable()) cvt.join();
    qDebug("GigEStream: Qt stopped dropped=%llu",
           (unsigned long long)m_fullQueue->droppedCount());
}

// ── processPacket: GVSP packet parsing and frame assembly ────────────────────
// Supports both standard (16-bit block ID) and extended (64-bit block ID, GV 2.0+)
// header formats per GV 2.2 §16.2.
void GigEStream::processPacket(const uint8_t* data, int size)
{
    // Minimum GVSP header: status(2) + block_id(2) + packet_infos(4) = 8 bytes
    if (size < GVSP_HEADER_SIZE) return;

    const uint16_t status = (uint16_t(data[0]) << 8) | data[1];
    if (status != GVSP_STATUS_SUCCESS) return;

    const uint32_t pktInfos = (uint32_t(data[4]) << 24) |
                               (uint32_t(data[5]) << 16) |
                               (uint32_t(data[6]) <<  8) |
                                uint32_t(data[7]);

    const bool extMode = (pktInfos & GVSP_EXT_ID_FLAG) != 0;
    const uint8_t contentType =
        uint8_t((pktInfos & GVSP_CONTENT_TYPE_MASK) >> GVSP_CONTENT_TYPE_SHIFT);

    uint64_t       blockId;
    uint32_t       packetId;
    const uint8_t* payload;
    int            payloadSize;

    if (extMode) {
        // Extended header (GV 2.0+): 20 bytes total
        // byte[8-11]:  block_id_high32
        // byte[12-15]: block_id_low32
        // byte[16-19]: 32-bit packet_id
        if (size < GVSP_EXT_HEADER_SIZE) return;

        const uint32_t blockIdHigh = (uint32_t(data[ 8]) << 24) |
                                      (uint32_t(data[ 9]) << 16) |
                                      (uint32_t(data[10]) <<  8) |
                                       uint32_t(data[11]);
        const uint32_t blockIdLow  = (uint32_t(data[12]) << 24) |
                                      (uint32_t(data[13]) << 16) |
                                      (uint32_t(data[14]) <<  8) |
                                       uint32_t(data[15]);
        blockId   = (uint64_t(blockIdHigh) << 32) | blockIdLow;
        packetId  = (uint32_t(data[16]) << 24) |
                    (uint32_t(data[17]) << 16) |
                    (uint32_t(data[18]) <<  8) |
                     uint32_t(data[19]);
        payload     = data + GVSP_EXT_HEADER_SIZE;
        payloadSize = size - GVSP_EXT_HEADER_SIZE;
    } else {
        blockId   = (uint16_t(data[2]) << 8) | data[3];
        packetId  = pktInfos & GVSP_PACKET_ID_MASK;
        payload     = data + GVSP_HEADER_SIZE;
        payloadSize = size - GVSP_HEADER_SIZE;
    }

    switch (contentType) {

    // ── LEADER ────────────────────────────────────────────────────────────────
    case GVSP_CONTENT_LEADER: {
        // GV 2.2 Table 16-9 Image Data Leader layout:
        //   payload[0-1]   = reserved
        //   payload[2-3]   = payload_type (big-endian, 0x0001 = IMAGE)
        //   payload[4-7]   = timestamp_high
        //   payload[8-11]  = timestamp_low
        //   payload[12-15] = pixel_format (PFNC, big-endian)
        //   payload[16-19] = size_x / width
        //   payload[20-23] = size_y / height
        if (payloadSize < GVSP_LEADER_MIN_SIZE) return;

        auto readBE16 = [](const uint8_t* p) -> uint16_t {
            return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
        };
        auto readBE32 = [](const uint8_t* p) -> uint32_t {
            return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|
                   (uint32_t(p[2])<<8) | uint32_t(p[3]);
        };

        // payload_type is at offset 2, NOT offset 8
        const uint16_t payloadType = readBE16(payload + 2);
        if (payloadType != GVSP_PAYLOAD_TYPE_IMAGE) {
            qDebug("GigEStream: LEADER block=0x%llX unsupported payload_type=0x%04X",
                   (unsigned long long)blockId, payloadType);
            return;
        }

        const uint32_t pfnc = readBE32(payload + 12);
        const uint32_t w    = readBE32(payload + 16);
        const uint32_t h    = readBE32(payload + 20);

        if (w == 0 || h == 0 || w > 16384 || h > 16384) {
            qWarning("GigEStream: invalid image size %ux%u (pfnc=0x%08X)", w, h, pfnc);
            return;
        }

        qDebug("GigEStream: LEADER block=0x%llX pfnc=0x%08X %ux%u%s",
               (unsigned long long)blockId, pfnc, w, h,
               extMode ? " [ext]" : "");

        auto fb = m_freePool->tryPop();
        if (!fb) {
            qDebug("GigEStream: frame drop (no free buffer)");
            m_current.reset();
            m_currentBlockId = blockId;
            return;
        }

        const size_t rawSize = static_cast<size_t>(w) * h * 2;
        if (fb->raw.size() < rawSize) fb->raw.resize(rawSize);

        const size_t pktCount = rawSize / 8000 + 4;
        fb->received.assign(pktCount, false);

        fb->width           = static_cast<int>(w);
        fb->height          = static_cast<int>(h);
        fb->pfnc            = pfnc;
        fb->blockId         = blockId;
        fb->packetSize      = 0;
        fb->leaderReceived  = true;
        fb->trailerReceived = false;
        fb->totalPackets    = 0;

        m_current        = std::move(fb);
        m_currentBlockId = blockId;
        break;
    }

    // ── PAYLOAD (data block) ──────────────────────────────────────────────────
    case GVSP_CONTENT_PAYLOAD: {
        if (!m_current || m_currentBlockId != blockId) return;
        if (payloadSize <= 0) return;

        // packet IDs are 1-based; leader = 0, first data = 1
        if (m_current->packetSize == 0 && packetId == 1)
            m_current->packetSize = static_cast<uint32_t>(payloadSize);

        const uint32_t pktSize = m_current->packetSize > 0
                                 ? m_current->packetSize
                                 : static_cast<uint32_t>(payloadSize);
        const size_t offset = static_cast<size_t>(packetId - 1) * pktSize;

        if (offset + static_cast<size_t>(payloadSize) > m_current->raw.size())
            m_current->raw.resize(offset + static_cast<size_t>(payloadSize));
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
        m_current->totalPackets    = packetId;

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