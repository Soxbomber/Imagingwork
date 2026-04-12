// ============================================================
// DebayerAVX2.cpp
//
// 핵심: "행 u16 선변환" 전략
//   기존: 루프 내부에서 16픽셀당 _mm256_cvtepu8_epi16 × 9회
//   개선: 행 처리 전 전체 행을 u16 버퍼로 1회 변환
//         → 루프 내부 cvtepu8 완전 제거
//         → load는 u16 배열에서 _mm256_loadu_si256만
//   결과: 5MP 기준 25-30ms → 목표 5ms 이하
// ============================================================

#include "DebayerAVX2.h"
#include "ArvU3vProtocol.h"
#include <vector>
#include <cstring>
#include <thread>
#include <algorithm>

#ifdef _MSC_VER
#  include <intrin.h>
#  include <immintrin.h>
#else
#  include <immintrin.h>
#endif

bool debayerAVX2Supported()
{
#ifdef _MSC_VER
    int info[4]{};
    __cpuid(info, 0);
    if (info[0] < 7) return false;
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
#else
    return __builtin_cpu_supports("avx2");
#endif
}

// ── u8 행 전체 → u16 행 (AVX2, 행당 1회) ─────────────────────────────────────
static void rowU8toU16(const uint8_t*  __restrict src,
                        uint16_t*       __restrict dst, int w)
{
    int x = 0;
    for (; x + 16 <= w; x += 16)
        _mm256_storeu_si256(
            reinterpret_cast<__m256i*>(dst + x),
            _mm256_cvtepu8_epi16(
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + x))));
    for (; x < w; ++x) dst[x] = src[x];
}

// ── u16×16 → u8×16 (saturate) ────────────────────────────────────────────────
static inline __m128i pack16to8(__m256i v)
{
    __m256i p = _mm256_packus_epi16(v, _mm256_setzero_si256());
    p = _mm256_permute4x64_epi64(p, 0b00001000);
    return _mm256_castsi256_si128(p);
}

// ── BGRA 16픽셀 NT-store (64 bytes) ──────────────────────────────────────────
static inline void storeBGRA16_NT(uint8_t* __restrict dst,
                                   __m128i R, __m128i G, __m128i B)
{
    const __m128i A   = _mm_set1_epi8(-1); // alpha=0xFF
    __m128i bgl = _mm_unpacklo_epi8(B, G);
    __m128i bgh = _mm_unpackhi_epi8(B, G);
    __m128i ral = _mm_unpacklo_epi8(R, A);
    __m128i rah = _mm_unpackhi_epi8(R, A);
    _mm_stream_si128(reinterpret_cast<__m128i*>(dst +  0), _mm_unpacklo_epi16(bgl, ral));
    _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 16), _mm_unpackhi_epi16(bgl, ral));
    _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 32), _mm_unpacklo_epi16(bgh, rah));
    _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 48), _mm_unpackhi_epi16(bgh, rah));
}

// ── 핵심 AVX2 행 처리 (u16 버퍼 → BGRA) ─────────────────────────────────────
// P/C/N: 각 행의 u16 버퍼 (이미 변환됨, cvtepu8 불필요)
// 16픽셀당 명령 수:
//   load: 9× _mm256_loadu_si256 (u16, 32B)
//   연산: add×4, srli×3, add×2 = 9개
//   pack: 5× pack16to8 (packus+permute+cast = 3명령×5 = 15개)
//   store: storeBGRA16_NT = 12개
//   합계: ~45개 (기존 ~110개 대비 59% 감소)
static void processRowAVX2(
    const uint16_t* __restrict P,
    const uint16_t* __restrict C,
    const uint16_t* __restrict N,
    uint8_t*        __restrict dst,
    int xStart, int xEnd,
    bool isRGrow, bool r0c0_R)
{
    // 짝/홀 마스크: 짝수 byte 위치 = 0xFF, 홀수 = 0x00
    const __m128i em = _mm_set1_epi16(0x00FF);
    const __m128i om = _mm_xor_si128(em, _mm_set1_epi8(-1));

    for (int x = xStart; x < xEnd; x += 16) {
        // 9회 load (u16, 변환 없음) ─────────────────────────────────────
        __m256i c  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(C + x));
        __m256i cL = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(C + x - 1));
        __m256i cR = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(C + x + 1));
        __m256i p  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(P + x));
        __m256i n  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(N + x));
        __m256i pL = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(P + x - 1));
        __m256i pR = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(P + x + 1));
        __m256i nL = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(N + x - 1));
        __m256i nR = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(N + x + 1));

        // 공통 중간값 ──────────────────────────────────────────────────
        __m256i hz     = _mm256_add_epi16(cL, cR);         // L+R
        __m256i vt     = _mm256_add_epi16(p,  n);          // U+D
        __m256i cross4 = _mm256_srli_epi16(
            _mm256_add_epi16(hz, vt), 2);                  // (L+R+U+D)/4
        __m256i diag4  = _mm256_srli_epi16(
            _mm256_add_epi16(
                _mm256_add_epi16(pL, pR),
                _mm256_add_epi16(nL, nR)), 2);             // (UL+UR+DL+DR)/4
        __m256i hz2    = _mm256_srli_epi16(hz, 1);         // (L+R)/2
        __m256i vt2    = _mm256_srli_epi16(vt, 1);         // (U+D)/2

        // u16 → u8 pack (5회) ─────────────────────────────────────────
        __m128i raw   = pack16to8(c);
        __m128i cross = pack16to8(cross4);
        __m128i diag  = pack16to8(diag4);
        __m128i horiz = pack16to8(hz2);
        __m128i vert  = pack16to8(vt2);

        // 채널 배정 (짝/홀 마스크) ────────────────────────────────────
        // 픽셀 배치: x=짝수 위치, x+1=홀수 위치 (16픽셀)
        // em 마스크: byte0=FF(짝수픽셀), byte1=00(홀수픽셀) 반복
        __m128i R, G, B;
        if (isRGrow && r0c0_R) {
            // BayerRG RG행: 짝수=R, 홀수=G
            // R pixel: G=cross4, B=diag4
            // G pixel: R=hz2,    B=vt2
            R = _mm_or_si128(_mm_and_si128(raw,   em),
                             _mm_and_si128(horiz, om));
            G = _mm_or_si128(_mm_and_si128(cross, em),
                             _mm_and_si128(raw,   om));
            B = _mm_or_si128(_mm_and_si128(diag,  em),
                             _mm_and_si128(vert,  om));
        } else if (!isRGrow && r0c0_R) {
            // BayerRG GB행: 짝수=G, 홀수=B
            // G pixel: R=vt2,    B=hz2
            // B pixel: G=cross4, R=diag4
            R = _mm_or_si128(_mm_and_si128(vert,  em),
                             _mm_and_si128(diag,  om));
            G = _mm_or_si128(_mm_and_si128(raw,   em),
                             _mm_and_si128(cross, om));
            B = _mm_or_si128(_mm_and_si128(horiz, em),
                             _mm_and_si128(raw,   om));
        } else if (isRGrow && !r0c0_R) {
            // BayerGR GR행: 짝수=G, 홀수=R
            // G pixel: B=vt2,    R=hz2
            // R pixel: G=cross4, B=diag4
            R = _mm_or_si128(_mm_and_si128(horiz, em),
                             _mm_and_si128(raw,   om));
            G = _mm_or_si128(_mm_and_si128(raw,   em),
                             _mm_and_si128(cross, om));
            B = _mm_or_si128(_mm_and_si128(vert,  em),
                             _mm_and_si128(diag,  om));
        } else {
            // BayerGR BG행: 짝수=B, 홀수=G
            // B pixel: G=cross4, R=diag4
            // G pixel: B=hz2,    R=vt2
            R = _mm_or_si128(_mm_and_si128(diag,  em),
                             _mm_and_si128(vert,  om));
            G = _mm_or_si128(_mm_and_si128(cross, em),
                             _mm_and_si128(raw,   om));
            B = _mm_or_si128(_mm_and_si128(raw,   em),
                             _mm_and_si128(horiz, om));
        }

        storeBGRA16_NT(dst + x * 4, R, G, B);
    }
}

// ── 경계 스칼라 ───────────────────────────────────────────────────────────────
static void scalarBGRA(
    const uint8_t* prev, const uint8_t* curr, const uint8_t* next,
    uint8_t* dst, int w, int xFrom, int xTo,
    bool isRGrow, bool r0c0_R)
{
    auto cl = [&](int x) -> int { return x < 0 ? 0 : x >= w ? w-1 : x; };
    auto Px = [&](const uint8_t* r, int x) -> int { return r[cl(x)]; };

    for (int x = xFrom; x < xTo; ++x) {
        const bool ec   = (x % 2 == 0);
        const bool rawR = r0c0_R ? (isRGrow && ec)   : (isRGrow && !ec);
        const bool rawB = r0c0_R ? (!isRGrow && !ec) : (!isRGrow && ec);
        uint8_t r, g, b;

        if (rawR) {
            r = curr[x];
            g = static_cast<uint8_t>(
                (Px(curr,x-1)+Px(curr,x+1)+Px(prev,x)+Px(next,x)) >> 2);
            b = static_cast<uint8_t>(
                (Px(prev,x-1)+Px(prev,x+1)+Px(next,x-1)+Px(next,x+1)) >> 2);
        } else if (rawB) {
            b = curr[x];
            g = static_cast<uint8_t>(
                (Px(curr,x-1)+Px(curr,x+1)+Px(prev,x)+Px(next,x)) >> 2);
            r = static_cast<uint8_t>(
                (Px(prev,x-1)+Px(prev,x+1)+Px(next,x-1)+Px(next,x+1)) >> 2);
        } else {
            g = curr[x];
            const bool gInR = (isRGrow == r0c0_R);
            if (gInR) {
                r = static_cast<uint8_t>((Px(curr,x-1)+Px(curr,x+1)) >> 1);
                b = static_cast<uint8_t>((Px(prev,x)+Px(next,x)) >> 1);
            } else {
                b = static_cast<uint8_t>((Px(curr,x-1)+Px(curr,x+1)) >> 1);
                r = static_cast<uint8_t>((Px(prev,x)+Px(next,x)) >> 1);
            }
        }
        dst[x*4+0] = b; dst[x*4+1] = g;
        dst[x*4+2] = r; dst[x*4+3] = 0xFF;
    }
}

// ── 공개: BGRA32 출력 ─────────────────────────────────────────────────────────
void debayerAVX2_BGRA(const uint8_t* __restrict src,
                      uint8_t*       __restrict dst,
                      int w, int h, uint32_t pfnc)
{
    if (!src || !dst || w < 32 || h < 4) return;

    bool r0c0_R = true, startOdd = false;
    switch (pfnc) {
    case PFNC_BayerRG8: r0c0_R=true;  startOdd=false; break;
    case PFNC_BayerGR8: r0c0_R=false; startOdd=false; break;
    case PFNC_BayerGB8: r0c0_R=true;  startOdd=true;  break;
    case PFNC_BayerBG8: r0c0_R=false; startOdd=true;  break;
    default: break;
    }

    const int xStart = 16;
    const int xEnd   = (w - 16) & ~15;

    // u16 행 버퍼 3개: thread_local → 재할당 없음
    // 2448px × 2bytes × 3 = ~14KB → L1 캐시에 상주
    static thread_local std::vector<uint16_t> bP, bC, bN;
    if (static_cast<int>(bP.size()) < w) {
        bP.resize(static_cast<size_t>(w));
        bC.resize(static_cast<size_t>(w));
        bN.resize(static_cast<size_t>(w));
    }

    // 초기 3행 u16 변환
    rowU8toU16(src,                                    bP.data(), w); // prev(y=0) = row0 클램프
    rowU8toU16(src,                                    bC.data(), w); // curr(y=0)
    rowU8toU16(src + static_cast<size_t>(1) * w,      bN.data(), w); // next(y=0) = row1

    uint16_t* rowP = bP.data();
    uint16_t* rowC = bC.data();
    uint16_t* rowN = bN.data();

    // ── 멀티스레드: 행 블록을 스레드별로 분할 ───────────────────────────
    // 2048행을 N스레드로 분할 → 이론적으로 N× 속도
    // N_THREADS: HW 스레드 수 기반, 최대 4 (메모리 대역폭 한계)
    const int N_THREADS = std::min(4, static_cast<int>(
        std::thread::hardware_concurrency()));

    // 단일 스레드 처리 함수 (행 범위 지정)
    auto processRows = [&](int yFrom, int yTo) {
        // 각 스레드 전용 u16 버퍼 (thread_local은 스레드별 분리됨)
        std::vector<uint16_t> tP(static_cast<size_t>(w));
        std::vector<uint16_t> tC(static_cast<size_t>(w));
        std::vector<uint16_t> tN(static_cast<size_t>(w));

        // 초기 3행 변환
        int y0 = yFrom;
        rowU8toU16(src + static_cast<size_t>(y0 > 0   ? y0-1 : 0)   * w, tP.data(), w);
        rowU8toU16(src + static_cast<size_t>(y0)                      * w, tC.data(), w);
        rowU8toU16(src + static_cast<size_t>(y0 < h-1 ? y0+1 : h-1) * w, tN.data(), w);

        uint16_t* rP = tP.data(), *rC = tC.data(), *rN = tN.data();

        for (int y = yFrom; y < yTo; ++y) {
            const uint8_t* srcP = src + static_cast<size_t>(y > 0   ? y-1 : 0)   * w;
            const uint8_t* srcC = src + static_cast<size_t>(y)                    * w;
            const uint8_t* srcN = src + static_cast<size_t>(y < h-1 ? y+1 : h-1) * w;
            uint8_t*       D    = dst + static_cast<size_t>(y) * w * 4;
            const bool isRG = ((y % 2 == 0) != startOdd);

            scalarBGRA(srcP, srcC, srcN, D, w, 0,    xStart, isRG, r0c0_R);
            scalarBGRA(srcP, srcC, srcN, D, w, xEnd, w,      isRG, r0c0_R);

            if (xEnd > xStart && y > 0 && y < h-1) {
                if (y + 2 < h)
                    _mm_prefetch(reinterpret_cast<const char*>(
                        src + static_cast<size_t>(y + 2) * w), _MM_HINT_T0);
                processRowAVX2(rP, rC, rN, D, xStart, xEnd, isRG, r0c0_R);
            } else if (xEnd > xStart) {
                scalarBGRA(srcP, srcC, srcN, D, w, xStart, xEnd, isRG, r0c0_R);
            }

            // 롤링
            uint16_t* tmp = rP; rP = rC; rC = rN; rN = tmp;
            if (y + 2 < h)
                rowU8toU16(src + static_cast<size_t>(y + 2) * w, rN, w);
        }
    };

    if (N_THREADS <= 1 || h < 64) {
        // 단일 스레드 (h가 작거나 스레드 생성 오버헤드가 큰 경우)
        processRows(0, h);
    } else {
        // 행 단위 블록 분할 (2 정렬: Bayer 짝/홀 행 쌍)
        std::vector<std::thread> threads;
        threads.reserve(static_cast<size_t>(N_THREADS));
        const int blockH = ((h / N_THREADS) & ~1);  // 2의 배수

        for (int t = 0; t < N_THREADS; ++t) {
            const int yFrom = t * blockH;
            const int yTo   = (t == N_THREADS - 1) ? h : yFrom + blockH;
            threads.emplace_back(processRows, yFrom, yTo);
        }
        for (auto& th : threads) th.join();
    }

    _mm_sfence();
}

// ── 공개: RGB888 (하위 호환) ──────────────────────────────────────────────────
void debayerAVX2(const uint8_t* __restrict src,
                 uint8_t*       __restrict dst,
                 int w, int h, uint32_t pfnc)
{
    if (!src || !dst || w < 4 || h < 4) return;
    static thread_local std::vector<uint8_t> tmp;
    tmp.resize(static_cast<size_t>(w) * h * 4);
    debayerAVX2_BGRA(src, tmp.data(), w, h, pfnc);
    const uint8_t* s = tmp.data();
    uint8_t*       d = dst;
    for (int i = 0, n = w*h; i < n; ++i, s+=4, d+=3) {
        d[0]=s[2]; d[1]=s[1]; d[2]=s[0];
    }
}
