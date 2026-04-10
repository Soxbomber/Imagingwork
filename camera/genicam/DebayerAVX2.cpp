// ============================================================
// DebayerAVX2.cpp  — 5MP 최적화 버전
//
// 핵심 개선 사항 (vs 이전 구현):
//  1. scalarRow를 경계 픽셀만 처리 (좌16 + 우16, 상하1행)
//     → 이전: 매 행 전체(2448픽셀) 스칼라
//     → 개선: 경계만(32픽셀) 스칼라  (98% 스칼라 제거)
//  2. Non-temporal store (_mm_stream_si128)
//     → 5MP BGRA = 20MB > L3 캐시 → cache pollution 방지
//  3. QImage 버퍼 재사용 (convertPixelsBGRA 호출측에서 관리)
//     → 매 프레임 20MB malloc/free 제거
//  4. 4픽셀 unroll 대신 완전 벡터 인터리브 (BGRA 64B 연속 저장)
// ============================================================

#include "DebayerAVX2.h"
#include "ArvU3vProtocol.h"
#include <cstring>
#include <algorithm>

#ifdef _MSC_VER
#  include <intrin.h>
#  include <immintrin.h>
#else
#  include <immintrin.h>
#endif

// ── CPU 지원 확인 ─────────────────────────────────────────────────────────────
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

// ── 유틸 ─────────────────────────────────────────────────────────────────────
static inline __m256i load16u(const uint8_t* p) {
    return _mm256_cvtepu8_epi16(
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(p)));
}

static inline __m128i pack16(const __m256i& v) {
    __m256i p = _mm256_packus_epi16(v, _mm256_setzero_si256());
    p = _mm256_permute4x64_epi64(p, 0b00001000);
    return _mm256_castsi256_si128(p);
}

static inline __m128i evenMask() {
    return _mm_set_epi8(0,-1, 0,-1, 0,-1, 0,-1,
                        0,-1, 0,-1, 0,-1, 0,-1);
}

// ── BGRA 16픽셀 non-temporal store (64 bytes) ─────────────────────────────────
// Non-temporal = write-combining, cache bypass → 5MP에서 cache pollution 방지
static inline void storeBGRA16_NT(
    uint8_t* dst, __m128i rv, __m128i gv, __m128i bv)
{
    const __m128i alpha = _mm_set1_epi8(-1); // 0xFF

    // B G R A 인터리브: 4픽셀씩 4회
    __m128i bg_lo = _mm_unpacklo_epi8(bv, gv);   // B0G0 B1G1 B2G2 B3G3...
    __m128i bg_hi = _mm_unpackhi_epi8(bv, gv);
    __m128i ra_lo = _mm_unpacklo_epi8(rv, alpha);
    __m128i ra_hi = _mm_unpackhi_epi8(rv, alpha);

    __m128i bgra0 = _mm_unpacklo_epi16(bg_lo, ra_lo); // B0G0R0A0 B1G1R1A1...
    __m128i bgra1 = _mm_unpackhi_epi16(bg_lo, ra_lo);
    __m128i bgra2 = _mm_unpacklo_epi16(bg_hi, ra_hi);
    __m128i bgra3 = _mm_unpackhi_epi16(bg_hi, ra_hi);

    // non-temporal store: 캐시 우회 (5MP > L3)
    _mm_stream_si128(reinterpret_cast<__m128i*>(dst +  0), bgra0);
    _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 16), bgra1);
    _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 32), bgra2);
    _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 48), bgra3);
}

// ── 경계 픽셀 전용 스칼라 처리 ───────────────────────────────────────────────
// x 범위만 처리 (전체 행이 아닌 지정 구간만)
static void scalarBGRA_range(
    const uint8_t* prev, const uint8_t* curr, const uint8_t* next,
    uint8_t* dst, int w, int xFrom, int xTo,
    bool isRGrow, bool r0c0_R)
{
    auto cl = [&](int x) -> int { return x < 0 ? 0 : x >= w ? w-1 : x; };
    auto P  = [&](const uint8_t* r, int x) -> int { return r[cl(x)]; };

    for (int x = xFrom; x < xTo; ++x) {
        const bool ec = (x % 2 == 0);
        bool rawR = r0c0_R ? (isRGrow && ec)   : (isRGrow && !ec);
        bool rawB = r0c0_R ? (!isRGrow && !ec) : (!isRGrow && ec);
        uint8_t r, g, b;

        if (rawR) {
            r = static_cast<uint8_t>(curr[x]);
            g = static_cast<uint8_t>(
                (P(curr,x-1)+P(curr,x+1)+P(prev,x)+P(next,x)) >> 2);
            b = static_cast<uint8_t>(
                (P(prev,x-1)+P(prev,x+1)+P(next,x-1)+P(next,x+1)) >> 2);
        } else if (rawB) {
            b = static_cast<uint8_t>(curr[x]);
            g = static_cast<uint8_t>(
                (P(curr,x-1)+P(curr,x+1)+P(prev,x)+P(next,x)) >> 2);
            r = static_cast<uint8_t>(
                (P(prev,x-1)+P(prev,x+1)+P(next,x-1)+P(next,x+1)) >> 2);
        } else {
            g = static_cast<uint8_t>(curr[x]);
            bool gInRrow = (isRGrow == r0c0_R);
            if (gInRrow) {
                r = static_cast<uint8_t>((P(curr,x-1)+P(curr,x+1)) >> 1);
                b = static_cast<uint8_t>((P(prev,x)  +P(next,x)  ) >> 1);
            } else {
                b = static_cast<uint8_t>((P(curr,x-1)+P(curr,x+1)) >> 1);
                r = static_cast<uint8_t>((P(prev,x)  +P(next,x)  ) >> 1);
            }
        }
        dst[x*4+0] = b;
        dst[x*4+1] = g;
        dst[x*4+2] = r;
        dst[x*4+3] = 0xFF;
    }
}

// ── AVX2 RG-row → BGRA (non-temporal store) ──────────────────────────────────
static void processRGrowBGRA(
    const uint8_t* prev, const uint8_t* curr, const uint8_t* next,
    uint8_t* dst, int x0, int n, bool r0c0_R)
{
    const __m128i em = evenMask();
    const __m128i om = _mm_xor_si128(em, _mm_set1_epi8(-1));

    for (int x = x0; x < x0 + n; x += 16) {
        __m256i C  = load16u(curr+x), CL = load16u(curr+x-1), CR = load16u(curr+x+1);
        __m256i U  = load16u(prev+x), D  = load16u(next+x);
        __m256i UL = load16u(prev+x-1), UR = load16u(prev+x+1);
        __m256i DL = load16u(next+x-1), DR = load16u(next+x+1);

        __m256i hz  = _mm256_add_epi16(CL, CR);
        __m256i vt  = _mm256_add_epi16(U,  D);
        __m256i cr4 = _mm256_srli_epi16(_mm256_add_epi16(hz, vt), 2);
        __m256i dg4 = _mm256_srli_epi16(
            _mm256_add_epi16(_mm256_add_epi16(UL,UR),_mm256_add_epi16(DL,DR)), 2);
        __m256i hz2 = _mm256_srli_epi16(hz, 1);
        __m256i vt2 = _mm256_srli_epi16(vt, 1);

        __m128i raw=pack16(C), gfR=pack16(cr4), bfR=pack16(dg4);
        __m128i rFG=pack16(hz2), bFG=pack16(vt2);

        __m128i R, G, B;
        if (r0c0_R) {
            R = _mm_or_si128(_mm_and_si128(raw,em), _mm_and_si128(rFG,om));
            G = _mm_or_si128(_mm_and_si128(gfR,em), _mm_and_si128(raw,om));
            B = _mm_or_si128(_mm_and_si128(bfR,em), _mm_and_si128(bFG,om));
        } else {
            R = _mm_or_si128(_mm_and_si128(rFG,em), _mm_and_si128(raw,om));
            G = _mm_or_si128(_mm_and_si128(raw,em), _mm_and_si128(gfR,om));
            B = _mm_or_si128(_mm_and_si128(bFG,em), _mm_and_si128(bfR,om));
        }
        storeBGRA16_NT(dst + x*4, R, G, B);
    }
}

// ── AVX2 GB-row → BGRA (non-temporal store) ──────────────────────────────────
static void processGBrowBGRA(
    const uint8_t* prev, const uint8_t* curr, const uint8_t* next,
    uint8_t* dst, int x0, int n, bool r0c0_R)
{
    const __m128i em = evenMask();
    const __m128i om = _mm_xor_si128(em, _mm_set1_epi8(-1));

    for (int x = x0; x < x0 + n; x += 16) {
        __m256i C  = load16u(curr+x), CL = load16u(curr+x-1), CR = load16u(curr+x+1);
        __m256i U  = load16u(prev+x), D  = load16u(next+x);
        __m256i UL = load16u(prev+x-1), UR = load16u(prev+x+1);
        __m256i DL = load16u(next+x-1), DR = load16u(next+x+1);

        __m256i hz  = _mm256_add_epi16(CL, CR);
        __m256i vt  = _mm256_add_epi16(U,  D);
        __m256i cr4 = _mm256_srli_epi16(_mm256_add_epi16(hz, vt), 2);
        __m256i dg4 = _mm256_srli_epi16(
            _mm256_add_epi16(_mm256_add_epi16(UL,UR),_mm256_add_epi16(DL,DR)), 2);
        __m256i bFG = _mm256_srli_epi16(hz, 1);
        __m256i rFG = _mm256_srli_epi16(vt, 1);

        __m128i raw=pack16(C), gfB=pack16(cr4), rfB=pack16(dg4);
        __m128i bfG=pack16(bFG), rfG=pack16(rFG);

        __m128i R, G, B;
        if (r0c0_R) {
            R = _mm_or_si128(_mm_and_si128(rfG,em), _mm_and_si128(rfB,om));
            G = _mm_or_si128(_mm_and_si128(raw,em), _mm_and_si128(gfB,om));
            B = _mm_or_si128(_mm_and_si128(bfG,em), _mm_and_si128(raw,om));
        } else {
            R = _mm_or_si128(_mm_and_si128(rfB,em), _mm_and_si128(rfG,om));
            G = _mm_or_si128(_mm_and_si128(gfB,em), _mm_and_si128(raw,om));
            B = _mm_or_si128(_mm_and_si128(raw,em), _mm_and_si128(bfG,om));
        }
        storeBGRA16_NT(dst + x*4, R, G, B);
    }
}

// ── RGB888 출력용 (참조 유지) ─────────────────────────────────────────────────
static void storeRGB16(uint8_t* dst, __m128i rv, __m128i gv, __m128i bv)
{
    alignas(16) uint8_t R[16], G[16], B[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(R), rv);
    _mm_store_si128(reinterpret_cast<__m128i*>(G), gv);
    _mm_store_si128(reinterpret_cast<__m128i*>(B), bv);
#define W4(base, off) \
    dst[(base)+ 0]=R[(off)]; dst[(base)+ 1]=G[(off)]; dst[(base)+ 2]=B[(off)]; \
    dst[(base)+ 3]=R[(off)+1];dst[(base)+ 4]=G[(off)+1];dst[(base)+ 5]=B[(off)+1]; \
    dst[(base)+ 6]=R[(off)+2];dst[(base)+ 7]=G[(off)+2];dst[(base)+ 8]=B[(off)+2]; \
    dst[(base)+ 9]=R[(off)+3];dst[(base)+10]=G[(off)+3];dst[(base)+11]=B[(off)+3];
    W4( 0, 0) W4(12, 4) W4(24, 8) W4(36, 12)
#undef W4
}

static void processRGrow(
    const uint8_t* prev, const uint8_t* curr, const uint8_t* next,
    uint8_t* dst, int x0, int n, bool r0c0_R)
{
    const __m128i em = evenMask();
    const __m128i om = _mm_xor_si128(em, _mm_set1_epi8(-1));
    for (int x = x0; x < x0 + n; x += 16) {
        __m256i C=load16u(curr+x),CL=load16u(curr+x-1),CR=load16u(curr+x+1);
        __m256i U=load16u(prev+x),D=load16u(next+x);
        __m256i UL=load16u(prev+x-1),UR=load16u(prev+x+1);
        __m256i DL=load16u(next+x-1),DR=load16u(next+x+1);
        __m256i hz=_mm256_add_epi16(CL,CR),vt=_mm256_add_epi16(U,D);
        __m256i cr4=_mm256_srli_epi16(_mm256_add_epi16(hz,vt),2);
        __m256i dg4=_mm256_srli_epi16(_mm256_add_epi16(_mm256_add_epi16(UL,UR),_mm256_add_epi16(DL,DR)),2);
        __m256i hz2=_mm256_srli_epi16(hz,1),vt2=_mm256_srli_epi16(vt,1);
        __m128i raw=pack16(C),gfR=pack16(cr4),bfR=pack16(dg4),rFG=pack16(hz2),bFG=pack16(vt2);
        __m128i R,G,B;
        if(r0c0_R){R=_mm_or_si128(_mm_and_si128(raw,em),_mm_and_si128(rFG,om));G=_mm_or_si128(_mm_and_si128(gfR,em),_mm_and_si128(raw,om));B=_mm_or_si128(_mm_and_si128(bfR,em),_mm_and_si128(bFG,om));}
        else{R=_mm_or_si128(_mm_and_si128(rFG,em),_mm_and_si128(raw,om));G=_mm_or_si128(_mm_and_si128(raw,em),_mm_and_si128(gfR,om));B=_mm_or_si128(_mm_and_si128(bFG,em),_mm_and_si128(bfR,om));}
        storeRGB16(dst+x*3,R,G,B);
    }
}

static void scalarRow_rgb(
    const uint8_t* prev, const uint8_t* curr, const uint8_t* next,
    uint8_t* dst, int w, int xFrom, int xTo, bool isRGrow, bool r0c0_R)
{
    auto cl=[&](int x)->int{return x<0?0:x>=w?w-1:x;};
    auto P=[&](const uint8_t* r,int x)->int{return r[cl(x)];};
    for(int x=xFrom;x<xTo;++x){
        const bool ec=(x%2==0);
        bool rawR=r0c0_R?(isRGrow&&ec):(isRGrow&&!ec);
        bool rawB=r0c0_R?(!isRGrow&&!ec):(!isRGrow&&ec);
        uint8_t r,g,b;
        if(rawR){r=curr[x];g=(P(curr,x-1)+P(curr,x+1)+P(prev,x)+P(next,x))>>2;b=(P(prev,x-1)+P(prev,x+1)+P(next,x-1)+P(next,x+1))>>2;}
        else if(rawB){b=curr[x];g=(P(curr,x-1)+P(curr,x+1)+P(prev,x)+P(next,x))>>2;r=(P(prev,x-1)+P(prev,x+1)+P(next,x-1)+P(next,x+1))>>2;}
        else{g=curr[x];bool gInR=(isRGrow==r0c0_R);if(gInR){r=(P(curr,x-1)+P(curr,x+1))>>1;b=(P(prev,x)+P(next,x))>>1;}else{b=(P(curr,x-1)+P(curr,x+1))>>1;r=(P(prev,x)+P(next,x))>>1;}}
        dst[x*3]=r;dst[x*3+1]=g;dst[x*3+2]=b;
    }
}

static void processGBrow(
    const uint8_t* prev, const uint8_t* curr, const uint8_t* next,
    uint8_t* dst, int x0, int n, bool r0c0_R)
{
    const __m128i em=evenMask(),om=_mm_xor_si128(em,_mm_set1_epi8(-1));
    for(int x=x0;x<x0+n;x+=16){
        __m256i C=load16u(curr+x),CL=load16u(curr+x-1),CR=load16u(curr+x+1);
        __m256i U=load16u(prev+x),D=load16u(next+x);
        __m256i UL=load16u(prev+x-1),UR=load16u(prev+x+1);
        __m256i DL=load16u(next+x-1),DR=load16u(next+x+1);
        __m256i hz=_mm256_add_epi16(CL,CR),vt=_mm256_add_epi16(U,D);
        __m256i cr4=_mm256_srli_epi16(_mm256_add_epi16(hz,vt),2);
        __m256i dg4=_mm256_srli_epi16(_mm256_add_epi16(_mm256_add_epi16(UL,UR),_mm256_add_epi16(DL,DR)),2);
        __m256i bFG=_mm256_srli_epi16(hz,1),rFG=_mm256_srli_epi16(vt,1);
        __m128i raw=pack16(C),gfB=pack16(cr4),rfB=pack16(dg4),bfG=pack16(bFG),rfG=pack16(rFG);
        __m128i R,G,B;
        if(r0c0_R){R=_mm_or_si128(_mm_and_si128(rfG,em),_mm_and_si128(rfB,om));G=_mm_or_si128(_mm_and_si128(raw,em),_mm_and_si128(gfB,om));B=_mm_or_si128(_mm_and_si128(bfG,em),_mm_and_si128(raw,om));}
        else{R=_mm_or_si128(_mm_and_si128(rfB,em),_mm_and_si128(rfG,om));G=_mm_or_si128(_mm_and_si128(gfB,em),_mm_and_si128(raw,om));B=_mm_or_si128(_mm_and_si128(raw,em),_mm_and_si128(bfG,om));}
        storeRGB16(dst+x*3,R,G,B);
    }
}

// ── 공개 인터페이스: RGB888 출력 ─────────────────────────────────────────────
void debayerAVX2(const uint8_t* __restrict src,
                 uint8_t*       __restrict dst,
                 int w, int h, uint32_t pfnc)
{
    if (!src || !dst || w < 32 || h < 4) return;
    bool r0c0_R=true, startOdd=false;
    switch(pfnc){
    case PFNC_BayerRG8: r0c0_R=true;  startOdd=false; break;
    case PFNC_BayerGR8: r0c0_R=false; startOdd=false; break;
    case PFNC_BayerGB8: r0c0_R=true;  startOdd=true;  break;
    case PFNC_BayerBG8: r0c0_R=false; startOdd=true;  break;
    default: break;
    }
    const int x0=(16), xE=(w-16)&~15, n=xE-x0;

    for(int y=0;y<h;++y){
        const uint8_t* P=src+(y>0?y-1:0)*w;
        const uint8_t* C=src+y*w;
        const uint8_t* N=src+(y<h-1?y+1:h-1)*w;
        uint8_t* D=dst+y*w*3;
        const bool isRG=((y%2==0)!=startOdd);

        // 경계만 스칼라 (전체 행 X)
        scalarRow_rgb(P,C,N,D,w, 0,   std::min(x0,w), isRG,r0c0_R);
        scalarRow_rgb(P,C,N,D,w, xE,  w,               isRG,r0c0_R);

        if(n>0 && y>0 && y<h-1){
            if(isRG) processRGrow(P,C,N,D,x0,n,r0c0_R);
            else     processGBrow(P,C,N,D,x0,n,r0c0_R);
        } else if(n>0) {
            // 상하 경계행: 내부도 스칼라 (위아래 행이 없음)
            scalarRow_rgb(P,C,N,D,w, x0, xE, isRG,r0c0_R);
        }
    }
}

// ── 공개 인터페이스: BGRA32 출력 + non-temporal store ─────────────────────────
void debayerAVX2_BGRA(const uint8_t* __restrict src,
                      uint8_t*       __restrict dst,
                      int w, int h, uint32_t pfnc)
{
    if (!src || !dst || w < 32 || h < 4) return;

    bool r0c0_R=true, startOdd=false;
    switch(pfnc){
    case PFNC_BayerRG8: r0c0_R=true;  startOdd=false; break;
    case PFNC_BayerGR8: r0c0_R=false; startOdd=false; break;
    case PFNC_BayerGB8: r0c0_R=true;  startOdd=true;  break;
    case PFNC_BayerBG8: r0c0_R=false; startOdd=true;  break;
    default: break;
    }

    // AVX2 처리 범위: x=[16, (w-16)&~15), y=[1, h-1)
    const int x0 = 16;
    const int xE = (w - 16) & ~15;
    const int n  = xE - x0;

    for (int y = 0; y < h; ++y) {
        const uint8_t* P = src + (y > 0   ? y-1 : 0)   * w;
        const uint8_t* C = src + y * w;
        const uint8_t* N = src + (y < h-1 ? y+1 : h-1) * w;
        uint8_t*       D = dst + y * w * 4;
        const bool isRG  = ((y % 2 == 0) != startOdd);

        // 좌측 경계 (x=0 ~ x0)
        scalarBGRA_range(P, C, N, D, w, 0, std::min(x0, w), isRG, r0c0_R);
        // 우측 경계 (x=xE ~ w)
        scalarBGRA_range(P, C, N, D, w, xE, w,               isRG, r0c0_R);

        if (n > 0 && y > 0 && y < h-1) {
            // 내부: AVX2 + non-temporal store
            if (isRG) processRGrowBGRA(P, C, N, D, x0, n, r0c0_R);
            else      processGBrowBGRA(P, C, N, D, x0, n, r0c0_R);
        } else if (n > 0) {
            // 상하 경계행: 스칼라 (이전/다음 행 경계 처리)
            scalarBGRA_range(P, C, N, D, w, x0, xE, isRG, r0c0_R);
        }
    }

    // non-temporal store 후 fence (다음 일반 load 전에 필요)
    _mm_sfence();
}
