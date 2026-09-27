#include "pch.h"
#include "Compress.h"
#include "Exception.h"
#include "LZ4Compressor.h"
#include "PAKReader.h"
#include "Stream.h"

namespace { // anonymous namespace

template <typename TFile>
bool readStructs(Stream& stream, std::vector<TFile>& entries, uint32_t numFiles)
{
    entries.resize(numFiles);

    for (uint32_t i = 0; i < numFiles; ++i) {
        entries[i] = stream.read<TFile>();
    }

    return true;
}

template <typename TFileEntry>
PackagedFileInfo createFromEntry(Package& package, const TFileEntry& entry)
{
    PackagedFileInfo info;

    info.name = entry.name;
    info.archivePart = entry.archivePart;
    info.crc = 0;
    info.flags = static_cast<CompressionFlags>(entry.flags);
    info.offsetInFile = entry.offsetInFile1 | static_cast<uint64_t>(entry.offsetInFile2) << 32;
    info.sizeOnDisk = entry.sizeOnDisk;
    info.uncompressedSize = entry.uncompressedSize;

    return info;
}

template <typename TFileEntry>
void readCompressedFileList(PAKReader& reader, int64_t offset)
{
    auto& package = reader.package();

    package.seek(offset, SeekMode::Begin);
    auto numFiles = package.read<uint32_t>();

    UInt8Ptr compressed;
    uint32_t compressedSize;

    if (reader.package().m_header.version > 13) {
        package.seek(offset + 4, SeekMode::Begin);
        compressedSize = package.read<uint32_t>();

        compressed = std::make_unique<uint8_t[]>(compressedSize);

        package.read(compressed.get(), compressedSize);
    } else {
        compressedSize = reader.package().m_header.fileListSize - 4;
        compressed = std::make_unique<uint8_t[]>(compressedSize);

        package.read(compressed.get(), compressedSize);
    }

    const uint32_t fileBufferSize = sizeof(TFileEntry) * numFiles;

    LZ4Compressor compressor;
    auto decompressed = compressor.decompress(compressed.get(), compressedSize, fileBufferSize);

    std::vector<TFileEntry> entries(numFiles);
    auto result = readStructs<TFileEntry>(decompressed, entries, numFiles);
    if (!result) {
        throw Exception("Failed to read file list.");
    }

    for (const auto& entry : entries) {
        // TODO: handle archive parts, whatever they are
        auto info = createFromEntry<TFileEntry>(package, entry);
        package.addFile(info);
    }
}

template <typename THeader, typename TFileEntry>
bool readHeader(PAKReader& pakReader, int64_t offset)
{
    auto& package = pakReader.package();

    // Seek to the header
    package.seek(offset, SeekMode::Begin);

    // Read the header
    auto header = package.read<THeader>();

    package.m_header = header.commonHeader();

    if (header.version > 10) {
        package.m_header.dataOffset = static_cast<uint32_t>(offset) + sizeof(THeader);
        readCompressedFileList<TFileEntry>(pakReader, package.m_header.fileListOffset);
    } else {
        return false;
    }

    return true;
}
} // anonymous namespace

PAKReader::PAKReader()
= default;

PAKReader::PAKReader(PAKReader&& rhs) noexcept
{
    m_package = std::move(rhs.m_package);
}

PAKReader& PAKReader::operator=(PAKReader&& rhs) noexcept
{
    if (this != &rhs) {
        m_package = std::move(rhs.m_package);
    }

    return *this;
}

bool PAKReader::read(const char* filename)
{
    m_package.load(filename);
    m_package.seek(-4, SeekMode::End);

    auto signature = m_package.read<uint32_t>();

    if (signature == PAK_MAGIC) {
        // Read v13 PAK file
        throw Exception("Unsupported PAK version.");
    }

    // Check for v10 PAK file
    m_package.seek(0, SeekMode::Begin);
    signature = m_package.read<uint32_t>();

    if (signature == PAK_MAGIC) {
        // Read v10 PAK file
        auto version = m_package.read<int32_t>();

        if (version != 18) {
            throw Exception("Unsupported PAK version.");
        }

        readHeader<LSPKHeader16, FileEntry18>(*this, 4);

        return true;
    }

    return false;
}

void PAKReader::close()
{
    m_package.reset();
}

bool PAKReader::explode(const char* path)
{
    for (auto& files = m_package.m_files; const auto& fileInfo : files) {
        if (!extractFile(fileInfo, path)) {
            return false;
        }
    }

    return true;
}

const std::string& PAKReader::filename() const
{
    return m_package.m_filename;
}

Package& PAKReader::package()
{
    return m_package;
}

const std::vector<PackagedFileInfo>& PAKReader::files() const
{
    return m_package.m_files;
}

const PackagedFileInfo& PAKReader::operator[](const std::string& name) const
{
    auto it = m_package.m_filemap.find(name);
    if (it != m_package.m_filemap.end()) {
        return m_package.m_files[it->second];
    }

    throw std::out_of_range("File not found.");
}

void PAKReader::sortFiles()
{
    std::ranges::sort(m_package.m_files,
                      [](const PackagedFileInfo& a, const PackagedFileInfo& b) {
                          return a.name < b.name;
                      });

    m_package.m_filemap.clear();

    for (size_t i = 0; i < m_package.m_files.size(); ++i) {
        m_package.m_filemap[m_package.m_files[i].name] = i;
    }
}

ByteBuffer PAKReader::readFile(const std::string& name)
{
    const auto& file = (*this)[name];

    m_package.seek(static_cast<int64_t>(file.offsetInFile), SeekMode::Begin);

    auto fileData = std::make_unique<uint8_t[]>(file.sizeOnDisk);
    m_package.read(fileData.get(), file.sizeOnDisk);

    if (file.sizeOnDisk == 0) {
        return {nullptr, 0};
    }

    if (file.method() != CompressionMethod::NONE) {
        auto decompressed = Compression::decompress(file.method(), fileData.get(), file.sizeOnDisk,
                                                    file.uncompressedSize);
        return decompressed.detach();
    }

    return {std::move(fileData), file.size()};
}

bool PAKReader::extractFile(const PackagedFileInfo& file, const char* path)
{
    try {
        auto rootPath = std::filesystem::absolute(std::filesystem::path(path)).lexically_normal();
        auto outputPath = (rootPath / std::filesystem::path(file.name)).lexically_normal();
        auto relativePath = std::filesystem::relative(outputPath, rootPath);

        if (relativePath.empty() || relativePath.is_absolute() || relativePath == ".." ||
            relativePath.string().starts_with(".." + std::string(1, std::filesystem::path::preferred_separator))) {
            return false;
        }

        auto isReparsePoint = [](const std::filesystem::path& filePath) {
            const auto attributes = GetFileAttributesA(filePath.string().c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        };

        if (isReparsePoint(rootPath)) {
            return false;
        }

        auto currentPath = rootPath;
        for (const auto& component : relativePath) {
            currentPath /= component;
            if (isReparsePoint(currentPath)) {
                return false;
            }
        }

        std::error_code error;
        std::filesystem::create_directories(outputPath.parent_path(), error);
        if (error) {
            return false;
        }

        auto [fileData, fileSize] = readFile(file.name);
        FileStream outFile;
        outFile.open(outputPath.string().c_str(), "wb");
        if (outFile.write(reinterpret_cast<const char*>(fileData.get()), fileSize) != fileSize) {
            return false;
        }
        outFile.close();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool Package::load(const char* filename)
{
    reset();

    m_filename = filename;
    m_file.open(m_filename.c_str(), "rb");

    return true;
}

void Package::seek(int64_t offset, SeekMode mode)
{
    m_file.seek(offset, mode);
}

void Package::reset()
{
    m_file.close();

    m_files.clear();
    m_filemap.clear();
}

void Package::read(void* buffer, std::size_t size)
{
    m_file.read(static_cast<char*>(buffer), size);
}

Package::Package()
= default;

Package::~Package()
{
    reset();
}

void Package::addFile(const PackagedFileInfo& file)
{
    const auto it = m_files.emplace_back(file);
    m_filemap[file.name] = m_files.size() - 1;
}
