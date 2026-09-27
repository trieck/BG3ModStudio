#include "pch.h"

#include <CppUnitTest.h>
#include "Exception.h"
#include "GR2Reader.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(GR2Tests)
{
public:
    TEST_METHOD(TestTruncatedSectionTableIsRejected)
    {
        GR2Header header{};
        const uint8_t signature[] = {
            0x29, 0xDE, 0x6C, 0xC0, 0xBA, 0xA4, 0x53, 0x2B,
            0x25, 0xF5, 0xB7, 0xA5, 0xF6, 0x66, 0xE2, 0xEE
        };
        std::memcpy(header.signature, signature, sizeof(signature));
        header.sectionOffset = sizeof(GR2Header) - offsetof(GR2Header, version);
        header.numSections = 1;

        auto data = std::make_unique<uint8_t[]>(sizeof(header));
        std::memcpy(data.get(), &header, sizeof(header));
        ByteBuffer buffer{std::move(data), sizeof(header)};

        GR2Reader reader;
        Assert::ExpectException<Exception>([&] {
            reader.read(buffer);
        });
    }

    TEST_METHOD(TestHeaderRelativeSectionTable)
    {
        // Minimal uncompressed v7 file with one section and an empty type list.
        GR2Header header{};
        const uint8_t signature[] = {
            0x29, 0xDE, 0x6C, 0xC0, 0xBA, 0xA4, 0x53, 0x2B,
            0x25, 0xF5, 0xB7, 0xA5, 0xF6, 0x66, 0xE2, 0xEE
        };
        std::memcpy(header.signature, signature, sizeof(signature));
        header.version = 7;
        header.sectionOffset = sizeof(GR2Header) - offsetof(GR2Header, version);
        header.numSections = 1;
        header.headerSize = sizeof(GR2Header) + sizeof(GR2SectionHeader);
        header.fileSize = header.headerSize + sizeof(GR2TypeNode);
        // A legal application extra tag makes the old, misaligned read fail reliably.
        header.extra[1] = UINT32_MAX;

        GR2SectionHeader section{};
        section.dataOffset = header.headerSize;
        section.compressedLen = section.decompressedLen = sizeof(GR2TypeNode);
        section.alignment = 4;
        auto data = std::make_unique<uint8_t[]>(header.fileSize);
        std::memcpy(data.get(), &header, sizeof(header));
        std::memcpy(data.get() + sizeof(header), &section, sizeof(section));
        ByteBuffer buffer{std::move(data), header.fileSize};
        GR2Reader reader;
        reader.read(buffer);
        Assert::IsTrue(reader.rootObjects().empty());
    }
};
