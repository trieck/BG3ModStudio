#include "pch.h"
#include <CppUnitTest.h>
#include "PAKReader.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace {
struct ExtractionFixture {
    std::filesystem::path root;
    PAKReader reader;

    ExtractionFixture()
    {
        char temp[MAX_PATH], name[MAX_PATH];
        GetTempPathA(MAX_PATH, temp);
        if (!GetTempFileNameA(temp, "pak", 0, name)) {
            throw std::runtime_error("Cannot create test directory");
        }
        root = name;
        std::filesystem::remove(root);
        std::filesystem::create_directories(root / "output");
        auto source = (root / "source.bin").string();
        {
            std::ofstream out(source, std::ios::binary);
            out << "payload";
        }
        reader.package().load(source.c_str());
        PackagedFileInfo entry{};
        entry.name = "file.txt";
        entry.sizeOnDisk = entry.uncompressedSize = 7;
        reader.package().addFile(entry);
    }

    ~ExtractionFixture()
    {
        reader.close();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
}

TEST_CLASS(PAKTests)
{
public:
    TEST_METHOD(TestExtractionFlushesCompleteContents)
    {
        ExtractionFixture fixture;
        auto output = (fixture.root / "output").string();
        Assert::IsTrue(fixture.reader.explode(output.c_str()));
        std::ifstream input(fixture.root / "output" / "file.txt", std::ios::binary);
        std::string actual((std::istreambuf_iterator<char>(input)), {});
        Assert::AreEqual(std::string("payload"), actual);
    }

    TEST_METHOD(TestExtractionReportsFinalFlushFailure)
    {
        ExtractionFixture fixture;
        auto output = (fixture.root / "output").string();
        auto target = (fixture.root / "output" / "file.txt").string();
        constexpr DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        ATL::CHandle lock(CreateFileA(target.c_str(), GENERIC_READ | GENERIC_WRITE,
            sharing, nullptr, CREATE_ALWAYS, 0, nullptr));
        Assert::IsTrue(lock.m_h != INVALID_HANDLE_VALUE);
        OVERLAPPED overlapped{};
        Assert::IsTrue(!!LockFileEx(lock, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
            0, 100, 0, &overlapped));
        {
            // Opening/truncating succeeds; the buffered final write is what must fail.
            ATL::CHandle probe(CreateFileA(target.c_str(), GENERIC_WRITE,
                sharing, nullptr, CREATE_ALWAYS, 0, nullptr));
            Assert::IsTrue(probe.m_h != INVALID_HANDLE_VALUE);
        }
        Assert::IsFalse(fixture.reader.explode(output.c_str()));
        Assert::AreEqual<uintmax_t>(0, std::filesystem::file_size(target));
    }
};
