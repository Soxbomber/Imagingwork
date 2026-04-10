#pragma once
// ============================================================
// UvcConvertAVX2.h
// UVC 픽셀 포맷 → ARGB32 변환 (AVX2 가속)
//
// 지원 포맷:
//   ARGB32/RGB32/BGR32 → ARGB32  (alpha 삽입 + NT store)
//   RGB24/BGR24        → ARGB32  (3→4 byte 확장)
//   RGB565             → ARGB32  (bit extract + scale)
//   YUYV/UYVY          → ARGB32  (YUV→RGB 정수 행렬)
//   NV12/NV21          → ARGB32  (planar YUV420)
//   YUV420P            → ARGB32  (planar YUV420)
//
// 모든 함수: dst는 16-byte 정렬 권장 (QImage::bits() 보장)
//            경계 픽셀 자동 스칼라 폴백
// ============================================================
#include <cstdint>

bool uvcAVX2Supported();

// ARGB32: 14MB NT-store memcpy (cache bypass)
void avx2_argb32_to_argb32(const uint8_t* src, uint32_t* dst, int n);

// RGB32(XRGB) → ARGB32: alpha 삽입
void avx2_rgb32_to_argb32(const uint32_t* src, uint32_t* dst, int n);

// BGR32(XBGR) → ARGB32: byte shuffle + alpha
void avx2_bgr32_to_argb32(const uint8_t* src, uint32_t* dst, int n);

// RGB24 → ARGB32: 3→4 byte pack
void avx2_rgb24_to_argb32(const uint8_t* src, int srcStride,
                           uint32_t* dst, int w, int h);

// BGR24 → ARGB32: 3→4 byte pack + R↔B swap
void avx2_bgr24_to_argb32(const uint8_t* src, int srcStride,
                           uint32_t* dst, int w, int h);

// RGB565 → ARGB32
void avx2_rgb565_to_argb32(const uint16_t* src, uint32_t* dst, int n);

// YUYV(YUY2) → ARGB32
void avx2_yuyv_to_argb32(const uint8_t* src, int srcStride,
                          uint32_t* dst, int w, int h);

// UYVY → ARGB32
void avx2_uyvy_to_argb32(const uint8_t* src, int srcStride,
                          uint32_t* dst, int w, int h);

// NV12 (Y plane + interleaved UV) → ARGB32
void avx2_nv12_to_argb32(const uint8_t* y_plane, int yStride,
                          const uint8_t* uv_plane, int uvStride,
                          uint32_t* dst, int w, int h);

// NV21 (Y plane + interleaved VU) → ARGB32
void avx2_nv21_to_argb32(const uint8_t* y_plane, int yStride,
                          const uint8_t* vu_plane, int vuStride,
                          uint32_t* dst, int w, int h);

// YUV420P (Y + U + V separate planes) → ARGB32
void avx2_yuv420p_to_argb32(const uint8_t* y_plane,  int yStride,
                              const uint8_t* u_plane,  int uStride,
                              const uint8_t* v_plane,  int vStride,
                              uint32_t* dst, int w, int h);
