#pragma once
// ============================================================
// GenApiController.h
// GenICam SDK (v3.5) 기반 카메라 NodeMap 제어
//
// SDK 없이 빌드 시: HAVE_GENICAM_SDK 미정의 → 스텁 클래스 사용
// SDK 있을 시:      thirdparty/genicam 에 압축 해제 후 CMake 재구성
// ============================================================
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#ifdef HAVE_GENICAM_SDK

// GenICam SDK 헤더
#include <GenApi/GenApi.h>
#include <GenApi/NodeMapFactory.h>
#include <GenApi/Pointer.h>

namespace GA = GENAPI_NAMESPACE;
namespace GC = GENICAM_NAMESPACE;

using PortReadFn  = std::function<void(void* buf, int64_t addr, int64_t len)>;
using PortWriteFn = std::function<void(const void* buf, int64_t addr, int64_t len)>;

// ── IPort 구현: GigEDevice ↔ GenApi NodeMap 브리지 ───────────────────────────
class GvspPort : public GA::IPort
{
public:
    GvspPort(PortReadFn r, PortWriteFn w) : m_read(r), m_write(w) {}
    void Read (void*       buf, int64_t addr, int64_t len) override { m_read (buf, addr, len); }
    void Write(const void* buf, int64_t addr, int64_t len) override { m_write(buf, addr, len); }
    GA::EAccessMode GetAccessMode() const override { return GA::RW; }
private:
    PortReadFn  m_read;
    PortWriteFn m_write;
};

// ── GenApiController (SDK 버전) ───────────────────────────────────────────────
class GenApiController
{
public:
    GenApiController() = default;
    ~GenApiController() { reset(); }

    bool loadXml(const std::vector<uint8_t>& xmlData, bool isZipped,
                 PortReadFn readFn, PortWriteFn writeFn);

    bool isLoaded() const { return m_nodeMap != nullptr; }

    bool getInteger(const char* name, int64_t& out) const;
    bool setInteger(const char* name, int64_t val);
    bool getFloat  (const char* name, double&  out) const;
    bool setFloat  (const char* name, double   val);
    bool getString (const char* name, std::string& out) const;
    bool getEnum   (const char* name, std::string& out) const;
    bool setEnum   (const char* name, const char* val);
    bool execute   (const char* name);
    bool getRegisterAddress(const char* name, int64_t& addr, int64_t& len) const;

    // 노드 존재 여부 확인
    bool hasNode(const char* name) const;

    // 노드 타입 조회 (ExposureTime이 Integer/Float인지 구분 등)
    enum class NodeType { Unknown, Integer, Float, String, Enum, Command, Boolean };
    NodeType nodeType(const char* name) const;

    std::vector<std::string> getNodeNames() const;
    GA::INodeMap* nodeMap() const { return m_nodeMap; }

private:
    void reset();
    GvspPort*     m_port    {nullptr};
    GA::INodeMap* m_nodeMap {nullptr};
};

#else  // HAVE_GENICAM_SDK 미정의: 스텁 (빌드만 통과)

using PortReadFn  = std::function<void(void* buf, int64_t addr, int64_t len)>;
using PortWriteFn = std::function<void(const void* buf, int64_t addr, int64_t len)>;

class GenApiController
{
public:
    bool loadXml(const std::vector<uint8_t>&, bool,
                 PortReadFn, PortWriteFn) { return false; }
    bool isLoaded() const { return false; }
    bool getInteger(const char*, int64_t&)  const { return false; }
    bool setInteger(const char*, int64_t)         { return false; }
    bool getFloat  (const char*, double&)   const { return false; }
    bool setFloat  (const char*, double)          { return false; }
    bool getString (const char*, std::string&) const { return false; }
    bool getEnum   (const char*, std::string&) const { return false; }
    bool setEnum   (const char*, const char*)       { return false; }
    bool execute   (const char*)                    { return false; }
    bool getRegisterAddress(const char*, int64_t&, int64_t&) const { return false; }
    bool hasNode(const char*) const { return false; }
    enum class NodeType { Unknown, Integer, Float, String, Enum, Command, Boolean };
    NodeType nodeType(const char*) const { return NodeType::Unknown; }
    std::vector<std::string> getNodeNames() const { return {}; }
};

#endif  // HAVE_GENICAM_SDK
