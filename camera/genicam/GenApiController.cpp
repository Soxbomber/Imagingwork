// GenApiController.cpp
// HAVE_GENICAM_SDK 정의 시에만 컴파일 (CMakeLists에서 thirdparty/genicam 설정 필요)
#ifdef HAVE_GENICAM_SDK

// ============================================================
// GenApiController.cpp
// GenICam SDK v3.5 기반 구현
// ============================================================
#include "GenApiController.h"
#include <QDebug>
#include <stdexcept>

// GenICam 스마트 포인터 단축
using IntPtr  = GA::CIntegerPtr;
using FltPtr  = GA::CFloatPtr;
using StrPtr  = GA::CStringPtr;
using CmdPtr  = GA::CCommandPtr;
using EnumPtr = GA::CEnumerationPtr;
using RegPtr  = GA::CRegisterPtr;

// ── reset ────────────────────────────────────────────────────────────────────
void GenApiController::reset()
{
    // INodeMap 소유권은 CNodeMapFactory에 있으므로 delete 불가
    // factory가 소멸될 때 자동 해제됨
    m_nodeMap = nullptr;
    delete m_port;
    m_port = nullptr;
}

// ── loadXml ──────────────────────────────────────────────────────────────────
bool GenApiController::loadXml(const std::vector<uint8_t>& xmlData,
                                bool isZipped,
                                PortReadFn readFn,
                                PortWriteFn writeFn)
{
    reset();

    try {
        // 1. Port 생성 (GigEDevice ↔ GenApi 브리지)
        m_port = new GvspPort(readFn, writeFn);

        // 2. NodeMapFactory: XML 로드
        //    ContentType_ZippedXml : ZIP으로 압축된 XML (카메라 내장 형식)
        //    ContentType_Xml       : 일반 XML 텍스트
        auto contentType = isZipped
            ? GA::ContentType_ZippedXml
            : GA::ContentType_Xml;

        // Member factory — each GenApiController owns its NodeMap lifetime
        m_factory = GA::CNodeMapFactory(contentType,
                                        xmlData.data(),
                                        xmlData.size());

        // 3. NodeMap 생성
        m_nodeMap = m_factory.CreateNodeMap();

        // 4. Port 연결 (Device 표준 포트)
        if (m_nodeMap && !m_nodeMap->Connect(m_port)) {
            qWarning("GenApiController: failed to connect port");
        }

        if (!m_nodeMap) {
            qWarning("GenApiController: CreateNodeMap returned nullptr");
            return false;
        }

        // 노드 수 확인
        GA::NodeList_t nodes;
        m_nodeMap->GetNodes(nodes);
        qDebug("GenApiController: loaded %zu nodes", nodes.size());
        return true;

    } catch (const GC::GenericException& e) {
        qWarning("GenApiController: GenICam exception: %s",
                 e.GetDescription());
        reset();
        return false;
    } catch (const std::exception& e) {
        qWarning("GenApiController: exception: %s", e.what());
        reset();
        return false;
    }
}

// ── 접근 모드 확인: CPointer를 T* 변환 후 자유 함수에 전달 ──────────────────

// ── getInteger ────────────────────────────────────────────────────────────────
bool GenApiController::getInteger(const char* name, int64_t& out) const
{
    if (!m_nodeMap) return false;
    try {
        IntPtr p = m_nodeMap->GetNode(name);
        if (!p.IsValid() || !GA::IsReadable(p->GetAccessMode())) return false;
        out = p->GetValue();
        return true;
    } catch (...) { return false; }
}

// ── setInteger ────────────────────────────────────────────────────────────────
bool GenApiController::setInteger(const char* name, int64_t val)
{
    if (!m_nodeMap) return false;
    try {
        IntPtr p = m_nodeMap->GetNode(name);
        if (!p.IsValid() || !GA::IsWritable(p->GetAccessMode())) return false;
        p->SetValue(val);
        return true;
    } catch (const GC::GenericException& e) {
        qWarning("GenApiController::setInteger(%s): %s", name, e.GetDescription());
        return false;
    }
}

// ── getFloat ─────────────────────────────────────────────────────────────────
bool GenApiController::getFloat(const char* name, double& out) const
{
    if (!m_nodeMap) return false;
    try {
        FltPtr p = m_nodeMap->GetNode(name);
        if (!p.IsValid() || !GA::IsReadable(p->GetAccessMode())) return false;
        out = p->GetValue();
        return true;
    } catch (...) { return false; }
}

// ── setFloat ──────────────────────────────────────────────────────────────────
bool GenApiController::setFloat(const char* name, double val)
{
    if (!m_nodeMap) return false;
    try {
        FltPtr p = m_nodeMap->GetNode(name);
        if (!p.IsValid() || !GA::IsWritable(p->GetAccessMode())) return false;
        p->SetValue(val);
        return true;
    } catch (const GC::GenericException& e) {
        qWarning("GenApiController::setFloat(%s): %s", name, e.GetDescription());
        return false;
    }
}

// ── getString ─────────────────────────────────────────────────────────────────
bool GenApiController::getString(const char* name, std::string& out) const
{
    if (!m_nodeMap) return false;
    try {
        StrPtr p = m_nodeMap->GetNode(name);
        if (!p.IsValid() || !GA::IsReadable(p->GetAccessMode())) return false;
        out = std::string(p->GetValue().c_str());
        return true;
    } catch (...) { return false; }
}

// ── getEnum ───────────────────────────────────────────────────────────────────
bool GenApiController::getEnum(const char* name, std::string& out) const
{
    if (!m_nodeMap) return false;
    try {
        EnumPtr p = m_nodeMap->GetNode(name);
        if (!p.IsValid() || !GA::IsReadable(p->GetAccessMode())) return false;
        out = std::string(p->ToString().c_str());
        return true;
    } catch (...) { return false; }
}

// ── setEnum ───────────────────────────────────────────────────────────────────
bool GenApiController::setEnum(const char* name, const char* val)
{
    if (!m_nodeMap) return false;
    try {
        EnumPtr p = m_nodeMap->GetNode(name);
        if (!p.IsValid() || !GA::IsWritable(p->GetAccessMode())) return false;
        p->FromString(GC::gcstring(val));
        return true;
    } catch (const GC::GenericException& e) {
        qWarning("GenApiController::setEnum(%s=%s): %s", name, val, e.GetDescription());
        return false;
    }
}

// ── execute ───────────────────────────────────────────────────────────────────
bool GenApiController::execute(const char* name)
{
    if (!m_nodeMap) return false;
    try {
        CmdPtr p = m_nodeMap->GetNode(name);
        if (!p.IsValid() || !GA::IsWritable(p->GetAccessMode())) return false;
        p->Execute();
        qDebug("GenApiController::execute(%s) OK", name);
        return true;
    } catch (const GC::GenericException& e) {
        qWarning("GenApiController::execute(%s): %s", name, e.GetDescription());
        return false;
    }
}

// ── getRegisterAddress ────────────────────────────────────────────────────────
// AcquisitionStart 등 Command 노드의 실제 레지스터 주소 조회
// 기존 FeatureDesc(address, commandValue) 방식과의 호환용
bool GenApiController::getRegisterAddress(const char* name,
                                           int64_t& addr,
                                           int64_t& len) const
{
    if (!m_nodeMap) return false;
    try {
        RegPtr p = m_nodeMap->GetNode(name);
        if (!p.IsValid()) return false;
        addr = p->GetAddress();
        len  = p->GetLength();
        return true;
    } catch (...) { return false; }
}

// ── hasNode ───────────────────────────────────────────────────────────────────
bool GenApiController::hasNode(const char* name) const
{
    if (!m_nodeMap) return false;
    try {
        GA::INode* node = m_nodeMap->GetNode(name);
        return node != nullptr;
    } catch (...) { return false; }
}

// ── nodeType ──────────────────────────────────────────────────────────────────
GenApiController::NodeType GenApiController::nodeType(const char* name) const
{
    if (!m_nodeMap) return NodeType::Unknown;
    try {
        GA::INode* node = m_nodeMap->GetNode(name);
        if (!node) return NodeType::Unknown;
        switch (node->GetPrincipalInterfaceType()) {
            case GA::intfIInteger:     return NodeType::Integer;
            case GA::intfIFloat:       return NodeType::Float;
            case GA::intfIString:      return NodeType::String;
            case GA::intfIEnumeration: return NodeType::Enum;
            case GA::intfICommand:     return NodeType::Command;
            case GA::intfIBoolean:     return NodeType::Boolean;
            default:                   return NodeType::Unknown;
        }
    } catch (...) { return NodeType::Unknown; }
}

// ── getNodeNames ──────────────────────────────────────────────────────────────
std::vector<std::string> GenApiController::getNodeNames() const
{
    std::vector<std::string> result;
    if (!m_nodeMap) return result;
    try {
        GA::NodeList_t nodes;
        m_nodeMap->GetNodes(nodes);
        result.reserve(nodes.size());
        for (auto* node : nodes)
            result.emplace_back(node->GetName().c_str());
    } catch (...) {}
    return result;
}

#endif  // HAVE_GENICAM_SDK
