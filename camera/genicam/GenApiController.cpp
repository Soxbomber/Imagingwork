#include "GenApiController.h"
#include <QDebug>
#include <cstring>
#include <cmath>

// ── 바이트 순서 변환 ──────────────────────────────────────────────────────────
static inline uint32_t swap32(uint32_t v) {
    return ((v&0xFF)<<24)|((v>>8&0xFF)<<16)|((v>>16&0xFF)<<8)|(v>>24);
}
static inline uint64_t swap64(uint64_t v) {
    return (uint64_t(swap32(uint32_t(v)))<<32) | swap32(uint32_t(v>>32));
}

GenApiController::GenApiController(IRegisterDevice*     dev,
                                   const ArvGenApiInfo& info)
    : m_dev(dev), m_info(info)
{}

// ── 노드 존재 확인 ────────────────────────────────────────────────────────────
bool GenApiController::hasNode(const QString& name) const
{
    return m_info.nodes.contains(name);
}

GenApiNodeType GenApiController::nodeType(const QString& name) const
{
    const auto* n = m_info.get(name);
    return n ? n->type : GenApiNodeType::Unknown;
}

// ── 레지스터 read (32bit) ─────────────────────────────────────────────────────
bool GenApiController::regRead32(const GenApiNode& node, int64_t& val) const
{
    uint32_t raw{};
    if (!m_dev->readRegister(static_cast<uint32_t>(node.address), raw))
        return false;
    if (node.bigEndian) raw = swap32(raw);
    val = static_cast<int64_t>(static_cast<int32_t>(raw));
    return true;
}

bool GenApiController::regWrite32(const GenApiNode& node, int64_t val)
{
    uint32_t raw = static_cast<uint32_t>(static_cast<int32_t>(val));
    if (node.bigEndian) raw = swap32(raw);
    return m_dev->writeRegister(static_cast<uint32_t>(node.address), raw);
}

// ── 레지스터 read (64bit) ─────────────────────────────────────────────────────
bool GenApiController::regRead64(const GenApiNode& node, int64_t& val) const
{
    uint8_t buf[8]{};
    if (!m_dev->readMemory(static_cast<uint32_t>(node.address), buf, 8))
        return false;
    uint64_t raw{};
    std::memcpy(&raw, buf, 8);
    if (node.bigEndian) raw = swap64(raw);
    val = static_cast<int64_t>(raw);
    return true;
}

bool GenApiController::regWrite64(const GenApiNode& node, int64_t val)
{
    uint64_t raw = static_cast<uint64_t>(val);
    if (node.bigEndian) raw = swap64(raw);
    uint8_t buf[8]{};
    std::memcpy(buf, &raw, 8);
    return m_dev->writeMemory(static_cast<uint32_t>(node.address), buf, 8);
}

// ── 레지스터 read (IEEE754 double, FloatReg) ──────────────────────────────────
bool GenApiController::regReadFloat(const GenApiNode& node, double& val) const
{
    if (node.length == 8) {
        // 64bit IEEE754 double
        uint8_t buf[8]{};
        if (!m_dev->readMemory(static_cast<uint32_t>(node.address), buf, 8))
            return false;
        uint64_t raw{};
        std::memcpy(&raw, buf, 8);
        if (node.bigEndian) raw = swap64(raw);
        std::memcpy(&val, &raw, 8);
    } else {
        // 32bit IEEE754 float → double 변환
        uint32_t raw{};
        if (!m_dev->readRegister(static_cast<uint32_t>(node.address), raw))
            return false;
        if (node.bigEndian) raw = swap32(raw);
        float f{};
        std::memcpy(&f, &raw, 4);
        val = static_cast<double>(f);
    }
    return true;
}

bool GenApiController::regWriteFloat(const GenApiNode& node, double val)
{
    if (node.length == 8) {
        uint64_t raw{};
        std::memcpy(&raw, &val, 8);
        if (node.bigEndian) raw = swap64(raw);
        uint8_t buf[8]{};
        std::memcpy(buf, &raw, 8);
        return m_dev->writeMemory(static_cast<uint32_t>(node.address), buf, 8);
    } else {
        float f = static_cast<float>(val);
        uint32_t raw{};
        std::memcpy(&raw, &f, 4);
        if (node.bigEndian) raw = swap32(raw);
        return m_dev->writeRegister(static_cast<uint32_t>(node.address), raw);
    }
}

// ── Integer ───────────────────────────────────────────────────────────────────
bool GenApiController::getInteger(const QString& name, int64_t& value) const
{
    const auto* n = m_info.get(name);
    if (!n || !n->valid) {
        qWarning("GenApiController::getInteger: node '%s' not found",
                 qPrintable(name));
        return false;
    }
    if (n->type != GenApiNodeType::Integer &&
        n->type != GenApiNodeType::Enumeration) {
        qWarning("GenApiController::getInteger: node '%s' is not Integer",
                 qPrintable(name));
        return false;
    }
    bool ok = (n->length >= 8) ? regRead64(*n, value) : regRead32(*n, value);
    if (ok)
        qDebug("GenApiController: get %s = %lld", qPrintable(name), (long long)value);
    return ok;
}

bool GenApiController::setInteger(const QString& name, int64_t value)
{
    const auto* n = m_info.get(name);
    if (!n || !n->valid) {
        qWarning("GenApiController::setInteger: node '%s' not found",
                 qPrintable(name));
        return false;
    }

    // 범위 클램프
    if (n->maxVal > n->minVal) {
        const int64_t lo = static_cast<int64_t>(n->minVal);
        const int64_t hi = static_cast<int64_t>(n->maxVal);
        const int64_t step = static_cast<int64_t>(n->incVal > 0 ? n->incVal : 1);
        value = std::max(lo, std::min(hi, value));
        if (step > 1) value = lo + ((value - lo) / step) * step;
    }

    bool ok = (n->length >= 8) ? regWrite64(*n, value) : regWrite32(*n, value);
    if (ok)
        qDebug("GenApiController: set %s = %lld", qPrintable(name), (long long)value);
    else
        qWarning("GenApiController: set %s failed", qPrintable(name));
    return ok;
}

// ── Float ─────────────────────────────────────────────────────────────────────
bool GenApiController::getFloat(const QString& name, double& value) const
{
    const auto* n = m_info.get(name);
    if (!n || !n->valid) {
        // Float가 Integer 레지스터로 구현된 경우 (ExposureTime in ns 등)
        int64_t ival{};
        if (getInteger(name, ival)) { value = static_cast<double>(ival); return true; }
        qWarning("GenApiController::getFloat: node '%s' not found",
                 qPrintable(name));
        return false;
    }
    bool ok = (n->type == GenApiNodeType::Float)
              ? regReadFloat(*n, value)
              : (regRead32(*n, reinterpret_cast<int64_t&>(value)),
                 (value = static_cast<double>(static_cast<int64_t>(value))), true);
    // Integer 타입도 float로 읽기 지원
    if (n->type == GenApiNodeType::Integer) {
        int64_t iv{}; ok = regRead32(*n, iv); value = static_cast<double>(iv);
    } else {
        ok = regReadFloat(*n, value);
    }
    if (ok)
        qDebug("GenApiController: get %s = %.4f %s",
               qPrintable(name), value, qPrintable(n->unit));
    return ok;
}

bool GenApiController::setFloat(const QString& name, double value)
{
    const auto* n = m_info.get(name);
    if (!n || !n->valid) {
        qWarning("GenApiController::setFloat: node '%s' not found",
                 qPrintable(name));
        return false;
    }

    // 범위 클램프
    if (n->maxVal > n->minVal)
        value = std::max(n->minVal, std::min(n->maxVal, value));

    bool ok = false;
    if (n->type == GenApiNodeType::Float) {
        ok = regWriteFloat(*n, value);
    } else if (n->type == GenApiNodeType::Integer) {
        // 일부 카메라는 ExposureTime을 Integer(ns)로 구현
        ok = regWrite32(*n, static_cast<int64_t>(std::round(value)));
    }

    if (ok)
        qDebug("GenApiController: set %s = %.4f %s",
               qPrintable(name), value, qPrintable(n->unit));
    else
        qWarning("GenApiController: set %s failed", qPrintable(name));
    return ok;
}

// ── Enumeration ───────────────────────────────────────────────────────────────
bool GenApiController::getEnum(const QString& name, QString& entry) const
{
    const auto* n = m_info.get(name);
    if (!n || !n->valid || n->type != GenApiNodeType::Enumeration) {
        qWarning("GenApiController::getEnum: node '%s' not Enumeration",
                 qPrintable(name));
        return false;
    }

    int64_t raw{};
    if (!regRead32(*n, raw)) return false;

    // 값 → 이름 역매핑
    for (auto it = n->enumEntries.begin(); it != n->enumEntries.end(); ++it) {
        if (it.value() == raw) {
            entry = it.key();
            qDebug("GenApiController: get %s = '%s' (%lld)",
                   qPrintable(name), qPrintable(entry), (long long)raw);
            return true;
        }
    }

    // 알 수 없는 값: 숫자 그대로
    entry = QString::number(raw);
    return true;
}

bool GenApiController::setEnum(const QString& name, const QString& entry)
{
    const auto* n = m_info.get(name);
    if (!n || !n->valid || n->type != GenApiNodeType::Enumeration) {
        qWarning("GenApiController::setEnum: node '%s' not Enumeration",
                 qPrintable(name));
        return false;
    }

    if (!n->enumEntries.contains(entry)) {
        qWarning("GenApiController::setEnum: entry '%s' not found in '%s'",
                 qPrintable(entry), qPrintable(name));
        qWarning("  Available: %s",
                 qPrintable(QStringList(n->enumEntries.keys()).join(", ")));
        return false;
    }

    const int64_t val = n->enumEntries[entry];
    bool ok = regWrite32(*n, val);
    if (ok)
        qDebug("GenApiController: set %s = '%s' (%lld)",
               qPrintable(name), qPrintable(entry), (long long)val);
    else
        qWarning("GenApiController: set %s failed", qPrintable(name));
    return ok;
}

// ── Command 실행 ──────────────────────────────────────────────────────────────
bool GenApiController::execute(const QString& name)
{
    const auto* n = m_info.get(name);
    if (!n || !n->valid || n->type != GenApiNodeType::Command) {
        qWarning("GenApiController::execute: command '%s' not found",
                 qPrintable(name));
        return false;
    }
    return m_dev->writeRegister(static_cast<uint32_t>(n->address),
                                n->commandValue);
}

// ── 범위 조회 ─────────────────────────────────────────────────────────────────
bool GenApiController::getRange(const QString& name,
                                double& minVal, double& maxVal,
                                double& step) const
{
    const auto* n = m_info.get(name);
    if (!n || !n->valid) return false;
    minVal = n->minVal;
    maxVal = n->maxVal;
    step   = n->incVal;
    return true;
}

// ── Enumeration 항목 목록 ─────────────────────────────────────────────────────
QStringList GenApiController::enumEntries(const QString& name) const
{
    const auto* n = m_info.get(name);
    if (!n || n->type != GenApiNodeType::Enumeration) return {};
    return n->enumEntries.keys();
}

// ── 주요 파라미터 일괄 읽기 ──────────────────────────────────────────────────
GenApiController::CameraParams GenApiController::readAll() const
{
    CameraParams p;

    auto tryI = [&](const QString& name, int64_t& dst) {
        int64_t v{}; if (getInteger(name, v)) dst = v; };
    auto tryF = [&](const QString& name, double& dst) {
        double v{}; if (getFloat(name, v)) dst = v; };
    auto tryE = [&](const QString& name, QString& dst) {
        QString s; if (getEnum(name, s)) dst = s; };

    tryI("Width",   p.width);
    tryI("Height",  p.height);
    tryI("OffsetX", p.offsetX);
    tryI("OffsetY", p.offsetY);

    // ExposureTime: 카메라마다 단위가 다를 수 있음 (us or ns)
    // 일반적으로 GenICam 표준은 μs
    tryF("ExposureTime", p.exposureTime);
    tryF("Gain",         p.gain);

    // FrameRate: 다양한 이름 시도
    if (!getFloat("AcquisitionFrameRate", p.frameRate))
        if (!getFloat("ResultingFrameRate", p.frameRate))
            getFloat("FrameRate", p.frameRate);

    tryE("AcquisitionMode", p.acquisitionMode);
    tryE("PixelFormat",     p.pixelFormat);

    p.valid = true;
    return p;
}
