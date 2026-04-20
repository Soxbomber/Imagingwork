#pragma once
// ============================================================
// GigEStream.h  –  Qt 기반 GVSP 수신기
// QUdpSocket + SO_RCVBUF 8MB + 전용 수신 스레드
// ============================================================
#include "GigEProtocol.h"
#include "../genicam/ArvGenApiXml.h"
#include <QObject>
#include <QUdpSocket>
#include <QImage>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>
#include <thread>

static constexpr int GVSP_MTU     = 9100;
static constexpr int IOCP_PENDING = 16;   // 하위 호환용 (미사용)

// ── 프레임 버퍼 ──────────────────────────────────────────────────────────────
struct GigEFrameBuffer {
    std::vector<uint8_t> raw;
    std::vector<bool>    received;
    int      width{};
    int      height{};
    uint32_t pfnc{};
    uint32_t packetSize{};
    uint32_t totalPackets{};
    uint64_t blockId{};          // 64-bit block ID (GV 2.0+ extended mode)
    bool     leaderReceived{false};
    bool     trailerReceived{false};
    QImage   outImage;
};

class GigEFramePool {
public:
    explicit GigEFramePool(int n) {
        for (int i = 0; i < n; ++i)
            m_pool.push_back(std::make_unique<GigEFrameBuffer>());
    }
    std::unique_ptr<GigEFrameBuffer> tryPop() {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_pool.empty()) return nullptr;
        auto fb = std::move(m_pool.front());
        m_pool.pop_front();
        return fb;
    }
    void push(std::unique_ptr<GigEFrameBuffer> fb) {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_pool.push_back(std::move(fb));
    }
private:
    std::mutex m_mtx;
    std::deque<std::unique_ptr<GigEFrameBuffer>> m_pool;
};

class GigEFrameQueue {
public:
    static constexpr size_t MAX_QUEUE = 2;
    void setPool(GigEFramePool* p) { m_pool = p; }
    void push(std::unique_ptr<GigEFrameBuffer> fb) {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            if (m_queue.size() >= MAX_QUEUE) {
                m_pool->push(std::move(m_queue.front()));
                m_queue.pop_front();
                ++m_dropped;
            }
            m_queue.push_back(std::move(fb));
        }
        m_cv.notify_one();
    }
    std::unique_ptr<GigEFrameBuffer> popWait(int ms) {
        std::unique_lock<std::mutex> lk(m_mtx);
        if (!m_cv.wait_for(lk, std::chrono::milliseconds(ms),
                [this]{ return !m_queue.empty() || m_stopped; }))
            return nullptr;
        if (m_queue.empty()) return nullptr;
        auto fb = std::move(m_queue.front());
        m_queue.pop_front();
        return fb;
    }
    void stop() {
        { std::lock_guard<std::mutex> lk(m_mtx); m_stopped = true; }
        m_cv.notify_all();
    }
    uint64_t droppedCount() const { return m_dropped; }
private:
    std::mutex              m_mtx;
    std::condition_variable m_cv;
    std::deque<std::unique_ptr<GigEFrameBuffer>> m_queue;
    GigEFramePool*          m_pool{};
    bool                    m_stopped{false};
    uint64_t                m_dropped{0};
};

// ── GigEStream ────────────────────────────────────────────────────────────────
class GigEStream : public QObject
{
    Q_OBJECT
public:
    static constexpr int N_BUFFERS = 4;

    explicit GigEStream(QObject* parent = nullptr);
    ~GigEStream() override;

    void setBindPort(uint16_t port) { m_bindPort = port; }
    uint16_t boundPort() const { return m_port; }

    void Stop();

public slots:
    void Start();

signals:
    void ImageReceived(QImage image);
    void errorOccurred(const QString& msg);
    void bindCompleted(uint16_t port, bool ok);

private:
    void processPacket(const uint8_t* data, int size);
    void convertLoop();
    QImage convertFrame(GigEFrameBuffer& fb);

    uint16_t  m_bindPort{0};
    uint16_t  m_port{0};

    std::atomic<bool> m_running{true};

    std::unique_ptr<GigEFramePool>  m_freePool;
    std::unique_ptr<GigEFrameQueue> m_fullQueue;

    std::unique_ptr<GigEFrameBuffer> m_current;
    uint64_t m_currentBlockId{0xFFFFFFFFFFFFFFFFULL};  // 64-bit block ID
};
