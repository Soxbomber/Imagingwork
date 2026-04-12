#pragma once
// ============================================================
// ArvGenApiXml.h
// GenICam XML 완전 파싱: 모든 노드 타입 지원
//
// 지원 노드:
//   Command    → AcquisitionStart/Stop
//   Integer    → Width, Height, OffsetX/Y, ExposureTime(us), Gain, FrameRate
//   Float      → ExposureTime(float), Gain(float), FrameRate(float)
//   Enumeration → AcquisitionMode, PixelFormat, TriggerMode 등
//
// 레지스터 체인 해석:
//   Integer/Float.pValue → IntReg/FloatReg.Address
//   Enumeration.pValue   → IntReg.Address (EnumEntry.Value 기록)
// ============================================================

#include <QString>
#include <QByteArray>
#include <QMap>
#include <QVariant>
#include <cstdint>

// ── 노드 타입 ─────────────────────────────────────────────────────────────────
enum class GenApiNodeType {
    Unknown,
    Command,
    Integer,    // Integer 노드 → IntReg → 레지스터에 int64 read/write
    Float,      // Float 노드   → FloatReg → 레지스터에 double read/write
    Enumeration,// Enumeration  → IntReg → 선택값 int 기록
};

// ── 하나의 노드 (파라미터 하나에 대응) ──────────────────────────────────────
struct GenApiNode {
    QString         name;
    GenApiNodeType  type    { GenApiNodeType::Unknown };
    uint64_t        address { 0 };    // 레지스터 물리 주소
    uint32_t        length  { 4 };    // 레지스터 바이트 수 (4 or 8)
    bool            bigEndian { false };
    bool            valid   { false };

    // Integer/Float 공통
    double          minVal  { 0.0 };
    double          maxVal  { 0.0 };
    double          incVal  { 1.0 };  // Integer step
    QString         unit;             // e.g. "us", "dB", "Hz"

    // Enumeration 전용: entry 이름 → 정수 값
    QMap<QString, int64_t> enumEntries;

    // Command 전용
    uint32_t        commandValue { 1 };
};

// ── 파싱 결과 (전체 노드 맵) ─────────────────────────────────────────────────
struct ArvGenApiInfo {
    // 이전 버전 호환
    GenApiNode acquisitionStart;
    GenApiNode acquisitionStop;
    bool       parsed { false };

    // 전체 노드 맵 (name → GenApiNode)
    QMap<QString, GenApiNode> nodes;

    // 편의 조회: 노드 존재 여부
    bool has(const QString& name) const { return nodes.contains(name); }
    const GenApiNode* get(const QString& name) const {
        auto it = nodes.find(name);
        return it != nodes.end() ? &it.value() : nullptr;
    }
};

// ── 하위 호환 alias ───────────────────────────────────────────────────────────
using ArvCommandNode = GenApiNode;

// ── XML 파서 + ZIP 해제 ────────────────────────────────────────────────────────
class ArvGenApiXml
{
public:
    // ZIP 압축 해제 (manifest table의 압축된 XML용)
    static QByteArray decompressZip(const QByteArray& zipData);

    // GenICam XML 완전 파싱 → 모든 노드 추출
    static ArvGenApiInfo parse(const QByteArray& xmlData);
};
