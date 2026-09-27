#include "pch.h"
#include "Exception.h"
#include "FileStream.h"
#include "FNVHash.h"
#include "LSFWriter.h"

LSFWriter::LSFWriter() : m_stringHashMap(StringHashMapSize)
{
    m_metadata.majorVersion = LSMetadata::currentMajorVersion;
    m_stringHashMap.resize(StringHashMapSize);
}

LSFWriter::~LSFWriter()
{
}

void LSFWriter::write(StreamBase& stream, const Resource& resource)
{
    m_metadata = resource.metadata;

    writeRegions(resource);

    Stream strings;
    writeStaticStrings(strings);

    LSFMagic magic;
    std::memcpy(magic.magic, LSF_MAGIC, sizeof(LSF_MAGIC));
    magic.version = static_cast<uint32_t>(m_version);

    stream.write(magic);

    PackedVersion gameVersion{
        .major = m_metadata.majorVersion,
        .minor = m_metadata.minorVersion,
        .revision = m_metadata.revision,
        .build = m_metadata.buildNumber
    };

    if (m_version < LSFVersion::BG3_EXTENDED_HEADER) {
        LSFHeader header;
        header.engineVersion = gameVersion.toVersion32();
        stream.write(header);
    } else {
        LSFExtendedHeader header;
        header.engineVersion = gameVersion.toVersion64();
        stream.write(header);
    }

    // No compression yet

    if (m_version < LSFVersion::BG3_NODE_KEYS) {
        LSFMetadataV5 meta{};
        meta.stringsUncompressedSize = static_cast<uint32_t>(strings.size());
        meta.nodesUncompressedSize = static_cast<uint32_t>(m_nodeStream.size());
        meta.attributesUncompressedSize = static_cast<uint32_t>(m_attrStream.size());
        meta.valuesUncompressedSize = static_cast<uint32_t>(m_valueStream.size());
        meta.compressionFlags = Compression::compressionFlags(m_compressionMethod);
        meta.metadataFormat = m_metadataFormat;

        stream.write(meta);
    } else {
        LSFMetadataV6 meta{};
        meta.stringsUncompressedSize = static_cast<uint32_t>(strings.size());
        meta.keysUncompressedSize = static_cast<uint32_t>(m_keyStream.size());
        meta.nodesUncompressedSize = static_cast<uint32_t>(m_nodeStream.size());
        meta.attributesUncompressedSize = static_cast<uint32_t>(m_attrStream.size());
        meta.valuesUncompressedSize = static_cast<uint32_t>(m_valueStream.size());
        meta.compressionFlags = Compression::compressionFlags(m_compressionMethod);
        meta.metadataFormat = m_metadataFormat;

        stream.write(meta);
    }

    stream.write(strings.bytes().first.get(), strings.size());
    stream.write(m_nodeStream.bytes().first.get(), m_nodeStream.size());
    stream.write(m_attrStream.bytes().first.get(), m_attrStream.size());
    stream.write(m_valueStream.bytes().first.get(), m_valueStream.size());
    stream.write(m_keyStream.bytes().first.get(), m_keyStream.size());
}

void LSFWriter::writeRegions(const Resource& resource)
{
    m_nextNodeIndex = 0;

    for (const auto& node : resource.regions | std::views::values) {
        if (m_version >= LSFVersion::EXTENDED_NODES &&
            m_metadataFormat == LSFMetadataFormat::KEYS_AND_ADJACENCY) {
            writeNodeV3(node);
        } else {
            writeNodeV2(node);
        }
    }
}

void LSFWriter::writeStaticString(Stream& stream, const std::string& str)
{
    auto length = static_cast<uint16_t>(str.length());
    stream.write<uint16_t>(length);
    stream.write(str.data(), length);
}

void LSFWriter::writeStaticStrings(Stream& stream)
{
    stream.write<uint32_t>(static_cast<uint32_t>(m_stringHashMap.size()));

    for (const auto& hashEntry : m_stringHashMap) {
        stream.write<uint16_t>(static_cast<uint16_t>(hashEntry.size()));
        for (const auto& str : hashEntry) {
            writeStaticString(stream, str);
        }
    }
}

void LSFWriter::writeNodeV2(const LSNode::Ptr& node)
{
    LSFNodeEntryV2 nodeInfo;

    if (node->parent.lock() == nullptr) {
        nodeInfo.parentIndex = -1;
    } else {
        nodeInfo.parentIndex = m_nodeIndices[node->parent.lock()];
    }

    nodeInfo.nameHashTableIndex = addStaticString(node->name);

    if (!node->attributes.empty()) {
        nodeInfo.firstAttributeIndex = m_nextAttrIndex;
        writeNodeAttributesV2(node);
    } else {
        nodeInfo.firstAttributeIndex = -1;
    }

    m_nodeStream.write<LSFNodeEntryV2>(nodeInfo);
    m_nodeIndices[node] = m_nextNodeIndex++;

    writeNodeChildren(node);
}

void LSFWriter::writeNodeChildren(const LSNode::Ptr& node)
{
    for (const auto& children : node->children | std::views::values) {
        for (const auto& child : children) {
            if (m_version >= LSFVersion::EXTENDED_NODES &&
                m_metadataFormat == LSFMetadataFormat::KEYS_AND_ADJACENCY) {
                writeNodeV3(child);
            } else {
                writeNodeV2(child);
            }
        }        
    }
}

void LSFWriter::writeNodeAttributesV2(const LSNode::Ptr& node)
{
    auto lastOffset = m_valueStream.tell();

    for (const auto& entry : node->attributes) {
        writeAttributeValue(entry.second);

        LSFAttributeEntryV2 attrInfo;
        auto length = m_valueStream.tell() - lastOffset;
        attrInfo.typeAndLength = static_cast<uint32_t>(entry.second.type()) | static_cast<uint32_t>(length << 6);
        attrInfo.nameHashTableIndex = addStaticString(entry.first);
        attrInfo.nodeIndex = m_nextNodeIndex;

        m_attrStream.write<LSFAttributeEntryV2>(attrInfo);
        m_nextAttrIndex++;
        lastOffset = m_valueStream.tell();
    }
}

void LSFWriter::writeAttributeValue(const NodeAttribute& attr)
{
    switch (attr.type()) {
    case String:
    case FixedString:
    case LSString:
    case WString:
    case LSWString:
    case ScratchBuffer:
        writeString(attr.str());
        break;
    case TranslatedFSString:
        writeTranslatedFSString(std::get<TranslatedFSStringT>(attr.value()));
        break;
    case TranslatedString:
        writeTranslatedString(std::get<TranslatedStringT>(attr.value()));
        break;
    case Byte:
        m_valueStream.write(std::get<uint8_t>(attr.value()));
        break;
    case Short:
        m_valueStream.write(std::get<int16_t>(attr.value()));
        break;
    case UShort:
        m_valueStream.write(std::get<uint16_t>(attr.value()));
        break;
    case Int:
        m_valueStream.write(std::get<int32_t>(attr.value()));
        break;
    case UInt:
        m_valueStream.write(std::get<uint32_t>(attr.value()));
        break;
    case Float:
        m_valueStream.write(std::get<float>(attr.value()));
        break;
    case Double:
        m_valueStream.write(std::get<double>(attr.value()));
        break;
    case IVec2:
        m_valueStream.write(std::get<std::array<int32_t, 2>>(attr.value()));
        break;
    case IVec3:
        m_valueStream.write(std::get<std::array<int32_t, 3>>(attr.value()));
        break;
    case IVec4:
        m_valueStream.write(std::get<std::array<int32_t, 4>>(attr.value()));
        break;
    case Vec2:
        m_valueStream.write(std::get<std::array<float, 2>>(attr.value()));
        break;
    case Vec3:
        m_valueStream.write(std::get<std::array<float, 3>>(attr.value()));
        break;
    case Vec4:
        m_valueStream.write(std::get<std::array<float, 4>>(attr.value()));
        break;
    case Mat2:
    case Mat3:
    case Mat3x4:
    case Mat4x3:
    case Mat4:
        ASSERT(0);
        break;
    case Bool:
        m_valueStream.write(std::get<bool>(attr.value()));
        break;
    case ULongLong:
        m_valueStream.write(std::get<uint64_t>(attr.value()));
        break;
    case Long:
        m_valueStream.write(std::get<int64_t>(attr.value()));
        break;
    case Int8:
        m_valueStream.write(std::get<int8_t>(attr.value()));
        break;
    case Uuid:
        m_valueStream.write(std::get<UUIDT>(attr.value()));
        break;
    case Int64:
        m_valueStream.write(std::get<int64_t>(attr.value()));
        break;
    default:
        throw Exception("Unsupported attribute type");
    }
}

void LSFWriter::writeString(const std::string& str)
{
    m_valueStream.write(str.data(), str.size());
    m_valueStream.write<uint8_t>(0); // null terminator
}

void LSFWriter::writeStringWithLength(const std::string& value)
{
    auto length = static_cast<uint32_t>(value.size());
    m_valueStream.write(length + 1);
    m_valueStream.write(value.data(), length);
    m_valueStream.write<uint8_t>(0); // null terminator
}

void LSFWriter::writeTranslatedFSString(const TranslatedFSStringT& str)
{
    if (m_version >= LSFVersion::BG3 ||
        m_metadata.majorVersion > 4 ||
        (m_metadata.majorVersion == 4 && m_metadata.minorVersion > 0) ||
        (m_metadata.majorVersion == 4 && m_metadata.minorVersion == 0 && m_metadata.buildNumber >= 0x1a)) {
        m_valueStream.write(str.version);
    } else {
        writeStringWithLength(str.value);
    }

    writeStringWithLength(str.handle);
    if (str.arguments.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        throw Exception("Too many translated string arguments.");
    }
    m_valueStream.write(static_cast<int32_t>(str.arguments.size()));

    for (const auto& arg : str.arguments) {
        writeStringWithLength(arg.key);
        writeTranslatedFSString(*arg.string);
        writeStringWithLength(arg.value);
    }
}

void LSFWriter::writeTranslatedString(const TranslatedStringT& str)
{
    if (m_version >= LSFVersion::BG3) {
        m_valueStream.write(str.version);
    } else {
        writeStringWithLength(str.value);
    }
    writeStringWithLength(str.handle);
}

void LSFWriter::writeNodeV3(const LSNode::Ptr& node)
{
    LSFNodeEntryV3 nodeInfo;

    if (node->parent.lock() == nullptr) {
        nodeInfo.parentIndex = -1;
    } else {
        nodeInfo.parentIndex = m_nodeIndices[node->parent.lock()];
    }

    nodeInfo.nameHashTableIndex = addStaticString(node->name);
    nodeInfo.nextSiblingIndex = m_nextSiblingIndices[m_nextNodeIndex];

    if (!node->attributes.empty()) {
        nodeInfo.firstAttributeIndex = m_nextAttrIndex;
        writeNodeAttributesV3(node);
    } else {
        nodeInfo.firstAttributeIndex = -1;
    }

    m_nodeStream.write<LSFNodeEntryV3>(nodeInfo);

    if (!node->keyAttribute.empty() && m_metadataFormat == LSFMetadataFormat::KEYS_AND_ADJACENCY) {
        LSFKeyEntry keyInfo;
        keyInfo.nodeIndex = m_nextNodeIndex;
        keyInfo.keyName = addStaticString(node->keyAttribute);
        m_keyStream.write<LSFKeyEntry>(keyInfo);
    }

    m_nodeIndices[node] = m_nextNodeIndex++;

    writeNodeChildren(node);
}

void LSFWriter::writeNodeAttributesV3(const LSNode::Ptr& node)
{
    auto lastOffset = m_valueStream.tell();

    auto numWritten = 0;
    for (const auto& entry : node->attributes) {
        writeAttributeValue(entry.second);
        numWritten++;

        LSFAttributeEntryV3 attrInfo;
        auto length = m_valueStream.tell() - lastOffset;
        attrInfo.typeAndLength = static_cast<uint32_t>(entry.second.type()) | static_cast<uint32_t>(length << 6);
        attrInfo.nameHashTableIndex = addStaticString(entry.first);
        if (numWritten == static_cast<int>(node->attributes.size())) {
            attrInfo.nextAttributeIndex = -1;
        } else {
            attrInfo.nextAttributeIndex = m_nextAttrIndex + 1;
        }

        attrInfo.offset = static_cast<uint32_t>(lastOffset);
        m_attrStream.write<LSFAttributeEntryV3>(attrInfo);
        m_nextAttrIndex++;
        lastOffset = m_valueStream.tell();
    }
}

uint32_t LSFWriter::addStaticString(const std::string& str)
{
    auto hash = fnvhash::hash(str);

    auto bucket = static_cast<int>((hash & 0x1ff) ^ ((hash >> 9) & 0x1ff) ^ ((hash >> 18) & 0x1ff) ^ ((hash >> 27) &
        0x1ff));

    for (auto i = 0ull; i < m_stringHashMap[bucket].size(); ++i) {
        if (m_stringHashMap[bucket][i] == str) {
            return static_cast<uint32_t>((bucket << 16) | static_cast<uint32_t>(i));
        }
    }

    m_stringHashMap[bucket].emplace_back(str);

    return static_cast<uint32_t>((bucket << 16) | static_cast<uint32_t>(m_stringHashMap[bucket].size() - 1));
}
