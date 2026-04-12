#pragma once
// ============================================================
// GigEStream.h
// GigE Vision GVSP 스트림 수신기 (Aravis arvgvstream.c 포팅)
//
// GVSP over UDP:
//   LEADER → DATA_BLOCK(×N) → TRAILER 순으로 프레임 조립
//   패킷 순서 역전(reorder) / 손실(drop) 처리
//   N버퍼 풀 기반 (ArvU3vStream과 동일 구조)
// ============================================================

#include "GigEProtocol.h"
#include "../genicam/ArvGenApiXml.h"
#include <QObject>
#include <QUdpSocket>
#include <QImage>
#include <QThread>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>

// ── 프레임 버퍼 ──────────────────────────────────────────────────────────────
struct GigEFrameBuffer {
    std::vector<uint8_t> raw;       // raw 픽셀 데이터
    std::vector<bool>    received;  // 패킷 수신 여부 (패킷 재조립용)
    int      width{};
    int      height{};
    uint32_t pfnc{};
    uint32_t packetSize{};   // 데이터 패킷당 페이로드 크기
    uint32_t totalPackets{}; // 전체 패킷 수 (TRAILER에서 확인)
    uint32_t blockId{};      // 현재 프레임 ID
    bool     leaderReceived{false};
    bool     trailerReceived{false};
    QImage   outImage;       // 변환 결과 (재사용)
};

// ── 버퍼 풀 / 큐 ─────────────────────────────────────────────────────────────
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
    static constexpr int N_BUFFERS = 3;

    explicit GigEStream(QObject* parent = nullptr);
    ~GigEStream() override;

    // 수신 시작 (Start()는 별도 스레드에서 실행)
    bool bind(uint16_t port);
    uint16_t boundPort() const { return m_port; }

    void Stop();

public slots:
    void Start();  // 수신 스레드 진입점

signals:
    void ImageReceived(QImage image);
    void errorOccurred(const QString& msg);

private:
    void convertLoop();
    QImage convertFrame(GigEFrameBuffer& fb);

    // GVSP 패킷 파싱
    void processPacket(const uint8_t* data, int size);

    QUdpSocket  m_socket;
    uint16_t    m_port{};

    std::atomic<bool> m_running{true};

    // N버퍼 풀
    std::unique_ptr<GigEFramePool>  m_freePool;
    std::unique_ptr<GigEFrameQueue> m_fullQueue;

    // 현재 수신 중인 프레임 버퍼
    std::unique_ptr<GigEFrameBuffer> m_current;
    uint16_t m_currentBlockId{0xFFFF};
};
