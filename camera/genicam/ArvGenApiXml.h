#pragma once
// ============================================================
// ArvGenApiXml.h
// GenICam XML 파싱: AcquisitionStart/Stop 레지스터 주소 추출
//
// 카메라 내장 XML (manifest table에서 다운로드)에서
// Command 노드 -> pValue -> IntReg(Address, Length) 체인을 따라
// AcquisitionStart/Stop 커맨드를 실행할 레지스터 주소와 값을 얻음
// ============================================================

#include <QString>
#include <QByteArray>

struct ArvCommandNode {
    QString  name;           // e.g. "AcquisitionStart"
    uint64_t address{};      // IntReg 레지스터 주소
    uint32_t length{4};      // 레지스터 크기 (bytes)
    uint32_t commandValue{1};// CommandValue (보통 1)
    bool     bigEndian{false};
    bool     valid{false};
};

struct ArvGenApiInfo {
    ArvCommandNode acquisitionStart;
    ArvCommandNode acquisitionStop;
    bool           parsed{false};
};

class ArvGenApiXml
{
public:
    // ZIP 압축 해제 (ZIP local file 헤더 방식)
    // manifest entry의 schema가 ZIP이면 호출
    static QByteArray decompressZip(const QByteArray& zipData);

    // XML 파싱: AcquisitionStart/Stop 커맨드 노드 주소 추출
    static ArvGenApiInfo parse(const QByteArray& xmlData);
};
