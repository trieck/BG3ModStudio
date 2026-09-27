#include "pch.h"
#include "Exception.h"
#include "FileStream.h"
#include "GR2Reader.h"

namespace { // anonymous

bool rangeWithin(size_t offset, size_t length, size_t size)
{
    return offset <= size && length <= size - offset;
}

// Magic value used for version 7 little-endian 32-bit Granny files
constexpr uint8_t LittleEndian32Magic[] = {
    0x29, 0xDE, 0x6C, 0xC0, 0xBA, 0xA4, 0x53, 0x2B,
    0x25, 0xF5, 0xB7, 0xA5, 0xF6, 0x66, 0xE2, 0xEE
};

// Magic value used for version 7 little-endian 32-bit Granny files
constexpr uint8_t LittleEndian32Magic2[] = {
    0x29, 0x75, 0x31, 0x82, 0xBA, 0x02, 0x11, 0x77,
    0x25, 0x3A, 0x60, 0x2F, 0xF6, 0x6A, 0x8C, 0x2E
};

// Magic value used for version 6 little-endian 32-bit Granny files
constexpr uint8_t LittleEndian32MagicV6[] = {
    0xB8, 0x67, 0xB0, 0xCA, 0xF8, 0x6D, 0xB1, 0x0F,
    0x84, 0x72, 0x8C, 0x7E, 0x5E, 0x19, 0x00, 0x1E
};

// Magic value used for version 7 big-endian 32-bit Granny files
constexpr uint8_t BigEndian32Magic[] = {
    0x0E, 0x11, 0x95, 0xB5, 0x6A, 0xA5, 0xB5, 0x4B,
    0xEB, 0x28, 0x28, 0x50, 0x25, 0x78, 0xB3, 0x04
};

// Magic value used for version 7 big-endian 32-bit Granny files
constexpr uint8_t BigEndian32Magic2[] = {
    0x0E, 0x74, 0xA2, 0x0A, 0x6A, 0xEB, 0xEB, 0x64,
    0xEB, 0x4E, 0x1E, 0xAB, 0x25, 0x91, 0xDB, 0x8F
};

// Magic value used for version 7 little-endian 64-bit Granny files
constexpr uint8_t LittleEndian64Magic[] = {
    0xE5, 0x9B, 0x49, 0x5E, 0x6F, 0x63, 0x1F, 0x14,
    0x1E, 0x13, 0xEB, 0xA9, 0x90, 0xBE, 0xED, 0xC4
};

// Magic value used for version 7 little-endian 64-bit Granny files
constexpr uint8_t LittleEndian64Magic2[] = {
    0xE5, 0x2F, 0x4A, 0xE1, 0x6F, 0xC2, 0x8A, 0xEE,
    0x1E, 0xD2, 0xB4, 0x4C, 0x90, 0xD7, 0x55, 0xAF
};

// Magic value used for version 7 big-endian 64-bit Granny files
constexpr uint8_t BigEndian64Magic[] = {
    0x31, 0x95, 0xD4, 0xE3, 0x20, 0xDC, 0x4F, 0x62,
    0xCC, 0x36, 0xD0, 0x3A, 0xB1, 0x82, 0xFF, 0x89
};

// Magic value used for version 7 big-endian 64-bit Granny files
constexpr uint8_t BigEndian64Magic2[] = {
    0x31, 0xC2, 0x4E, 0x7C, 0x20, 0x40, 0xA3, 0x25,
    0xCC, 0xE1, 0xC2, 0x7A, 0xB1, 0x32, 0x49, 0xF3
};

struct MagicFormat
{
    GR2FileFormat format;
    const uint8_t* magic;
};

const MagicFormat MagicFormats[] = {
    {.format = LITTLE_ENDIAN_32, .magic = LittleEndian32Magic},
    {.format = LITTLE_ENDIAN_32, .magic = LittleEndian32Magic2},
    {.format = LITTLE_ENDIAN_32, .magic = LittleEndian32MagicV6},
    {.format = BIG_ENDIAN_32, .magic = BigEndian32Magic},
    {.format = BIG_ENDIAN_32, .magic = BigEndian32Magic2},
    {.format = LITTLE_ENDIAN_64, .magic = LittleEndian64Magic},
    {.format = LITTLE_ENDIAN_64, .magic = LittleEndian64Magic2},
    {.format = BIG_ENDIAN_64, .magic = BigEndian64Magic},
    {.format = BIG_ENDIAN_64, .magic = BigEndian64Magic2},
};
} // anonymous namespace

void GR2Reader::load(const char* filename, const GR2Callback& callback)
{
    read(filename);
    build(callback);
}

void GR2Reader::load(const ByteBuffer& buffer, const GR2Callback& callback)
{
    read(buffer);
    build(callback);
}

void GR2Reader::read(const char* filename)
{
    FileStream stream;
    stream.open(filename, "rb");
    m_data = stream.read();

    readHeader();
    readSections();
    makeRootStream();
}

void GR2Reader::read(const ByteBuffer& buffer)
{
    m_data.first = std::make_unique<uint8_t[]>(buffer.second);
    memcpy(m_data.first.get(), buffer.first.get(), buffer.second);
    m_data.second = buffer.second;

    readHeader();
    readSections();
    makeRootStream();
}

bool GR2Reader::isValid(const GR2TypeNode* node) const
{
    return node != nullptr && node->type > TYPE_NONE && node->type <= TYPE_MAX;
}

void GR2Reader::build(const GR2Callback& callback)
{
    auto* node = get<GR2TypeNode*>(m_header.type);
    while (isValid(node)) {
        build(node, nullptr, 0, callback);
        node++;
    }
}

const std::vector<GR2Object::Ptr>& GR2Reader::rootObjects() const
{
    return m_rootObjects;
}

bool GR2Reader::isGR2(FileStream& stream)
{
    const auto offset = stream.tell();
    stream.seek(0, SeekMode::Begin);

    GR2Header header{};
    stream.read(reinterpret_cast<char*>(&header), sizeof(GR2Header));
    stream.seek(static_cast<int64_t>(offset), SeekMode::Begin);

    GR2FileFormat format{UNKNOWN_FORMAT};

    for (const auto& fmt : MagicFormats) {
        if (memcmp(header.signature, fmt.magic, sizeof(header.signature)) == 0) {
            format = fmt.format;
            break;
        }
    }

    if (format == UNKNOWN_FORMAT) {
        return false;
    }

    if (format == BIG_ENDIAN_32 || format == BIG_ENDIAN_64) {
        return false;
    }

    return true;
}

bool GR2Reader::isGR2(const ByteBuffer& contents)
{
    if (contents.second < sizeof(GR2Header)) {
        return false;
    }

    GR2Header header{};
    memcpy(&header, contents.first.get(), sizeof(GR2Header));

    GR2FileFormat format{ UNKNOWN_FORMAT };

    for (const auto& fmt : MagicFormats) {
        if (memcmp(header.signature, fmt.magic, sizeof(header.signature)) == 0) {
            format = fmt.format;
            break;
        }
    }

    if (format == UNKNOWN_FORMAT) {
        return false;
    }

    if (format == BIG_ENDIAN_32 || format == BIG_ENDIAN_64) {
        return false;
    }

    return true;
}

GR2Object::Ptr GR2Reader::build(const GR2TypeNode* node, const GR2Object::Ptr& parent, uint32_t level,
                                const GR2Callback& callback)
{
    if (!isValid(node)) {
        return nullptr;
    }

    auto obj = makeObject(node, parent, level, callback);

    if (!isArrayType(*node)) { // Arrays handle their own fields and callbacks
        if (callback) {
            callback({.object = obj, .level = level});
        }

        auto* fields = resolve<GR2TypeNode*>(node->fields);
        buildFields(fields, obj, level + 1, callback);
    }

    return obj;
}

void GR2Reader::buildFields(const GR2TypeNode* fields, const GR2Object::Ptr& parent, uint32_t level,
                            const GR2Callback& callback)
{
    while (isValid(fields)) {
        build(fields, parent, level, callback);
        fields++;
    }
}

void GR2Reader::makeRootStream()
{
    const auto& root = m_header.root;
    auto* base = m_data.first.get() + m_sectionHeaders[root.section].dataOffset + root.offset;

    m_rootStream = GR2Stream(base);
}

GR2RefStream GR2Reader::getStream(const GR2Object::Ptr& parent)
{
    // A node's data pointer is interpreted relative to its parent's instance stream.
    auto* data = parent == nullptr ? &m_rootStream.data() : &parent->data;

    return GR2RefStream(data);
}

bool GR2Reader::isArrayType(const GR2TypeNode& node) const
{
    return node.type == TYPE_ARRAY_OF_REFERENCES || node.type == TYPE_REFERENCE_TO_ARRAY ||
        node.type == TYPE_REFERENCE_TO_VARIANT_ARRAY;
}

GR2Object::Ptr GR2Reader::makeObject(const GR2TypeNode* node, const GR2Object::Ptr& parent, uint32_t level,
                                     const GR2Callback& callback)
{
    GR2Object::Ptr obj = nullptr;

    switch (node->type) {
    case TYPE_INLINE:
        obj = makeInline(node, parent);
        break;
    case TYPE_REFERENCE:
        obj = makeReference(node, parent);
        break;
    case TYPE_VARIANT_REFERENCE:
        obj = makeVarReference(node, parent, level, callback);
        break;
    case TYPE_ARRAY_OF_REFERENCES:
        obj = makeArrayReference(node, parent, level, callback);
        break;
    case TYPE_REFERENCE_TO_ARRAY:
        obj = makeReferenceArray(node, parent, level, callback);
        break;
    case TYPE_REFERENCE_TO_VARIANT_ARRAY:
        obj = makeReferenceVarArray(node, parent, level, callback);
        break;
    case TYPE_STRING:
        obj = makeString(node, parent);
        break;
    case TYPE_INT8:
    case TYPE_BINORMAL_INT8:
        obj = makeInt8(node, parent);
        break;
    case TYPE_UINT8:
    case TYPE_NORMAL_UINT8:
        obj = makeUInt8(node, parent);
        break;
    case TYPE_INT16:
    case TYPE_BINORMAL_INT16:
        obj = makeInt16(node, parent);
        break;
    case TYPE_UINT16:
    case TYPE_NORMAL_UINT16:
    case TYPE_REAL16:
        obj = makeUInt16(node, parent);
        break;
    case TYPE_INT32:
        obj = makeInt32(node, parent);
        break;
    case TYPE_UINT32:
        obj = makeUInt32(node, parent);
        break;
    case TYPE_REAL32:
        obj = makeFloat(node, parent);
        break;
    case TYPE_TRANSFORM:
        obj = makeTransform(node, parent);
        break;
    default:
        throw Exception(std::format("Unsupported GR2 node type: {}", static_cast<uint32_t>(node->type)));
    }

    if (parent == nullptr) {
        m_rootObjects.emplace_back(obj);
    } else {
        parent->children.emplace_back(obj);
    }

    return obj;
}

GR2Object::Ptr GR2Reader::makeReference(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_REFERENCE);

    auto stream = getStream(parent);

    auto obj = std::make_shared<GR2Object>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);
    obj->data = resolve<uint8_t*>(stream);

    return obj;
}

GR2Object::Ptr GR2Reader::makeReferenceArray(const GR2TypeNode* node, const GR2Object::Ptr& parent, uint32_t level,
                                             const GR2Callback& callback)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_REFERENCE_TO_ARRAY);

    auto obj = std::make_shared<GR2ReferenceArray>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto stream = getStream(parent);
    if (!stream.isNull()) {
        obj->size = stream.read<uint32_t>();
        obj->data = resolve<uint8_t*>(stream);
    }

    if (callback) {
        callback({.object = obj, .level = level});
    }

    auto* fields = resolve<GR2TypeNode*>(node->fields);
    if (!isValid(fields)) {
        return obj;
    }

    for (auto i = 0u; i < obj->size; ++i) {
        buildFields(fields, obj, level + 1, callback);
    }

    return obj;
}

GR2Object::Ptr GR2Reader::makeReferenceVarArray(const GR2TypeNode* node, const GR2Object::Ptr& parent, uint32_t level,
                                                const GR2Callback& callback)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_REFERENCE_TO_VARIANT_ARRAY);

    auto obj = std::make_shared<GR2ReferenceVarArray>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto stream = getStream(parent);
    if (!stream.isNull()) {
        if (is64Bit()) {
            obj->typePtr = stream.read<uint64_t>();
        } else {
            obj->typePtr = stream.read<uint32_t>();
        }

        obj->size = stream.read<uint32_t>();
        obj->data = resolve<uint8_t*>(stream);
    }

    if (callback) {
        callback({.object = obj, .level = level});
    }

    auto* fields = resolve<GR2TypeNode*>(obj->typePtr);
    if (!isValid(fields)) {
        return obj;
    }

    for (auto i = 0u; i < obj->size; ++i) {
        buildFields(fields, obj, level + 1, callback);
    }

    return obj;
}

GR2Object::Ptr GR2Reader::makeString(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_STRING);

    auto stream = getStream(parent);

    auto obj = std::make_shared<GR2String>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);
    obj->data = resolve<uint8_t*>(stream);
    if (obj->data == nullptr) {
        return obj;
    }

    obj->value = reinterpret_cast<const char*>(obj->data);

    return obj;
}

GR2Object::Ptr GR2Reader::makeTransform(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_TRANSFORM);

    auto obj = std::make_shared<GR2Transform>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto stream = getStream(parent);
    if (stream.isNull()) {
        return obj;
    }

    obj->transform = stream.read<GR2TransformData>();
    obj->data = reinterpret_cast<uint8_t*>(&obj->transform);

    return obj;
}

GR2Object::Ptr GR2Reader::makeUInt16(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_UINT16 || node->type == TYPE_NORMAL_UINT16 || node->type == TYPE_REAL16);

    auto obj = std::make_shared<GR2UInt16>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto count = node->arraySize != 0 ? node->arraySize : 1;
    if (count == 0) {
        return obj;
    }

    auto stream = getStream(parent);
    if (stream.isNull()) {
        return obj;
    }

    obj->values.resize(count);
    for (auto i = 0; i < count; ++i) {
        obj->values[i] = stream.read<uint16_t>();
    }

    return obj;
}

GR2Object::Ptr GR2Reader::makeVarReference(const GR2TypeNode* node, const GR2Object::Ptr& parent, uint32_t level,
                                           const GR2Callback& callback)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_VARIANT_REFERENCE);

    auto obj = std::make_shared<GR2VarReference>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto stream = getStream(parent);
    if (stream.isNull()) {
        return obj;
    }

    if (is64Bit()) {
        obj->typePtr = stream.read<uint64_t>();
        obj->objPtr = stream.read<uint64_t>();
    } else {
        obj->typePtr = stream.read<uint32_t>();
        obj->objPtr = stream.read<uint32_t>();
    }

    auto* typeNode = resolve<GR2TypeNode*>(obj->typePtr);
    obj->data = resolve<uint8_t*>(obj->objPtr);

    if (callback) {
        callback({.object = obj, .level = level});
    }

    buildFields(typeNode, obj, level + 1, callback);

    return obj;
}

GR2Object::Ptr GR2Reader::makeArrayReference(const GR2TypeNode* node, const GR2Object::Ptr& parent, uint32_t level,
                                             const GR2Callback& callback)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_ARRAY_OF_REFERENCES);

    auto obj = std::make_shared<GR2ArrayReference>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto stream = getStream(parent);
    if (stream.isNull()) {
        return obj;
    }

    auto size = stream.read<uint32_t>();

    obj->data = resolve<uint8_t*>(stream);

    if (callback) {
        callback({.object = obj, .level = level});
    }

    if (obj->data == nullptr) {
        return obj;
    }

    auto* fields = resolve<GR2TypeNode*>(node->fields);
    if (!isValid(fields)) {
        return obj;
    }

    if (is64Bit()) {
        return processArrayReference<uint64_t>(obj, size, fields, level, callback);
    }

    return processArrayReference<uint32_t>(obj, size, fields, level, callback);
}

GR2Object::Ptr GR2Reader::makeInt16(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_INT16 || node->type == TYPE_BINORMAL_INT16);

    auto obj = std::make_shared<GR2Int16>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto count = node->arraySize != 0 ? node->arraySize : 1;
    if (count == 0) {
        return obj;
    }

    auto stream = getStream(parent);
    if (stream.isNull()) {
        return obj;
    }

    obj->values.resize(count);

    for (auto i = 0; i < count; ++i) {
        obj->values[i] = stream.read<int16_t>();
    }

    return obj;
}

GR2Object::Ptr GR2Reader::makeInt32(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_INT32);

    auto obj = std::make_shared<GR2Int32>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto count = node->arraySize != 0 ? node->arraySize : 1;
    if (count == 0) {
        return obj;
    }

    auto stream = getStream(parent);
    if (stream.isNull()) {
        return obj;
    }

    obj->values.resize(count);

    for (auto i = 0; i < count; ++i) {
        obj->values[i] = stream.read<int32_t>();
    }

    return obj;
}

GR2Object::Ptr GR2Reader::makeInt8(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_INT8 || node->type == TYPE_BINORMAL_INT8);

    auto obj = std::make_shared<GR2Int8>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto count = node->arraySize != 0 ? node->arraySize : 1;
    if (count == 0) {
        return obj;
    }

    auto stream = getStream(parent);
    if (stream.isNull()) {
        return obj;
    }

    obj->values.resize(count);

    for (auto i = 0; i < count; ++i) {
        obj->values[i] = stream.read<int8_t>();
    }

    return obj;
}

GR2Object::Ptr GR2Reader::makeUInt8(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_UINT8 || node->type == TYPE_NORMAL_UINT8);

    auto obj = std::make_shared<GR2UInt8>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto count = node->arraySize != 0 ? node->arraySize : 1;
    if (count == 0) {
        return obj;
    }

    auto stream = getStream(parent);
    if (stream.isNull()) {
        return obj;
    }

    obj->values.resize(count);

    for (auto i = 0; i < count; ++i) {
        obj->values[i] = stream.read<uint8_t>();
    }

    return obj;
}

GR2Object::Ptr GR2Reader::makeUInt32(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_UINT32);
    auto obj = std::make_shared<GR2UInt32>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto count = node->arraySize != 0 ? node->arraySize : 1;
    if (count == 0) {
        return obj;
    }

    auto stream = getStream(parent);
    if (stream.isNull()) {
        return obj;
    }

    obj->values.resize(count);
    for (auto i = 0; i < count; ++i) {
        obj->values[i] = stream.read<uint32_t>();
    }

    return obj;
}

GR2Object::Ptr GR2Reader::makeFloat(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_REAL32);

    auto obj = std::make_shared<GR2Float>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto count = node->arraySize != 0 ? node->arraySize : 1;
    if (count == 0) {
        return obj;
    }

    auto stream = getStream(parent);
    if (stream.isNull()) {
        return obj;
    }

    obj->values.resize(count);

    for (auto i = 0; i < count; ++i) {
        obj->values[i] = stream.read<float>();
    }

    return obj;
}

GR2Object::Ptr GR2Reader::makeInline(const GR2TypeNode* node, const GR2Object::Ptr& parent)
{
    ATLASSERT(isValid(node));
    ATLASSERT(node->type == TYPE_INLINE);

    auto obj = std::make_shared<GR2Object>();
    obj->typeNode = node;
    obj->parent = parent;
    obj->name = resolve<char*>(node->name);

    auto stream = getStream(parent);
    obj->data = resolve<uint8_t*>(stream);

    return obj;
}

bool GR2Reader::is64Bit() const
{
    return m_format == LITTLE_ENDIAN_64 || m_format == BIG_ENDIAN_64;
}

GR2FileFormat GR2Reader::readFormat(const GR2Header& header)
{
    GR2FileFormat format{UNKNOWN_FORMAT};

    for (const auto& fmt : MagicFormats) {
        if (memcmp(header.signature, fmt.magic, sizeof(header.signature)) == 0) {
            format = fmt.format;
            break;
        }
    }

    if (format == UNKNOWN_FORMAT) {
        throw Exception("Invalid GR2 file: unrecognized magic signature.");
    }

    if (format == BIG_ENDIAN_32 || format == BIG_ENDIAN_64) {
        throw Exception("Big-endian GR2 files are not supported.");
    }

    return format;
}

void GR2Reader::readHeader()
{
    if (!rangeWithin(0, sizeof(GR2Header), m_data.second)) {
        throw Exception("Invalid GR2 file: truncated header.");
    }

    memcpy(&m_header, m_data.first.get(), sizeof(GR2Header));
    m_format = readFormat(m_header);
}

void GR2Reader::readSections()
{
    if (static_cast<size_t>(m_header.numSections) >
        std::numeric_limits<size_t>::max() / sizeof(GR2SectionHeader)) {
        throw Exception("Invalid GR2 file: section count overflow.");
    }
    const auto sectionTableSize = static_cast<size_t>(m_header.numSections) * sizeof(GR2SectionHeader);
    // sectionOffset is relative to the header following the 32-byte magic block.
    constexpr auto magicSize = offsetof(GR2Header, version);
    if (!rangeWithin(magicSize, m_header.sectionOffset, m_data.second)) {
        throw Exception("Invalid GR2 file: section table offset is out of bounds.");
    }
    const auto sectionTableOffset = magicSize + static_cast<size_t>(m_header.sectionOffset);
    if (!rangeWithin(sectionTableOffset, sectionTableSize, m_data.second)) {
        throw Exception("Invalid GR2 file: truncated section table.");
    }

    m_sectionHeaders = std::vector<GR2SectionHeader>(m_header.numSections);

    auto* pbase = m_data.first.get();
    auto* pdata = pbase + sectionTableOffset;

    auto totalDataSize = 0u;
    for (auto& section : m_sectionHeaders) {
        memcpy(&section, pdata, sizeof(GR2SectionHeader));
        totalDataSize += section.decompressedLen;
        pdata += sizeof(GR2SectionHeader);

        if (!rangeWithin(section.dataOffset, section.compressedLen, m_data.second)) {
            throw Exception("Invalid GR2 file: section data is out of bounds.");
        }
        if (section.compressType == COMPRESSION_NONE &&
            !rangeWithin(section.dataOffset, section.decompressedLen, m_data.second)) {
            throw Exception("Invalid GR2 file: uncompressed section data is out of bounds.");
        }
        if (section.fixupSize > std::numeric_limits<uint32_t>::max() / sizeof(GR2FixUp)) {
            throw Exception("Invalid GR2 file: fixup size overflow.");
        }
        const auto fixupLength = static_cast<size_t>(section.fixupSize) * sizeof(GR2FixUp);
        if (section.fixupSize > 0 && !rangeWithin(section.fixupOffset,
                                                    section.compressType != COMPRESSION_NONE
                                                        ? sizeof(uint32_t)
                                                        : fixupLength,
                                                    m_data.second)) {
            throw Exception("Invalid GR2 file: fixup data is out of bounds.");
        }
    }

    Stream stream;
    stream.write(reinterpret_cast<const char*>(&m_header), sizeof(GR2Header));
    stream.write(reinterpret_cast<const char*>(m_sectionHeaders.data()),
                 m_sectionHeaders.size() * sizeof(GR2SectionHeader));

    // read all sections
    uint32_t outputOffset = static_cast<uint32_t>(sizeof(GR2Header) + m_sectionHeaders.size() * sizeof(
        GR2SectionHeader));

    for (auto& section : m_sectionHeaders) {
        if (section.decompressedLen == 0) {
            continue;
        }

        auto inputOffset = section.dataOffset;
        section.dataOffset = outputOffset;

        if (section.compressType != COMPRESSION_NONE) {
            auto* compressedBuffer = pbase + inputOffset;
            auto compressedLen = section.compressedLen;
            auto decompressedBuffer = std::make_unique<uint8_t[]>(section.decompressedLen);
            auto decompressedLen = section.decompressedLen;

            auto result = m_decompressor.Decompress(section.compressType,
                                                    compressedLen,
                                                    compressedBuffer,
                                                    decompressedBuffer.get(),
                                                    section.oodleStop0, section.oodleStop1,
                                                    decompressedLen);

            if (!result) {
                throw Exception("Failed to decompress GR2 section.");
            }

            stream.write(decompressedBuffer.get(), section.decompressedLen);

            if (section.fixupSize > 0) {
                auto* fixupData = pbase + section.fixupOffset;
                auto compressedFixupSize = *reinterpret_cast<uint32_t*>(fixupData);

                auto* compressedFixupBuffer = fixupData + sizeof(uint32_t);
                if (!rangeWithin(static_cast<size_t>(section.fixupOffset) + sizeof(uint32_t),
                                 compressedFixupSize, m_data.second)) {
                    throw Exception("Invalid GR2 file: compressed fixup data is out of bounds.");
                }

                auto decompressedFixupSize = static_cast<uint32_t>(section.fixupSize * sizeof(GR2FixUp));
                auto decompressedFixupBuffer = std::make_unique<uint8_t[]>(decompressedFixupSize);
                result = m_decompressor.Decompress(section.compressType,
                                                   compressedFixupSize,
                                                   compressedFixupBuffer,
                                                   decompressedFixupBuffer.get(),
                                                   section.oodleStop0, section.oodleStop1,
                                                   decompressedFixupSize);
                if (!result) {
                    throw Exception("Failed to decompress GR2 fixup section.");
                }

                section.fixupOffset = outputOffset + section.decompressedLen;
                stream.write(decompressedFixupBuffer.get(), decompressedFixupSize);
                outputOffset += decompressedFixupSize;
            }
        } else {
            // uncompressed, copy data as-is
            auto* src = pbase + inputOffset;
            stream.write(src, section.decompressedLen);
            if (section.fixupSize > 0) {
                auto* fixupData = pbase + section.fixupOffset;
                section.fixupOffset = outputOffset + section.decompressedLen;
                auto fixupLen = static_cast<uint32_t>(section.fixupSize * sizeof(GR2FixUp));
                stream.write(fixupData, fixupLen);
                outputOffset += fixupLen;
            }
        }
        outputOffset += section.decompressedLen;
    }

    // synchronize the stream data with the updated section headers
    stream.seek(sizeof(GR2Header), SeekMode::Begin);
    stream.write(reinterpret_cast<const char*>(m_sectionHeaders.data()),
                 m_sectionHeaders.size() * sizeof(GR2SectionHeader));

    m_data = stream.detach();
    pbase = m_data.first.get();

    // TODO: marshalling

    // make fixup table and apply fixups
    for (auto i = 0u; i < m_sectionHeaders.size(); ++i) {
        const auto& section = m_sectionHeaders[i];
        for (auto j = 0u; j < section.fixupSize; ++j) {
            auto* fixup = reinterpret_cast<GR2FixUp*>(pbase + section.fixupOffset + j * sizeof(GR2FixUp));
            addFixup(i, *fixup);
        }
    }
}

void GR2Reader::addFixup(uint32_t srcSection, GR2FixUp& fixup)
{
    // Bounds check source section index
    if (srcSection >= m_sectionHeaders.size()) {
        throw Exception("GR2 fixup: invalid source section");
    }

    // Bounds check destination section index
    if (fixup.dstSection >= m_sectionHeaders.size()) {
        throw Exception("GR2 fixup: invalid destination section");
    }

    auto pdata = m_data.first.get();
    auto* srcPtr = pdata + m_sectionHeaders[srcSection].dataOffset + fixup.srcOffset;
    auto* dstPtr = pdata + m_sectionHeaders[fixup.dstSection].dataOffset + fixup.dstOffset;

    m_fixups.emplace_back(dstPtr);
    auto index = static_cast<uint32_t>(m_fixups.size()); // 1-based index

    if (is64Bit()) {
        uint64_t index64 = index;
        memcpy(srcPtr, &index64, sizeof(uint64_t));
    } else {
        memcpy(srcPtr, &index, sizeof(uint32_t));
    }
}
