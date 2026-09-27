#include "pch.h"

#include "Compress.h"
#include "CRC32.h"
#include "LZ4Codec.h"
#include "MD5.h"
#include "PAKWriter.h"
#include "Stream.h"

#include <filesystem>
namespace fs = std::filesystem;

PAKWriter::PAKWriter(PackageBuildData build, const char* packagePath, ProgressCallback cb)
    : m_build(std::move(build)), m_packagePath(packagePath), m_cb(std::move(cb))
{
    m_metadata.version = static_cast<uint32_t>(m_build.version);
    m_metadata.flags = m_build.flags;
    m_metadata.priority = m_build.priority;
}

void PAKWriter::write()
{
    m_stream.open(m_packagePath.c_str(), "wb");

    m_stream.write<uint32_t>(PAK_MAGIC);

    auto header = LSPKHeader16::fromCommon(m_metadata);
    m_stream.write<LSPKHeader16>(header);

    auto writtenFiles = packFiles();

    m_metadata.fileListOffset = m_stream.tell();

    writeCompressedFileList(writtenFiles);

    m_metadata.fileListSize = static_cast<uint32_t>(m_stream.tell() - m_metadata.fileListOffset);

    if (m_build.hash) {
        archiveHash(m_metadata.md5);
    } else {
        memset(m_metadata.md5, 0, 16);
    }

    m_metadata.numParts = 1; // Pretend we know the number of parts

    m_stream.seek(4, SeekMode::Begin);

    header = LSPKHeader16::fromCommon(m_metadata);
    m_stream.write<LSPKHeader16>(header);
    m_stream.close();
}

void PAKWriter::writeCompressedFileList(const std::vector<PackagedFileInfoCommon>& files)
{
    Stream fileList;

    for (const auto& file : files) {
        auto entry = FileEntry18::fromCommon(file);
        fileList.write<FileEntry18>(entry);
    }

    auto compressedFileList = LZ4Codec::encode(fileList, 0, fileList.size());

    m_stream.write<uint32_t>(static_cast<uint32_t>(files.size()));

    if (m_build.version > PackageVersion::V13) {
        m_stream.write<uint32_t>(static_cast<uint32_t>(compressedFileList.size()));
    } else {
        m_metadata.fileListSize = static_cast<uint32_t>(compressedFileList.size() + 4);
    }

    auto [data, size] = compressedFileList.detach();

    m_stream.write(data.get(), size);
}

void PAKWriter::archiveHash(uint8_t digest[16])
{
    // MD5 is computed over the contents of all files in an alphabetically sorted order
    std::ranges::sort(m_build.files, [](const PackageBuildInputFile& a, const PackageBuildInputFile& b) {
        return std::ranges::lexicographical_compare(a.name, b.name);
    });

    MD5 md5;
    for (const auto& file : m_build.files) {
        FileStream input;
        input.open(file.filename.c_str(), "rb");

        auto buffer = input.read(input.size()).detach();
        md5.update(buffer.first.get(), static_cast<uint32_t>(buffer.second));
    }

    md5.finalize(digest);
}

std::vector<PackagedFileInfoCommon> PAKWriter::packFiles()
{
    std::vector<PackagedFileInfoCommon> writtenFiles;
    writtenFiles.reserve(m_build.files.size());

    for (size_t i = 0; i < m_build.files.size(); i++) {
        const auto& file = m_build.files[i];
        writtenFiles.emplace_back(writeFile(file));

        if (m_cb) {
            m_cb(i + 1, m_build.files.size(), file.name);
        }
    }

    return writtenFiles;
}

void PAKWriter::writePadding()
{
    int padLength;
    if (m_build.version <= PackageVersion::V9) {
        padLength = 0x1000;
    } else {
        padLength = 0x40;
    }

    size_t alignTo;
    if (m_build.version >= PackageVersion::V16) {
        alignTo = m_stream.tell() - sizeof(LSPKHeader16) - 4;
    } else {
        alignTo = m_stream.tell();
    }

    auto padByteLen = padLength - (alignTo % padLength) % padLength;

    uint8_t padByte = 0xAD;

    for (auto i = 0ul; i < padByteLen; i++) {
        m_stream.write(&padByte, 1);
    }
}

bool PAKWriter::canCompressFile(const PackageBuildInputFile& inputFile)
{
    if (m_build.compression == CompressionMethod::NONE) {
        return false;
    }

    fs::path p(inputFile.filename);
    auto ext = p.extension().string();
    std::ranges::transform(ext, ext.begin(), tolower);

    static const std::unordered_set<std::string> noCompressExts = {
        ".gts", ".gtp", ".wem", ".bnk"
    };

    if (noCompressExts.contains(ext)) {
        return false;
    }

    return true;
}

PackagedFileInfoCommon PAKWriter::writeFile(const PackageBuildInputFile& inputFile)
{
    FileStream input;
    input.open(inputFile.filename.c_str(), "rb");

    auto method = m_build.compression;
    auto level = m_build.compressionLevel;

    if (!canCompressFile(inputFile)) {
        method = CompressionMethod::NONE;
        level = LSCompressionLevel::DEFAULT;
    }

    auto size = input.size();
    auto data = input.read(size).detach();

    PackagedFileInfoCommon packaged{};
    packaged.name = inputFile.name;
    packaged.uncompressedSize = static_cast<uint32_t>(size);
    packaged.sizeOnDisk = static_cast<uint32_t>(size);
    packaged.archivePart = 0;
    packaged.offsetInFile = m_stream.tell();
    packaged.flags = Compression::compressionFlags(method, level);

    if (method != CompressionMethod::NONE) {
        auto compressed = Compression::compress(method, data.first.get(), size, level);
        packaged.sizeOnDisk = static_cast<uint32_t>(compressed.size());
        m_stream.write(compressed);
    } else {
        m_stream.write(data.first.get(), data.second);
    }

    if (m_build.version >= PackageVersion::V10 && m_build.version <= PackageVersion::V16) {
        packaged.crc = CRC32::compute(data.first.get(), data.second);
    }

    if (static_cast<uint8_t>(m_build.flags) & static_cast<uint8_t>(PackageFlags::Solid)) {
        writePadding();
    }

    input.close();

    return packaged;
}

void PAKWriter::close()
{
    m_stream.close();
}
