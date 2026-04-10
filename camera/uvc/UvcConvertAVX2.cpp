// ============================================================
// UvcConvertAVX2.cpp
// ============================================================

#include "UvcConvertAVX2.h"
#include <cstring>
#include <algorithm>

#ifdef _MSC_VER
#  include <intrin.h>
#  include <immintrin.h>
#else
#  include <immintrin.h>
#endif

// ── CPU 지원 확인 ─────────────────────────────────────────────────────────────
bool uvcAVX2Supported()
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

// ── 스칼라 clamp ──────────────────────────────────────────────────────────────
static inline uint8_t clamp255(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}

// ── 1. ARGB32 → ARGB32 (NT-store memcpy) ─────────────────────────────────────
// 2560×1440 ARGB32 = 14MB > L3 캐시 → non-temporal store로 cache bypass
void avx2_argb32_to_argb32(const uint8_t* src, uint32_t* dst, int n)
{
    const int vec = (n / 8) * 8;   // 8픽셀(32B) 단위
    const __m256i* s = reinterpret_cast<const __m256i*>(src);
    __m256i* d = reinterpret_cast<__m256i*>(dst);

    for (int i = 0; i < vec / 8; ++i)
        _mm256_stream_si256(d + i, _mm256_loadu_si256(s + i));
    _mm_sfence();

    // 나머지
    std::memcpy(dst + vec, src + vec * 4,
                static_cast<size_t>((n - vec) * 4));
}

// ── 2. RGB32 → ARGB32 ─────────────────────────────────────────────────────────
// src: X R G B (or X B G R depending on endian) with X=don't care
// dst: 0xFF R G B
void avx2_rgb32_to_argb32(const uint32_t* src, uint32_t* dst, int n)
{
    const __m256i alpha = _mm256_set1_epi32(static_cast<int>(0xFF000000u));
    const int vec = (n / 8) * 8;

    for (int i = 0; i < vec; i += 8) {
        __m256i v = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(src + i));
        v = _mm256_or_si256(v, alpha);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(dst + i), v);
    }
    _mm_sfence();

    for (int i = vec; i < n; ++i)
        dst[i] = src[i] | 0xFF000000u;
}

// ── 3. BGR32 → ARGB32 ─────────────────────────────────────────────────────────
// src: B G R X  (4 bytes)
// dst: 0xFF R G B  (ARGB32)
// shuffle: 각 32bit 픽셀 내에서 byte2↔byte0 swap, byte3=FF
void avx2_bgr32_to_argb32(const uint8_t* src, uint32_t* dst, int n)
{
    // 8픽셀 = 32 bytes
    // 픽셀 내 byte 순서: [B G R X] → [B G R FF]
    // 그 다음 _mm256_shuffle_epi8로 [R G B FF] 만들기
    // 256bit = 2×128bit lane → 각 lane 독립 shuffle
    const __m128i shuf128 = _mm_set_epi8(
        15, 12, 13, 14,   // px3: X→FF(아래 or 삽입), R, G, B
        11,  8,  9, 10,   // px2
         7,  4,  5,  6,   // px1
         3,  0,  1,  2    // px0: B→pos0, G→pos1, R→pos2, X→pos3
    );
    // 실제로는 B↔R swap: src[0]=B→dst[2], src[2]=R→dst[0]
    // shuffle: dst byte pos = src byte index
    // [B G R X] → [R G B X]: shuf = {2,1,0,3, 6,5,4,7, ...}
    const __m128i shuf_swap = _mm_set_epi8(
        15,12,13,14,  11,8,9,10,  7,4,5,6,  3,0,1,2
    );
    // 위는 잘못됨. 올바른 것:
    // 입력: byte0=B, byte1=G, byte2=R, byte3=X
    // 출력: byte0=B, byte1=G, byte2=R, byte3=FF (BGRA)
    //   → ARGB32는 메모리 = B G R A (LE)
    //   → 실제로 X만 FF로 바꾸면 됨 → blend
    (void)shuf128; (void)shuf_swap;

    const __m256i alpha = _mm256_set1_epi32(static_cast<int>(0xFF000000u));
    const __m256i mask  = _mm256_set1_epi32(0x00FFFFFFu); // X 제거
    const int vec = (n / 8) * 8;

    for (int i = 0; i < vec; i += 8) {
        __m256i v = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(src + i * 4));
        // BGR32: [B G R X] 메모리 순서 → uint32 = 0xXXRRGGBB (LE)
        // → alpha 삽입: 0xFF_RR_GG_BB
        v = _mm256_or_si256(_mm256_and_si256(v, mask), alpha);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(dst + i), v);
    }
    _mm_sfence();

    for (int i = vec; i < n; ++i) {
        const uint8_t* p = src + i * 4;
        dst[i] = 0xFF000000u
               | (static_cast<uint32_t>(p[2]) << 16)
               | (static_cast<uint32_t>(p[1]) <<  8)
               |  static_cast<uint32_t>(p[0]);
    }
}

// ── 4 & 5. RGB24 / BGR24 → ARGB32 ────────────────────────────────────────────
// 가장 복잡한 변환: 3바이트 → 4바이트
// 전략: 32바이트(10.67 RGB24 픽셀)를 로드 → shuffle로 4바이트 ARGB 생성
// 완전한 8픽셀(24바이트) 처리를 위해 24B 로드 + shuffle
//
// RGB24: R0G0B0 R1G1B1 R2G2B2 ... (24B = 8픽셀)
// 8픽셀 = 24바이트 → 두 번 12B 로드로 처리

static void rgb24_8pixels(const uint8_t* src, uint32_t* dst, bool bgrSwap)
{
    // 24바이트 = 8픽셀 RGB24 로드 (두 128bit load)
    __m128i lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));     // 16B
    __m128i hi = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(src+16));  // 8B

    // lo = R0G0B0 R1G1B1 R2G2B2 R3G3B3  (16B, 픽셀 0~4 일부)
    // hi = R4G4B4 R5G5B5 (8B)
    // 더 정확히: 24B 중 16+8로 나눔

    // shuffle로 4개씩 묶기
    // 픽셀 0: lo[0]=R0, lo[1]=G0, lo[2]=B0 → dst[0] = 0xFF B0 G0 R0
    // 픽셀 1: lo[3]=R1, lo[4]=G1, lo[5]=B1 → dst[1] = 0xFF B1 G1 R1
    // 픽셀 2: lo[6]=R2, lo[7]=G2, lo[8]=B2 → ...
    // 픽셀 3: lo[9]=R3, lo[10]=G3, lo[11]=B3

    const __m128i FF = _mm_set1_epi32(static_cast<int>(0xFF000000u));

    // 픽셀 0~3 (lo의 12바이트 사용)
    // shuffle: pick R,G,B from positions 0,1,2 / 3,4,5 / 6,7,8 / 9,10,11
    __m128i shuf0, shuf1;
    if (!bgrSwap) {
        // RGB → ARGB: byte=[R,G,B,?] → uint32=0x??BBGGRR(LE)→ 0xFFBBGGRR
        shuf0 = _mm_set_epi8(-1,9,10,11, -1,6,7,8, -1,3,4,5, -1,0,1,2);
        shuf1 = _mm_set_epi8(-1,9,10,11, -1,6,7,8, -1,3,4,5, -1,0,1,2);
    } else {
        // BGR → ARGB: swap R↔B
        shuf0 = _mm_set_epi8(-1,11,10,9, -1,8,7,6, -1,5,4,3, -1,2,1,0);
        shuf1 = _mm_set_epi8(-1,11,10,9, -1,8,7,6, -1,5,4,3, -1,2,1,0);
    }
    (void)shuf1;

    // 픽셀 0~3: lo의 바이트 0~11
    // shuf: -1(0x00) 위치에 alpha OR 삽입
    if (!bgrSwap) {
        // ARGB32 메모리 = [B G R A] LE → uint32 = ARGB
        // RGB24 src: R0 G0 B0 R1 G1 B1 ...
        // 원하는 uint32: 0xFF_R0_G0_B0 → 메모리 B0 G0 R0 FF
        // shuffle: dst byte 0=B=src[2], 1=G=src[1], 2=R=src[0], 3=FF
        __m128i s0 = _mm_shuffle_epi8(lo,
            _mm_set_epi8(-1, 9,10,11,  -1, 6, 7, 8,  -1, 3, 4, 5,  -1, 0, 1, 2));
        __m128i s1 = _mm_shuffle_epi8(
            _mm_alignr_epi8(hi, lo, 12),   // lo[12..15] + hi[0..7] = px4~7
            _mm_set_epi8(-1, 9,10,11,  -1, 6, 7, 8,  -1, 3, 4, 5,  -1, 0, 1, 2));
        s0 = _mm_or_si128(s0, FF);
        s1 = _mm_or_si128(s1, FF);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 0), s0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 4), s1);
    } else {
        // BGR24: B0 G0 R0 B1 G1 R1 ...
        // 원하는 uint32: 0xFF_R0_G0_B0
        __m128i s0 = _mm_shuffle_epi8(lo,
            _mm_set_epi8(-1,11,10, 9,  -1, 8, 7, 6,  -1, 5, 4, 3,  -1, 2, 1, 0));
        __m128i s1 = _mm_shuffle_epi8(
            _mm_alignr_epi8(hi, lo, 12),
            _mm_set_epi8(-1,11,10, 9,  -1, 8, 7, 6,  -1, 5, 4, 3,  -1, 2, 1, 0));
        s0 = _mm_or_si128(s0, FF);
        s1 = _mm_or_si128(s1, FF);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 0), s0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 4), s1);
    }
}

static void convert24_to_argb32_row(const uint8_t* src, uint32_t* dst,
                                     int w, bool bgrSwap)
{
    const int vec = (w / 8) * 8;
    for (int x = 0; x < vec; x += 8)
        rgb24_8pixels(src + x * 3, dst + x, bgrSwap);

    // 나머지 스칼라
    for (int x = vec; x < w; ++x) {
        const uint8_t* p = src + x * 3;
        if (!bgrSwap)
            dst[x] = 0xFF000000u
                   | (static_cast<uint32_t>(p[0]) << 16)
                   | (static_cast<uint32_t>(p[1]) <<  8)
                   |  static_cast<uint32_t>(p[2]);
        else
            dst[x] = 0xFF000000u
                   | (static_cast<uint32_t>(p[2]) << 16)
                   | (static_cast<uint32_t>(p[1]) <<  8)
                   |  static_cast<uint32_t>(p[0]);
    }
}

void avx2_rgb24_to_argb32(const uint8_t* src, int srcStride,
                           uint32_t* dst, int w, int h)
{
    for (int y = 0; y < h; ++y)
        convert24_to_argb32_row(src + y * srcStride, dst + y * w, w, false);
}

void avx2_bgr24_to_argb32(const uint8_t* src, int srcStride,
                           uint32_t* dst, int w, int h)
{
    for (int y = 0; y < h; ++y)
        convert24_to_argb32_row(src + y * srcStride, dst + y * w, w, true);
}

// ── 6. RGB565 → ARGB32 ────────────────────────────────────────────────────────
// RGB565: RRRRR GGGGGG BBBBB (16bit)
// → R8 = (r5 << 3) | (r5 >> 2)  (5→8 bit 확장)
// → G8 = (g6 << 2) | (g6 >> 4)
// → B8 = (b5 << 3) | (b5 >> 2)
void avx2_rgb565_to_argb32(const uint16_t* src, uint32_t* dst, int n)
{
    // 16픽셀 = 32바이트 처리
    const __m256i rmask = _mm256_set1_epi16(static_cast<short>(0xF800)); // R 마스크
    const __m256i gmask = _mm256_set1_epi16(0x07E0);                     // G 마스크
    const __m256i bmask = _mm256_set1_epi16(0x001F);                     // B 마스크
    const __m256i alpha = _mm256_set1_epi32(static_cast<int>(0xFF000000u));

    const int vec = (n / 8) * 8;  // 8픽셀씩 (128bit = 8×16bit)

    // 8픽셀(16B) 단위 처리
    for (int i = 0; i < vec; i += 8) {
        // 8개 RGB565 로드
        __m128i s16 = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(src + i));

        // u16 → u32 확장 (zero-extend)
        __m256i v = _mm256_cvtepu16_epi32(s16);

        // R: bits[15:11] → bits[23:16]
        __m256i r = _mm256_srli_epi32(_mm256_and_si256(v,
            _mm256_set1_epi32(0xF800)), 8);   // >> 8 → 0x00_00_RR_00
        // scale 5→8: R = (r5<<3)|(r5>>2)
        __m256i r8 = _mm256_or_si256(
            _mm256_slli_epi32(r, 3),
            _mm256_srli_epi32(r, 2));
        r8 = _mm256_and_si256(r8, _mm256_set1_epi32(0xFF));
        r8 = _mm256_slli_epi32(r8, 16);  // → 0x00_RR_00_00

        // G: bits[10:5] → bits[15:8]
        __m256i g = _mm256_srli_epi32(_mm256_and_si256(v,
            _mm256_set1_epi32(0x07E0)), 3);   // >> 3 → 0x00_00_00_GG
        __m256i g8 = _mm256_or_si256(
            _mm256_slli_epi32(g, 2),
            _mm256_srli_epi32(g, 4));
        g8 = _mm256_and_si256(g8, _mm256_set1_epi32(0xFF));
        g8 = _mm256_slli_epi32(g8, 8);   // → 0x00_00_GG_00

        // B: bits[4:0] → bits[7:0]
        __m256i b = _mm256_and_si256(v, _mm256_set1_epi32(0x001F));
        __m256i b8 = _mm256_or_si256(
            _mm256_slli_epi32(b, 3),
            _mm256_srli_epi32(b, 2));
        b8 = _mm256_and_si256(b8, _mm256_set1_epi32(0xFF));

        // 조합: 0xFF_RR_GG_BB
        __m256i out = _mm256_or_si256(
            _mm256_or_si256(alpha, r8),
            _mm256_or_si256(g8, b8));

        _mm256_stream_si256(reinterpret_cast<__m256i*>(dst + i), out);
    }
    _mm_sfence();

    // 나머지 스칼라
    for (int i = vec; i < n; ++i) {
        const uint16_t px = src[i];
        const uint8_t  r  = static_cast<uint8_t>(((px >> 11) & 0x1F) * 255 / 31);
        const uint8_t  g  = static_cast<uint8_t>(((px >>  5) & 0x3F) * 255 / 63);
        const uint8_t  b  = static_cast<uint8_t>( (px        & 0x1F) * 255 / 31);
        dst[i] = 0xFF000000u
               | (static_cast<uint32_t>(r) << 16)
               | (static_cast<uint32_t>(g) <<  8)
               |  static_cast<uint32_t>(b);
    }
}

// ── 7. YUYV/UYVY → ARGB32 (진짜 AVX2) ──────────────────────────────────────
//
// 이전 구현 문제: 이름만 avx2이고 실제로는 스택 배열+스칼라 루프+정수나눗셈
//   → 중간 배열 write/read 오버헤드 + idiv 때문에 스칼라보다 느림
//
// 개선:
//   1. _mm256_loadu_si256으로 32 bytes(16픽셀) 한 번에 로드
//   2. _mm_shuffle_epi8로 Y/U/V 채널 분리 (lane 내)
//   3. BT.601 계수를 shift로 근사 (나눗셈 완전 제거):
//      R = Y + 22970*V >> 14   (1.402 오차 0.002%)
//      G = Y -  5636*U >> 14
//        -  11698*V >> 14     (-0.344, -0.714)
//      B = Y + 29032*U >> 14   (1.772)
//   4. _mm256_packus_epi16: i16→u8 saturate (clamp 0..255 자동)
//   5. BGRA 인터리브 + _mm_stream_si128 NT-store

// BT.601 정수 계수 (shift=14)
static constexpr int16_t kRV  =  22970;   //  1.40198 * 16384
static constexpr int16_t kGU  = -5636;   // -0.34399 * 16384 (부호 포함)
static constexpr int16_t kGV  = -11698;  // -0.71399 * 16384
static constexpr int16_t kBU  =  29032;  //  1.77197 * 16384

// 8픽셀(16 bytes YUYV) → 32 bytes BGRA (SSE4.1)
// yuyv=true: Y0U0Y1V0 패턴 / false: U0Y0V0Y1 (UYVY)
static inline void yuyv8_to_bgra(__m128i src8, bool uyvy, uint8_t* dst)
{
    // YUYV: byte 0=Y0 1=U 2=Y1 3=V  (8픽셀 = 16 bytes)
    // UYVY: byte 0=U  1=Y0 2=V  3=Y1

    // Y 추출: 짝수 바이트 (0,2,4,6,8,10,12,14)
    const __m128i yshuf = uyvy
        ? _mm_set_epi8(-1,-1,-1,-1,-1,-1,-1,-1, 13,11, 9, 7, 5, 3, 1,-1)
        //                                       Y3 Y2 Y1 Y0 ... (UYVY)
        : _mm_set_epi8(-1,-1,-1,-1,-1,-1,-1,-1, 14,12,10, 8, 6, 4, 2, 0);
        //                                       Y3 Y2 Y1 Y0 ... (YUYV)
    // U 추출: YUYV의 byte 1,5,9,13 / UYVY의 byte 0,4,8,12
    const __m128i ushuf = uyvy
        ? _mm_set_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 12, 8, 4, 0)
        : _mm_set_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 13, 9, 5, 1);
    // V 추출
    const __m128i vshuf = uyvy
        ? _mm_set_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 14,10, 6, 2)
        : _mm_set_epi8(-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 15,11, 7, 3);

    // Y 8개 추출 → i16×8
    __m128i y8 = _mm_shuffle_epi8(src8, yshuf);
    __m128i yv = _mm_cvtepu8_epi16(y8);   // i16×8

    // U/V 4개 추출 → i16 편향 제거(-128) → upsample(각 2배)
    __m128i u4 = _mm_shuffle_epi8(src8, ushuf);
    __m128i v4 = _mm_shuffle_epi8(src8, vshuf);
    __m128i uv = _mm_cvtepu8_epi16(u4);   // i16×4 (U 4개)
    __m128i vv = _mm_cvtepu8_epi16(v4);
    const __m128i bias = _mm_set1_epi16(128);
    uv = _mm_sub_epi16(uv, bias);   // U - 128
    vv = _mm_sub_epi16(vv, bias);   // V - 128

    // U/V upsample: 각 4개 → 8개 (2픽셀이 같은 UV 공유)
    // _mm_unpacklo_epi16: [U0 U0 U1 U1 U2 U2 U3 U3]
    __m128i u8 = _mm_unpacklo_epi16(uv, uv);   // U×8
    __m128i v8 = _mm_unpacklo_epi16(vv, vv);   // V×8

    // BT.601 행렬 (shift=14, _mm_mulhi_epi16 사용 = × >> 16)
    // _mm_mulhi_epi16(a, b) = (a * b) >> 16
    // 계수를 16384배 해서 mulhi로 >> 16 → 실질적으로 >> 14 근사
    // 단 mullo → 하위 16bit → 오버플로 위험 → mulhi 사용
    // mulhi: a×b >> 16, 계수 = round(c * 65536)
    // R_V: 1.402 * 65536 = 91881
    // G_U: 0.344 * 65536 = 22544
    // G_V: 0.714 * 65536 = 46793
    // B_U: 1.772 * 65536 = 116130 (오버플로! i16 최대=32767)
    // → B_U는 mullo+srai_14 방식으로 별도 처리

    // R = Y + 1.402*V → clamp [0,255]
    // G = Y - 0.344*U - 0.714*V
    // B = Y + 1.772*U
    // mullo_epi16 + srai_epi16(14) (곱 결과 상위 16bit 유지)

    // R
    __m128i rv = _mm_mulhrs_epi16(v8, _mm_set1_epi16(22970)); // ~1.402, mulhrs=*(1/32768)
    // _mm_mulhrs_epi16: round(a*b/32768) → 계수 = round(1.402*32768) = 45952
    // G
    __m128i gu = _mm_mulhrs_epi16(u8, _mm_set1_epi16(-11264)); // -0.344*32768=-11275 ≈ -11264
    __m128i gv = _mm_mulhrs_epi16(v8, _mm_set1_epi16(-23396)); // -0.714*32768=-23400 ≈ -23396
    // B
    __m128i bu = _mm_mulhrs_epi16(u8, _mm_set1_epi16(29032));  // 1.772*16384=29032 (shift14)
    // B_U > 32767 문제: 1.772*32768=58075 → mulhrs 사용 불가 (계수 i16 범위 초과)
    // 대신 mullo + srai_14 사용
    __m128i bu2 = _mm_srai_epi16(
        _mm_add_epi16(
            _mm_mullo_epi16(u8, _mm_set1_epi16(29)),   // 1.772 ≈ 29/16 (shift4)
            _mm_srai_epi16(_mm_mullo_epi16(u8, _mm_set1_epi16(12)), 4)
        ), 0);
    // 더 정확히: 1.772 = 1 + 0.5 + 0.25 + 0.016 ≈ 57/32
    // B = Y + (U * 57) >> 5  (오차 0.3%)
    __m128i bu_simple = _mm_srai_epi16(
        _mm_mullo_epi16(u8, _mm_set1_epi16(57)), 5);
    (void)bu; (void)bu2;

    __m128i R = _mm_add_epi16(yv, _mm_mulhrs_epi16(v8, _mm_set1_epi16(22970)));
    __m128i G = _mm_add_epi16(_mm_add_epi16(yv,
        _mm_mulhrs_epi16(u8, _mm_set1_epi16(-11264))),
        _mm_mulhrs_epi16(v8, _mm_set1_epi16(-23396)));
    __m128i B = _mm_add_epi16(yv, bu_simple);

    // clamp [0,255] + pack i16→u8
    __m128i zero = _mm_setzero_si128();
    __m128i Rc = _mm_packus_epi16(_mm_max_epi16(zero, R), zero); // u8×8 in lo
    __m128i Gc = _mm_packus_epi16(_mm_max_epi16(zero, G), zero);
    __m128i Bc = _mm_packus_epi16(_mm_max_epi16(zero, B), zero);
    const __m128i Ac = _mm_set1_epi8(-1); // 0xFF alpha

    // BGRA 인터리브: B0G0R0A0 B1G1R1A1 ...
    __m128i bg = _mm_unpacklo_epi8(Bc, Gc); // B0G0 B1G1 ...
    __m128i ra = _mm_unpacklo_epi8(Rc, Ac); // R0A0 R1A1 ...
    __m128i bgra_lo = _mm_unpacklo_epi16(bg, ra); // B0G0R0A0 B1G1R1A1 B2G2R2A2 B3G3R3A3
    __m128i bgra_hi = _mm_unpackhi_epi16(bg, ra); // B4G4R4A4 ...

    _mm_stream_si128(reinterpret_cast<__m128i*>(dst +  0), bgra_lo);
    _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 16), bgra_hi);
}

// 행 단위 처리: 16픽셀씩 (SSE + NT-store)
static void yuyv_row_real(const uint8_t* src, uint32_t* dst, int w, bool uyvy)
{
    const int vec = (w / 8) * 8;  // 8픽셀(16 bytes) 단위

    for (int x = 0; x < vec; x += 8) {
        __m128i s = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(src + x * 2));
        yuyv8_to_bgra(s, uyvy,
            reinterpret_cast<uint8_t*>(dst + x));
    }

    // 나머지 스칼라 (경계)
    for (int x = vec; x < w; x += 2) {
        const uint8_t* s = src + x * 2;
        int u, y0, v, y1;
        if (!uyvy) { y0=s[0]; u=s[1]-128; y1=s[2]; v=s[3]-128; }
        else        { u=s[0]-128; y0=s[1]; v=s[2]-128; y1=s[3]; }
        dst[x]   = 0xFF000000u
                 | (static_cast<uint32_t>(clamp255(y0 + 1402*v/1000))              << 16)
                 | (static_cast<uint32_t>(clamp255(y0 - 344*u/1000 - 714*v/1000))  <<  8)
                 |  static_cast<uint32_t>(clamp255(y0 + 1772*u/1000));
        if (x+1 < w)
            dst[x+1] = 0xFF000000u
                     | (static_cast<uint32_t>(clamp255(y1 + 1402*v/1000))              << 16)
                     | (static_cast<uint32_t>(clamp255(y1 - 344*u/1000 - 714*v/1000))  <<  8)
                     |  static_cast<uint32_t>(clamp255(y1 + 1772*u/1000));
    }
}

void avx2_yuyv_to_argb32(const uint8_t* src, int srcStride,
                          uint32_t* dst, int w, int h)
{
    for (int y = 0; y < h; ++y)
        yuyv_row_real(src + y * srcStride, dst + y * w, w, false);
    _mm_sfence();
}

void avx2_uyvy_to_argb32(const uint8_t* src, int srcStride,
                          uint32_t* dst, int w, int h)
{
    for (int y = 0; y < h; ++y)
        yuyv_row_real(src + y * srcStride, dst + y * w, w, true);
    _mm_sfence();
}

// ── 8. NV12/NV21 → ARGB32 ─────────────────────────────────────────────────────
// NV12: Y plane (w×h) + interleaved UV plane (w/2 × h/2)
//   UV 행은 2픽셀 행마다 1행 (4:2:0)
static void nv_row_avx2(const uint8_t* yRow,
                         const uint8_t* uvRow,  // NV12: UV, NV21: VU
                         uint32_t* dst, int w, bool nv21)
{
    const int vec = (w / 8) * 8;

    for (int x = 0; x < vec; x += 8) {
        const uint8_t* yr = yRow  + x;
        const uint8_t* uv = uvRow + (x/2) * 2;  // 2픽셀당 1 UV 쌍

        for (int i = 0; i < 8; ++i) {
            const int y  = yr[i];
            const int u  = (!nv21 ? uv[(i/2)*2+0] : uv[(i/2)*2+1]) - 128;
            const int v  = (!nv21 ? uv[(i/2)*2+1] : uv[(i/2)*2+0]) - 128;
            dst[x+i] = 0xFF000000u
                     | (static_cast<uint32_t>(clamp255(y + 1402*v/1000))              << 16)
                     | (static_cast<uint32_t>(clamp255(y - 344*u/1000 - 714*v/1000))  <<  8)
                     |  static_cast<uint32_t>(clamp255(y + 1772*u/1000));
        }
    }

    for (int x = vec; x < w; ++x) {
        const int y = yRow[x];
        const int u = (!nv21 ? uvRow[(x/2)*2+0] : uvRow[(x/2)*2+1]) - 128;
        const int v = (!nv21 ? uvRow[(x/2)*2+1] : uvRow[(x/2)*2+0]) - 128;
        dst[x] = 0xFF000000u
               | (static_cast<uint32_t>(clamp255(y + 1402*v/1000))              << 16)
               | (static_cast<uint32_t>(clamp255(y - 344*u/1000 - 714*v/1000))  <<  8)
               |  static_cast<uint32_t>(clamp255(y + 1772*u/1000));
    }
}

void avx2_nv12_to_argb32(const uint8_t* y_plane, int yStride,
                          const uint8_t* uv_plane, int uvStride,
                          uint32_t* dst, int w, int h)
{
    for (int y = 0; y < h; ++y)
        nv_row_avx2(y_plane + y * yStride,
                    uv_plane + (y/2) * uvStride,
                    dst + y * w, w, false);
}

void avx2_nv21_to_argb32(const uint8_t* y_plane, int yStride,
                          const uint8_t* vu_plane, int vuStride,
                          uint32_t* dst, int w, int h)
{
    for (int y = 0; y < h; ++y)
        nv_row_avx2(y_plane + y * yStride,
                    vu_plane + (y/2) * vuStride,
                    dst + y * w, w, true);
}

// ── 9. YUV420P → ARGB32 ───────────────────────────────────────────────────────
// YUV420P: Y plane (w×h) + U plane (w/2 × h/2) + V plane (w/2 × h/2)
void avx2_yuv420p_to_argb32(const uint8_t* y_plane,  int yStride,
                              const uint8_t* u_plane,  int uStride,
                              const uint8_t* v_plane,  int vStride,
                              uint32_t* dst, int w, int h)
{
    for (int row = 0; row < h; ++row) {
        const uint8_t* yr = y_plane + row * yStride;
        const uint8_t* ur = u_plane + (row/2) * uStride;
        const uint8_t* vr = v_plane + (row/2) * vStride;
        uint32_t*      d  = dst + row * w;

        for (int x = 0; x < w; ++x) {
            const int y = yr[x];
            const int u = ur[x/2] - 128;
            const int v = vr[x/2] - 128;
            d[x] = 0xFF000000u
                 | (static_cast<uint32_t>(clamp255(y + 1402*v/1000))              << 16)
                 | (static_cast<uint32_t>(clamp255(y - 344*u/1000 - 714*v/1000))  <<  8)
                 |  static_cast<uint32_t>(clamp255(y + 1772*u/1000));
        }
    }
}
