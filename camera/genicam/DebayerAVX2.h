#pragma once
// ============================================================
// DebayerAVX2.h
// AVX2 기반 Bayer → RGB 변환 (BayerRG8 / BayerGR8 / BayerGB8 / BayerBG8)
//
// 알고리즘: Malvar-He-Cutler 단순화 bilinear (행 단위 벡터화)
//
// 처리 방식:
//   짝수행(R행 or G행)과 홀수행을 32픽셀씩 AVX2로 처리
//   경계(상하 1행, 좌우 1열)는 스칼라 폴백
//
// 의존: <immintrin.h>, MSVC /arch:AVX2 or GCC -mavx2
// ============================================================

#include <cstdint>

// AVX2 지원 여부 런타임 확인
bool debayerAVX2Supported();

// AVX2 debayer → RGB888 출력
void debayerAVX2(const uint8_t* __restrict src,
                 uint8_t*       __restrict dst,
                 int w, int h, uint32_t pfnc);

// AVX2 debayer → BGRA32 출력 (QImage::Format_ARGB32_Premultiplied)
// QPixmap::fromImage() 변환 비용 제거 - Qt native format 직접 출력
void debayerAVX2_BGRA(const uint8_t* __restrict src,
                      uint8_t*       __restrict dst,
                      int w, int h, uint32_t pfnc);
