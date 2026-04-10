#pragma once
// ============================================================
// ArvU3vStream - Aravis 방식 N버퍼 파이프라인
//
// [USB 수신 스레드]          [변환 스레드 (Start() 내부)]
//  FreePool에서 버퍼 획득      FullQueue에서 버퍼 획득
//  bulkRead(raw)              convertPixelsBGRA()
//  FullQueue push             emit ImageReceived()
//  ↓ 즉시 다음 수신           FreePool push (반환)
//
// N=3 (트리플 버퍼): 수신·변환·여분 동시 운용
// ============================================================
#include <QObject>
#include <QImage>
#include <QThread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <memory>
#include "ArvU3vDevice.h"

// 단일 raw 프레임 버퍼
struct FrameBuffer {
    std::vector<uint8_t> data;  // raw Bayer/Mono 데이터
    int      width{};
    int      height{};
    uint32_t pfnc{};            // PFNC 픽셀 포맷
    uint64_t frameId{};
    // 변환 결과 QImage 재사용 (매 프레임 20MB malloc 방지)
    QImage   outImage;          // convertPixelsBGRA 출력 캐시
};

// lock-free deque (mutex 기반, N 소규모)
class FramePool {
public:
    explicit FramePool(int n, size_t bufSize)
    {
        for (int i = 0; i < n; ++i) {
            auto fb = std::make_unique<FrameBuffer>();
            fb->data.resize(bufSize);
            m_pool.push_back(std::move(fb));
        }
    }

    // 빈 버퍼 획득 (없으면 nullptr 즉시 반환)
    std::unique_ptr<FrameBuffer> tryPop()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_pool.empty()) return nullptr;
        auto fb = std::move(m_pool.front());
        m_pool.pop_front();
        return fb;
    }

    // 버퍼 반환
    void push(std::unique_ptr<FrameBuffer> fb)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_pool.push_back(std::move(fb));
    }

private:
    std::mutex m_mtx;
    std::deque<std::unique_ptr<FrameBuffer>> m_pool;
};

// 수신 완료 버퍼 큐 (변환 스레드가 꺼냄)
class FrameQueue {
public:
    // 큐에 추가 (수신 스레드)
    void push(std::unique_ptr<FrameBuffer> fb)
    {
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            // 큐가 가득 차면 가장 오래된 것 버리고 FreePool로 반환
            // (변환 스레드가 따라오지 못할 때 최신 프레임 우선)
            if (m_queue.size() >= MAX_QUEUE) {
                m_dropped++;
                m_pool->push(std::move(m_queue.front()));
                m_queue.pop_front();
            }
            m_queue.push_back(std::move(fb));
        }
        m_cv.notify_one();
    }

    // 변환 스레드: 타임아웃 대기
    std::unique_ptr<FrameBuffer> popWait(int timeoutMs)
    {
        std::unique_lock<std::mutex> lk(m_mtx);
        if (!m_cv.wait_for(lk,
                std::chrono::milliseconds(timeoutMs),
                [this]{ return !m_queue.empty() || m_stopped; }))
            return nullptr;
        if (m_queue.empty()) return nullptr;
        auto fb = std::move(m_queue.front());
        m_queue.pop_front();
        return fb;
    }

    void stop()
    {
        { std::lock_guard<std::mutex> lk(m_mtx); m_stopped = true; }
        m_cv.notify_all();
    }

    void setPool(FramePool* p) { m_pool = p; }

    uint64_t droppedCount() const { return m_dropped; }

private:
    static constexpr size_t MAX_QUEUE = 2; // 큐 최대 깊이
    std::mutex              m_mtx;
    std::condition_variable m_cv;
    std::deque<std::unique_ptr<FrameBuffer>> m_queue;
    FramePool*              m_pool{};
    bool                    m_stopped{false};
    uint64_t                m_dropped{0};
};


class ArvU3vStream : public QObject
{
    Q_OBJECT
public:
    // N_BUFFERS: Aravis ARV_GENTL_STREAM_DEFAULT_N_BUFFERS = 3
    static constexpr int N_BUFFERS = 3;

    explicit ArvU3vStream(ArvU3vDevice*          device,
                          const ArvStreamParams& params,
                          QObject*               parent = nullptr);
    ~ArvU3vStream() override;

    void Stop();

public slots:
    // USB 수신 스레드에서 실행 (QThread::started 에 연결)
    void Start();

signals:
    void ImageReceived(QImage image);
    void errorOccurred(const QString& message);

private:
    // 변환 스레드 루프 (별도 QThread에서 실행)
    void convertLoop();

    // BGRA32를 dstBits에 직접 출력 (QImage 재사용, malloc 방지)
    void convertPixelsBGRA(const uint8_t* src, int w, int h,
                            uint32_t pfnc, uint8_t* dstBits) const;

    ArvU3vDevice*    m_device;
    ArvStreamParams  m_params;
    std::atomic<bool> m_running{true};

    // N개 pre-allocated 버퍼 풀
    std::unique_ptr<FramePool>  m_freePool;
    std::unique_ptr<FrameQueue> m_fullQueue;

    // 변환 전용 스레드는 Start() 내에서 std::thread로 관리
};
