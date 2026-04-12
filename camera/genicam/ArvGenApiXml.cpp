#include "ArvGenApiXml.h"
#include <QXmlStreamReader>
#include <QDebug>
#include <zlib.h>
#include <cstring>
#include <algorithm>

// ============================================================
// ZIP 파서 - Aravis arvzip.c 완전 포팅
//
// Aravis 핵심 로직:
//  1. 버퍼 뒤에서 End of Central Directory (PK\x05\x06) 검색
//  2. Central Directory에서 각 파일의 compressed/uncompressed 크기 읽기
//  3. Local File Header에서 실제 데이터 오프셋 계산
//  4. compressed < uncompressed → inflateInit2(-MAX_WBITS) raw deflate
//     compressed == uncompressed → memcpy (STORE)
//
// IDS 카메라 ZIP 특성:
//  - Local File Header의 method=0(STORE)이지만 Central Directory에는
//    실제 크기 정보가 있어 Aravis는 compressed<uncompressed 조건으로 inflate
// ============================================================

static inline uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
static inline uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24));
}

QByteArray ArvGenApiXml::decompressZip(const QByteArray& zipData)
{
    const auto* buf = reinterpret_cast<const uint8_t*>(zipData.constData());
    const int   n   = zipData.size();

    if (n < 22) {
        qWarning("ArvGenApiXml: data too small for ZIP");
        return {};
    }

    // ── Step 1: End of Central Directory 검색 (뒤에서부터) ─────────────────
    // Aravis: for (i = zip->buffer_size - 4; i > 0; i--)
    //         if (buffer[i]==0x50 && buffer[i+1]==0x4b &&
    //             buffer[i+2]==0x05 && buffer[i+3]==0x06)
    int eocdPos = -1;
    for (int i = n - 4; i >= 0; --i) {
        if (buf[i]==0x50 && buf[i+1]==0x4b &&
            buf[i+2]==0x05 && buf[i+3]==0x06) {
            eocdPos = i;
            break;
        }
    }
    if (eocdPos < 0) {
        qWarning("ArvGenApiXml: ZIP End of Central Directory not found");
        return {};
    }

    const uint8_t* eocd = buf + eocdPos;
    // EOCD: sig(4) diskNum(2) startDisk(2) numEntriesThisDisk(2)
    //       numEntries(2) dirSize(4) dirOffset(4) commentLen(2)
    const uint16_t numEntries  = le16(eocd + 10);
    const uint32_t dirSize     = le32(eocd + 12);
    const uint32_t dirOffset   = le32(eocd + 16);

    // Aravis: header_size = directory_position - (directory_offset + directory_size)
    // 즉 ZIP 앞부분에 prepended header가 있을 수 있음 (IDS가 이런 구조)
    const ptrdiff_t headerSize = eocdPos - (static_cast<ptrdiff_t>(dirOffset) +
                                            static_cast<ptrdiff_t>(dirSize));

    qDebug("ArvGenApiXml: ZIP EOCD at 0x%X numEntries=%u "
           "dirSize=%u dirOffset=0x%X headerSize=%lld",
           eocdPos, numEntries, dirSize, dirOffset,
           (long long)headerSize);

    if (numEntries == 0) {
        qWarning("ArvGenApiXml: ZIP has no files");
        return {};
    }

    // ── Step 2: Central Directory 파싱 (첫 번째 파일 엔트리) ───────────────
    // Central Directory File Header: PK\x01\x02
    const uint8_t* cdPtr = buf + headerSize + dirOffset;

    if (cdPtr + 46 > buf + n) {
        qWarning("ArvGenApiXml: Central Directory out of bounds");
        return {};
    }
    if (le32(cdPtr) != 0x02014b50) {
        qWarning("ArvGenApiXml: Central Directory signature mismatch "
                 "(got 0x%08X)", le32(cdPtr));
        return {};
    }

    // CD entry: sig(4) verMade(2) verNeeded(2) flags(2) method(2)
    //           mtime(2) mdate(2) crc(4) compSize(4) uncompSize(4)
    //           fnLen(2) extraLen(2) commentLen(2) ...
    //           localHeaderOffset(4) at offset 42
    const uint32_t compSize   = le32(cdPtr + 20);
    const uint32_t uncompSize = le32(cdPtr + 24);
    const uint16_t fnLen      = le16(cdPtr + 28);
    const uint32_t fileOffset = le32(cdPtr + 42);
    const QString  fname      = QString::fromLatin1(
        reinterpret_cast<const char*>(cdPtr + 46), fnLen);

    qDebug("ArvGenApiXml: ZIP file='%s' compSize=%u uncompSize=%u "
           "localOffset=0x%X",
           qPrintable(fname), compSize, uncompSize, fileOffset);

    // ── Step 3: Local File Header에서 실제 데이터 오프셋 계산 ──────────────
    // Aravis arv_zip_get_file_data():
    //   offset = zip_file->offset + header_size
    //   return offset + lfh[26] + lfh[28] + 30  (fnLen + extraLen + headerSize)
    const uint8_t* lfh = buf + headerSize + fileOffset;
    if (lfh + 30 > buf + n) {
        qWarning("ArvGenApiXml: Local File Header out of bounds");
        return {};
    }
    if (le32(lfh) != 0x04034b50) {
        qWarning("ArvGenApiXml: Local File Header signature mismatch");
        return {};
    }
    const uint16_t lfhFnLen    = le16(lfh + 26);
    const uint16_t lfhExtraLen = le16(lfh + 28);
    const uint8_t* dataPtr     = lfh + 30 + lfhFnLen + lfhExtraLen;

    if (dataPtr + compSize > buf + n) {
        qWarning("ArvGenApiXml: ZIP file data out of bounds "
                 "(dataOffset=%lld compSize=%u totalSize=%d)",
                 (long long)(dataPtr - buf), compSize, n);
        return {};
    }

    // ── Step 4: 압축 해제 ──────────────────────────────────────────────────
    // Aravis: if (compressed_size < uncompressed_size) → inflate
    //         else → memcpy
    QByteArray out(static_cast<int>(uncompSize), '\0');

    if (compSize < uncompSize) {
        // raw DEFLATE (Aravis: inflateInit2(&zs, -MAX_WBITS))
        z_stream zs{};
        zs.next_in   = const_cast<Bytef*>(dataPtr);
        zs.avail_in  = compSize;
        zs.next_out  = reinterpret_cast<Bytef*>(out.data());
        zs.avail_out = uncompSize;

        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
            qWarning("ArvGenApiXml: inflateInit2 failed");
            return {};
        }
        const int ret = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);

        if (ret != Z_STREAM_END && ret != Z_OK) {
            qWarning("ArvGenApiXml: inflate failed ret=%d total_out=%lu",
                     ret, zs.total_out);
            // 부분 성공도 시도 (일부 카메라 XML은 Z_BUF_ERROR 반환 후 정상)
            if (zs.total_out == 0) return {};
        }
        out.resize(static_cast<int>(zs.total_out));
        qDebug("ArvGenApiXml: inflated %u → %lu bytes",
               compSize, zs.total_out);
    } else {
        // STORE
        std::memcpy(out.data(), dataPtr, uncompSize);
        qDebug("ArvGenApiXml: STORE copied %u bytes", uncompSize);
    }

    return out;
}

// ============================================================
// GenICam XML 완전 파서
// Integer / Float / Enumeration / Command / IntReg / FloatReg 지원
// ============================================================

static bool isRegNode(const QString& tag)
{
    return tag == "IntReg"    || tag == "MaskedIntReg" ||
           tag == "FloatReg"  || tag == "StringReg"    ||
           tag == "StructReg" || tag == "Register";
}

static bool isFeatureNode(const QString& tag)
{
    return tag == "Integer"     || tag == "Float"   ||
           tag == "Enumeration" || tag == "Command"  ||
           tag == "Boolean"     || tag == "String";
}

ArvGenApiInfo ArvGenApiXml::parse(const QByteArray& xmlData)
{
    ArvGenApiInfo info;
    if (xmlData.isEmpty()) return info;

    // BOM 제거
    const char* data = xmlData.constData();
    int         size = xmlData.size();
    if (size >= 3 &&
        static_cast<uint8_t>(data[0])==0xEF &&
        static_cast<uint8_t>(data[1])==0xBB &&
        static_cast<uint8_t>(data[2])==0xBF)
        { data += 3; size -= 3; }

    // ── 1패스: 모든 노드 수집 ────────────────────────────────────────────────
    // 레지스터 정의 (IntReg/FloatReg 등)
    struct RegDef {
        uint64_t address  {};
        uint32_t length   {4};
        bool     bigEndian{false};
        bool     hasAddr  {false};
        bool     isFloat  {false}; // FloatReg
    };

    // 피처 노드 임시 정의
    struct FeatureDef {
        QString         tag;       // "Integer","Float","Enumeration","Command"
        QString         pValueRef; // → RegDef 또는 Converter 이름
        double          minVal{};
        double          maxVal{};
        double          incVal{1.0};
        QString         unit;
        uint32_t        cmdValue{1};
        QMap<QString, int64_t> enumEntries;
        QString         curEntry;  // EnumEntry 처리 중
    };

    QMap<QString, RegDef>     regs;
    QMap<QString, FeatureDef> features;

    QXmlStreamReader xml(QByteArray::fromRawData(data, size));

    QString curName;
    QString curTag;
    bool    inFeature = false;
    bool    inReg     = false;
    bool    inEntry   = false;
    QString entryName;

    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (xml.hasError()) break;

        if (tok == QXmlStreamReader::StartElement) {
            const QString tag  = xml.name().toString();
            const QString name = xml.attributes().value("Name").toString();

            if (isFeatureNode(tag) && !name.isEmpty()) {
                curName = name; curTag = tag;
                inFeature = true; inReg = false; inEntry = false;
                FeatureDef fd; fd.tag = tag;
                features[curName] = fd;

            } else if (isRegNode(tag) && !name.isEmpty()) {
                curName = name; curTag = tag;
                inReg = true; inFeature = false; inEntry = false;
                if (!regs.contains(curName)) {
                    RegDef rd;
                    rd.isFloat = (tag == "FloatReg");
                    regs[curName] = rd;
                }

            } else if (inFeature) {
                auto& fd = features[curName];

                if (tag == "EnumEntry" && !name.isEmpty()) {
                    inEntry = true;
                    entryName = name;
                    fd.curEntry = name;
                    fd.enumEntries[name] = 0; // 기본값

                } else if (inEntry && tag == "Value") {
                    bool ok; int64_t v = xml.readElementText().trimmed().toLongLong(&ok, 0);
                    if (ok) features[curName].enumEntries[entryName] = v;

                } else if (!inEntry) {
                    if (tag == "pValue") {
                        fd.pValueRef = xml.readElementText().trimmed();
                    } else if (tag == "CommandValue") {
                        bool ok; uint32_t v = xml.readElementText().trimmed().toUInt(&ok,0);
                        if (ok) fd.cmdValue = v;
                    } else if (tag == "Min") {
                        bool ok; double v = xml.readElementText().trimmed().toDouble(&ok);
                        if (ok) fd.minVal = v;
                    } else if (tag == "Max") {
                        bool ok; double v = xml.readElementText().trimmed().toDouble(&ok);
                        if (ok) fd.maxVal = v;
                    } else if (tag == "Inc") {
                        bool ok; double v = xml.readElementText().trimmed().toDouble(&ok);
                        if (ok) fd.incVal = v;
                    } else if (tag == "Unit") {
                        fd.unit = xml.readElementText().trimmed();
                    }
                }

            } else if (inReg) {
                auto& rd = regs[curName];
                if (tag == "Address" || tag == "Offset") {
                    bool ok; uint64_t v = xml.readElementText().trimmed().toULongLong(&ok,0);
                    if (ok && v != 0) { rd.address = v; rd.hasAddr = true; }
                } else if (tag == "Length") {
                    bool ok; uint32_t v = xml.readElementText().trimmed().toUInt(&ok,0);
                    if (ok) rd.length = v;
                } else if (tag == "Endianess" || tag == "Endianness") {
                    rd.bigEndian = xml.readElementText().trimmed() == "BigEndian";
                }
            }

        } else if (tok == QXmlStreamReader::EndElement) {
            const QString tag = xml.name().toString();
            if (isFeatureNode(tag) && tag == curTag) { inFeature = false; }
            if (isRegNode(tag)     && tag == curTag) { inReg = false; }
            if (tag == "EnumEntry")                  { inEntry = false; }
        }
    }

    qDebug("ArvGenApiXml: parsed %d features, %d registers",
           features.size(), regs.size());

    // ── 2패스: 피처 → 레지스터 연결 → GenApiNode 생성 ──────────────────────
    for (auto it = features.begin(); it != features.end(); ++it) {
        const QString&    fname = it.key();
        const FeatureDef& fd    = it.value();

        GenApiNode node;
        node.name    = fname;
        node.minVal  = fd.minVal;
        node.maxVal  = fd.maxVal;
        node.incVal  = fd.incVal;
        node.unit    = fd.unit;
        node.commandValue = fd.cmdValue;
        node.enumEntries  = fd.enumEntries;

        // 노드 타입 결정
        if      (fd.tag == "Integer")     node.type = GenApiNodeType::Integer;
        else if (fd.tag == "Float")       node.type = GenApiNodeType::Float;
        else if (fd.tag == "Enumeration") node.type = GenApiNodeType::Enumeration;
        else if (fd.tag == "Command")     node.type = GenApiNodeType::Command;
        else continue;

        // pValue → 레지스터 연결 (직접 또는 Converter 체인)
        QString regRef = fd.pValueRef;

        // 최대 3단계 간접 참조 (IntSwissKnife, Converter 등)
        for (int depth = 0; depth < 3 && !regs.contains(regRef); ++depth) {
            if (features.contains(regRef))
                regRef = features[regRef].pValueRef;
            else
                break;
        }

        if (!regs.contains(regRef)) {
            // 레지스터를 찾지 못해도 노드는 등록 (추후 확장 가능)
            continue;
        }

        const RegDef& rd = regs[regRef];
        if (!rd.hasAddr) continue;

        node.address   = rd.address;
        node.length    = rd.length;
        node.bigEndian = rd.bigEndian;
        node.valid     = true;

        // FloatReg이면 Float 타입으로 보정
        if (rd.isFloat && node.type == GenApiNodeType::Integer)
            node.type = GenApiNodeType::Float;

        info.nodes[fname] = node;
    }

    qDebug("ArvGenApiXml: resolved %d nodes", info.nodes.size());

    // ── 하위 호환: AcquisitionStart/Stop 추출 ────────────────────────────────
    if (info.nodes.contains("AcquisitionStart"))
        info.acquisitionStart = info.nodes["AcquisitionStart"];
    if (info.nodes.contains("AcquisitionStop"))
        info.acquisitionStop  = info.nodes["AcquisitionStop"];

    info.parsed = true;
    return info;
}
