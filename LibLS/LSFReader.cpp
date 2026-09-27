#include "pch.h"
#include "Compress.h"
#include "Exception.h"
#include "LSCommon.h"
#include "LSFReader.h"
#include "Resource.h"

LSFReader::LSFReader() = default;

LSFReader::~LSFReader()
= default;

namespace { // anonymous namespace

template <typename T, size_t N>
std::array<T, N> readArray(Stream& stream)
{
    std::array<T, N> out;

    for (auto& v : out) {
        v = stream.read<T>();
    }

    return out;
}
} // anonymous namespace

Resource::Ptr LSFReader::read(const ByteBuffer& info)
{
    m_stream = Stream::makeStream(info);

    return read();
}

Resource::Ptr LSFReader::read(StreamBase& stream)
{
    m_stream = Stream::makeStream(stream);

    return read();
}

Resource::Ptr LSFReader::read()
{
    readHeader();

    auto namesStream = decompress(m_metadata.stringsSizeOnDisk, m_metadata.stringsUncompressedSize, "strings.bin",
                                  false);
    readNames(namesStream);

    m_nodes.clear();

    auto nodesStream = decompress(m_metadata.nodesSizeOnDisk, m_metadata.nodesUncompressedSize, "nodes.bin", true);

    auto hasAdjacencyData = m_version >= LSFVersion::EXTENDED_NODES
        && m_metadata.metadataFormat == LSFMetadataFormat::KEYS_AND_ADJACENCY;
    readNodes(nodesStream, hasAdjacencyData);

    m_attributes.clear();

    auto attributesStream = decompress(m_metadata.attributesSizeOnDisk, m_metadata.attributesUncompressedSize,
                                       "attributes.bin", true);
    if (hasAdjacencyData) {
        readAttributesV3(attributesStream);
    } else {
        readAttributesV2(attributesStream);
    }

    m_values = decompress(m_metadata.valuesSizeOnDisk, m_metadata.valuesUncompressedSize, "values.bin", true);

    if (m_metadata.metadataFormat == LSFMetadataFormat::KEYS_AND_ADJACENCY) {
        auto keysStream = decompress(m_metadata.keysSizeOnDisk, m_metadata.keysUncompressedSize, "keys.bin", true);
        readKeys(keysStream);
    }

    auto resource = std::make_unique<Resource>();
    resource->metadataFormat = m_metadata.metadataFormat;

    readRegions(resource);

    resource->metadata.majorVersion = m_gameVersion.major;
    resource->metadata.minorVersion = m_gameVersion.minor;
    resource->metadata.revision = m_gameVersion.revision;
    resource->metadata.buildNumber = m_gameVersion.build;

    return resource;
}

void LSFReader::readHeader()
{
    auto [magic, version] = m_stream.read<LSFMagic>();
    if (std::memcmp(magic, LSF_MAGIC, sizeof(LSF_MAGIC)) != 0) {
        throw Exception("Invalid LSF file.");
    }

    if (version < static_cast<uint32_t>(LSFVersion::INITIAL) || version > static_cast<uint32_t>(
        LSFVersion::MAX_READ)) {
        throw Exception(std::format("Unsupported LSF version: {}", version));
    }

    if (version >= static_cast<uint32_t>(LSFVersion::BG3_EXTENDED_HEADER)) {
        auto [engineVersion] = m_stream.read<LSFExtendedHeader>();
        m_gameVersion = PackedVersion::fromInt64(engineVersion);

        // Workaround for LSF files with missing engine version
        if (m_gameVersion.major == 0) {
            m_gameVersion = {.major = 4, .minor = 0, .revision = 9, .build = 0};
        }
    } else {
        auto [engineVersion] = m_stream.read<LSFHeader>();
        m_gameVersion = PackedVersion::fromInt64(engineVersion);

        // Workaround for merged LSF files with missing engine version number
        if (m_gameVersion.major == 0) {
            m_gameVersion = {.major = 4, .minor = 0, .revision = 9, .build = 0};
        }
    }

    if (version < static_cast<uint32_t>(LSFVersion::BG3_NODE_KEYS)) {
        auto meta = m_stream.read<LSFMetadataV5>();
        m_metadata.stringsUncompressedSize = meta.stringsUncompressedSize;
        m_metadata.stringsSizeOnDisk = meta.stringsSizeOnDisk;
        m_metadata.nodesUncompressedSize = meta.nodesUncompressedSize;
        m_metadata.nodesSizeOnDisk = meta.nodesSizeOnDisk;
        m_metadata.attributesUncompressedSize = meta.attributesUncompressedSize;
        m_metadata.attributesSizeOnDisk = meta.attributesSizeOnDisk;
        m_metadata.valuesUncompressedSize = meta.valuesUncompressedSize;
        m_metadata.valuesSizeOnDisk = meta.valuesSizeOnDisk;
        m_metadata.compressionFlags = meta.compressionFlags;
        m_metadata.metadataFormat = meta.metadataFormat;
    } else {
        m_metadata = m_stream.read<LSFMetadataV6>();
    }

    m_version = static_cast<LSFVersion>(version);
}

void LSFReader::readNames(Stream& stream)
{
    // Format:
    // 32-bit hash entry count (N)
    //     N x 16-bit chain length (L)
    //         L x 16-bit string length (S)
    //             [S bytes of UTF-8 string data]

    m_names.clear();

    auto numHashEntries = stream.read<uint32_t>();
    while (numHashEntries-- > 0) {
        auto hash = std::vector<std::string>();

        auto numStrings = stream.read<uint16_t>();
        while (numStrings-- > 0) {
            auto nameLen = stream.read<uint16_t>();
            auto name = readString(stream, nameLen);
            hash.emplace_back(std::move(name));
        }

        m_names.emplace_back(hash);
    }
}

void LSFReader::readNodes(Stream& stream, bool longNodes)
{
    auto size = stream.size();
    while (stream.tell() < size) {
        auto resolved = LSFNodeInfo{};

        if (longNodes) {
            auto item = stream.read<LSFNodeEntryV3>();
            resolved.parentIndex = item.parentIndex;
            resolved.nameIndex = item.nameIndex();
            resolved.nameOffset = item.nameOffset();
            resolved.firstAttributeIndex = item.firstAttributeIndex;
        } else {
            auto item = stream.read<LSFNodeEntryV2>();
            resolved.parentIndex = item.parentIndex;
            resolved.nameIndex = item.nameIndex();
            resolved.nameOffset = item.nameOffset();
            resolved.firstAttributeIndex = item.firstAttributeIndex;
        }

        m_nodes.emplace_back(resolved);
    }
}

void LSFReader::readAttributesV2(Stream& stream)
{
    std::vector<int32_t> prevAttributeRefs;

    uint32_t dataOffset = 0;
    int32_t index = 0;

    auto size = stream.size();
    while (stream.tell() < size) {
        auto attribute = stream.read<LSFAttributeEntryV2>();

        LSFAttributeInfo resolved;
        resolved.nameIndex = attribute.nameIndex();
        resolved.nameOffset = attribute.nameOffset();
        resolved.typeId = attribute.type();
        resolved.length = attribute.length();
        resolved.dataOffset = dataOffset;
        resolved.nextAttributeIndex = -1;

        auto nodeIndex = attribute.nodeIndex + 1;
        if (prevAttributeRefs.size() > static_cast<size_t>(nodeIndex)) {
            if (prevAttributeRefs[nodeIndex] != -1) {
                m_attributes[prevAttributeRefs[nodeIndex]].nextAttributeIndex = index;
            }

            prevAttributeRefs[nodeIndex] = index;
        } else {
            while (prevAttributeRefs.size() < static_cast<size_t>(nodeIndex)) {
                prevAttributeRefs.emplace_back(-1);
            }
            prevAttributeRefs.emplace_back(index);
        }

        dataOffset += resolved.length;
        m_attributes.emplace_back(resolved);
        ++index;
    }
}

void LSFReader::readAttributesV3(Stream& stream)
{
    auto size = stream.size();
    while (stream.tell() < size) {
        auto attribute = stream.read<LSFAttributeEntryV3>();

        LSFAttributeInfo resolved;
        resolved.nameIndex = attribute.nameIndex();
        resolved.nameOffset = attribute.nameOffset();
        resolved.typeId = attribute.type();
        resolved.length = attribute.length();
        resolved.dataOffset = attribute.offset;
        resolved.nextAttributeIndex = attribute.nextAttributeIndex;

        m_attributes.emplace_back(resolved);
    }
}

void LSFReader::readKeys(Stream& stream)
{
    auto size = stream.size();
    while (stream.tell() < size) {
        auto key = stream.read<LSFKeyEntry>();
        auto keyNameIndex = key.keyNameIndex();
        auto keyNameOffset = key.keyNameOffset();

        auto keyAttribute = m_names[keyNameIndex][keyNameOffset];
        auto& node = m_nodes[key.nodeIndex];
        node.keyAttribute = keyAttribute;
    }
}

AttributeValue LSFReader::readVector(const NodeAttribute& attr, Stream& stream)
{
    AttributeValue val;

    switch (attr.type()) {
    case IVec2:
        val = readArray<int32_t, 2>(stream);
        break;
    case IVec3:
        val = readArray<int32_t, 3>(stream);
        break;
    case IVec4:
        val = readArray<int32_t, 4>(stream);
        break;
    case Vec2:
        val = readArray<float, 2>(stream);
        break;
    case Vec3:
        val = readArray<float, 3>(stream);
        break;
    case Vec4:
        val = readArray<float, 4>(stream);
        break;
    default:
        throw Exception("Unsupported vector type.");
    }

    return val;
}

TranslatedFSStringT LSFReader::readTranslatedFSString(Stream& stream) const
{
    TranslatedFSStringT str;

    if (m_version >= LSFVersion::BG3) {
        str.version = stream.read<uint16_t>();
    } else {
        str.version = 0;
        auto valueLength = stream.read<int32_t>();
        if (valueLength < 0) {
            throw Exception("Invalid translated string value length.");
        }
        str.value = readString(stream, valueLength);
    }

    auto handleLength = stream.read<int32_t>();
    if (handleLength < 0) {
        throw Exception("Invalid translated string handle length.");
    }
    str.handle = readString(stream, handleLength);

    auto numArgs = stream.read<int32_t>();
    if (numArgs < 0) {
        throw Exception("Invalid translated string argument count.");
    }
    constexpr auto minimumArgumentSize = sizeof(int32_t) * 4 + sizeof(uint16_t);
    if (stream.tell() > stream.size() ||
        static_cast<size_t>(numArgs) > (stream.size() - stream.tell()) / minimumArgumentSize) {
        throw Exception("Invalid translated string argument count.");
    }
    str.arguments.resize(static_cast<size_t>(numArgs));

    for (auto i = 0; i < numArgs; ++i) {
        TranslatedFSStringArgument arg;
        auto keyLength = stream.read<int32_t>();
        if (keyLength < 0) {
            throw Exception("Invalid translated string argument key length.");
        }
        arg.key = readString(stream, keyLength);
        arg.string = std::make_shared<TranslatedFSStringT>(readTranslatedFSString(stream));

        auto valueLength = stream.read<int32_t>();
        if (valueLength < 0) {
            throw Exception("Invalid translated string argument value length.");
        }
        arg.value = readString(stream, valueLength);
        str.arguments[i] = arg;
    }

    return str;
}

AttributeValue LSFReader::readMatrix(const NodeAttribute& attr, Stream& stream)
{
    AttributeValue val;

    switch (attr.type()) {
    case Mat2:
        val = readArray<float, 4>(stream);
        break;
    case Mat3:
        val = readArray<float, 9>(stream);
        break;
    case Mat3x4:
    case Mat4x3:
        val = readArray<float, 12>(stream);
        break;
    case Mat4:
        val = readArray<float, 16>(stream);
        break;
    default:
        break;
    }

    return val;
}

NodeAttribute LSFReader::readAttribute(AttributeType type, Stream& reader, uint32_t length) const
{
    NodeAttribute attr(type);
    TranslatedStringT str;

    int32_t handleLength;

    switch (type) {
    case String:
    case Path:
    case FixedString:
    case LSString:
    case WString:
    case LSWString:
    case ScratchBuffer:
        attr.setValue(readString(reader, length));
        break;
    case TranslatedString:
        if (m_version >= LSFVersion::BG3 || (m_gameVersion.major > 4 ||
            (m_gameVersion.major == 4 && m_gameVersion.revision > 0) ||
            (m_gameVersion.major == 4 && m_gameVersion.revision == 0 && m_gameVersion.build >= 0x1a))) {
            str.version = reader.read<uint16_t>();
        } else {
            str.version = 0;
            auto valueLength = reader.read<int32_t>();
            str.value = readString(reader, valueLength);
        }

        handleLength = reader.read<int32_t>();
        str.handle = readString(reader, handleLength);
        attr.setValue(str);
        break;
    case TranslatedFSString:
        attr.setValue(readTranslatedFSString(reader));
        break;
    case IVec2:
    case IVec3:
    case IVec4:
    case Vec2:
    case Vec3:
    case Vec4:
        attr.setValue(readVector(attr, reader));
        break;
    case Mat2:
    case Mat3:
    case Mat3x4:
    case Mat4x3:
    case Mat4:
        attr.setValue(readMatrix(attr, reader));
        break;
    default:
        attr = readAttribute(type, reader);
        break;
    }

    return attr;
}

static UUIDT readUUID(Stream& stream)
{
    UUIDT uuid{};
    stream.read(uuid.m_bytes.data(), uuid.m_bytes.size());
    return uuid;
}

NodeAttribute LSFReader::readAttribute(AttributeType type, Stream& reader)
{
    NodeAttribute attr(type);

    switch (type) {
    case None:
        break;
    case Byte:
        attr.setValue(reader.read<uint8_t>());
        break;
    case Int8:
        attr.setValue(reader.read<int8_t>());
        break;
    case Short:
        attr.setValue(reader.read<int16_t>());
        break;
    case UShort:
        attr.setValue(reader.read<uint16_t>());
        break;
    case Int:
        attr.setValue(reader.read<int32_t>());
        break;
    case UInt:
        attr.setValue(reader.read<uint32_t>());
        break;
    case Long:
        attr.setValue(reader.read<int64_t>());
        break;
    case Int64:
        attr.setValue(reader.read<int64_t>());
        break;
    case ULongLong:
        attr.setValue(reader.read<uint64_t>());
        break;
    case Float:
        attr.setValue(reader.read<float>());
        break;
    case Double:
        attr.setValue(reader.read<double>());
        break;
    case Bool:
        attr.setValue(reader.read<uint8_t>());
        break;
    case Uuid:
        attr.setValue(readUUID(reader));
        break;
    default:
        throw Exception("Unsupported attribute type.");
    }

    return attr;
}

void LSFReader::readNode(const LSFNodeInfo& defn, LSNode& node, Stream& attributeReader)
{
    node.name = m_names[defn.nameIndex][defn.nameOffset];

    if (defn.firstAttributeIndex != -1) {
        auto attribute = m_attributes[defn.firstAttributeIndex];
        while (true) {
            m_values.seek(attribute.dataOffset, SeekMode::Begin);
            auto value = readAttribute(static_cast<AttributeType>(attribute.typeId), attributeReader, attribute.length);

            node.attributes[m_names[attribute.nameIndex][attribute.nameOffset]] = value;

            if (attribute.nextAttributeIndex == -1) {
                break;
            }

            attribute = m_attributes[attribute.nextAttributeIndex];
        }
    }
}

void LSFReader::readRegions(const Resource::Ptr& resource)
{
    auto& attrReader = m_values;

    m_nodeInstances.clear();

    for (const auto& defn : m_nodes) {
        if (defn.parentIndex == -1) {
            auto region = std::make_shared<Region>();
            readNode(defn, *region, attrReader);
            region->keyAttribute = defn.keyAttribute;
            m_nodeInstances.emplace_back(region);
            region->regionName = region->name;
            resource->regions[region->regionName] = std::move(region);
        } else {
            auto node = std::make_shared<LSNode>();
            readNode(defn, *node, attrReader);
            node->keyAttribute = defn.keyAttribute;
            node->parent = m_nodeInstances[defn.parentIndex];
            m_nodeInstances.emplace_back(node);
            m_nodeInstances[defn.parentIndex]->children[node->name].emplace_back(node);
        }
    }
}

std::string LSFReader::readString(Stream& stream, uint32_t length) const
{
    auto s = stream.read(length).str();
    if (!s.empty() && s.back() == '\0') {
        s.pop_back();
    }

    return s;
}

Stream LSFReader::decompress(uint32_t sizeOnDisk, uint32_t uncompressedSize, const std::string& debugDumpTo,
                             bool allowChunked)
{
    if (sizeOnDisk == 0 && uncompressedSize != 0) {
        auto stream = m_stream.read(uncompressedSize);
        return stream;
    }

    if (sizeOnDisk == 0 || uncompressedSize == 0) {
        return {}; // no data
    }

    auto chunked = m_version >= LSFVersion::CHUNKED_COMPRESS && allowChunked;
    auto isCompressed = m_metadata.compressionMethod() != CompressionMethod::NONE;
    auto compressedSize = isCompressed ? sizeOnDisk : uncompressedSize;
    auto compressedStream = m_stream.read(compressedSize);
    auto uncompressedStream = Compression::decompress(m_metadata.compressionMethod(), compressedStream,
        uncompressedSize, chunked);

    return uncompressedStream;
}
