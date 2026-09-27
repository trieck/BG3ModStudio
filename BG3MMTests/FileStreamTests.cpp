#include "pch.h"
#include "UtilityBase.h"
#include "FileStream.h"

#include <CppUnitTest.h>

#include "Exception.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

static std::string tempFile(const char* name)
{
    auto path = std::filesystem::temp_directory_path() / name;
    return path.string();
}

TEST_CLASS(FileStreamTests)
{
public:
    TEST_METHOD_INITIALIZE(Setup)
    {
    }

    TEST_METHOD(TestBufferedHeaderRewrite)
    {
        auto path = tempFile("fs_header_rewrite.bin");
        for (size_t length : {100u, 40000u, 100000u}) {
            std::string expected(length, 'A');
            expected.replace(4, 4, "HEAD");
            FileStream fs;
            fs.open(path.c_str(), "wb");
            std::string original(length, 'A');
            fs.write(original.data(), original.size());
            fs.seek(4, SeekMode::Begin);
            fs.write("HEAD", 4);
            fs.close();
            std::ifstream input(path, std::ios::binary);
            std::string actual((std::istreambuf_iterator<char>(input)), {});
            Assert::AreEqual(expected, actual);
        }
    }

    TEST_METHOD(TestRelativeSeekBeyondReadBuffer)
    {
        auto path = tempFile("fs_relative_seek.bin");
        {
            std::ofstream output(path, std::ios::binary);
            output << std::string(3000000, 'A');
        }
        FileStream fs;
        fs.open(path.c_str(), "rb");
        char c;
        fs.read(&c, 1);
        fs.seek(2000000, SeekMode::Current);
        Assert::AreEqual<size_t>(2000001, fs.tell());
        fs.read(&c, 1);
        fs.seek(-2000000, SeekMode::Current);
        Assert::AreEqual<size_t>(2, fs.tell());
    }

    TEST_METHOD(TestCloseReportsWriteFailure)
    {
        auto path = tempFile("fs_failed_close.bin");
        FileStream fs;
        fs.open(path.c_str(), "wb");
        ATL::CHandle lock(CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, 0, nullptr));
        Assert::IsTrue(lock.m_h != INVALID_HANDLE_VALUE);
        OVERLAPPED overlapped{};
        Assert::IsTrue(!!LockFileEx(lock, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
            0, 100, 0, &overlapped));
        fs.write("data", 4);
        Assert::ExpectException<Exception>([&] { fs.close(); });
        Assert::IsFalse(fs.isOpen());
        Assert::AreEqual<uintmax_t>(0, std::filesystem::file_size(path));
    }

    TEST_METHOD(TestSeekReportsWriteFailure)
    {
        auto path = tempFile("fs_failed_seek.bin");
        FileStream fs;
        fs.open(path.c_str(), "wb");
        ATL::CHandle lock(CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, 0, nullptr));
        OVERLAPPED overlapped{};
        Assert::IsTrue(!!LockFileEx(lock, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
            0, 100, 0, &overlapped));
        fs.write("data", 4);
        Assert::ExpectException<Exception>([&] { fs.seek(0, SeekMode::Begin); });
        // Destruction must remain non-throwing after an explicit close failure.
        Assert::ExpectException<Exception>([&] { fs.close(); });
    }

    TEST_METHOD(TestOpenRead)
    {
        auto path = tempFile("fs_open_read.bin");

        // Create file with known contents
        {
            std::ofstream ofs(path, std::ios::binary);
            ofs << "abcdef";
        }

        FileStream fs;
        fs.open(path.c_str(), "rb");

        Assert::IsTrue(fs.isOpen());
        Assert::AreEqual<size_t>(6, fs.size());
        Assert::AreEqual<size_t>(0, fs.tell());

        // Verify write is rejected
        char c = 'x';
        Assert::ExpectException<Exception>([&] {
            fs.write(&c, 1);
        });
    }

    TEST_METHOD(TestOpenWrite)
    {
        auto path = tempFile("fs_open_write.bin");

        FileStream fs;
        fs.open(path.c_str(), "wb");

        Assert::IsTrue(fs.isOpen());
        Assert::AreEqual<size_t>(0, fs.size());
        Assert::AreEqual<size_t>(0, fs.tell());

        // Verify read is rejected
        char buf[1];
        Assert::ExpectException<Exception>([&] {
            fs.read(buf, 1);
        });

        // Write something
        fs.write("abc", 3);
        fs.flush();

        Assert::AreEqual<size_t>(3, fs.size());
    }

    TEST_METHOD(TestOpenAppend)
    {
        auto path = tempFile("fs_open_append.bin");

        // Seed file
        {
            std::ofstream ofs(path, std::ios::binary);
            ofs << "hello";
        }

        FileStream fs;
        fs.open(path.c_str(), "ab");

        Assert::IsTrue(fs.isOpen());
        Assert::AreEqual<size_t>(5, fs.size());
        Assert::AreEqual<size_t>(5, fs.tell());

        fs.write("!", 1);
        fs.flush();

        Assert::AreEqual<size_t>(6, fs.size());
    }

    TEST_METHOD(TestOpenInvalidMode)
    {
        FileStream fs;
        Assert::ExpectException<Exception>([&] {
            fs.open("dummy.bin", "r+");
        });
    }

    TEST_METHOD(TestOpenResetsState)
    {
        auto path = tempFile("fs_open_reset.bin");

        FileStream fs;
        fs.open(path.c_str(), "wb");
        fs.write("abc", 3);
        fs.flush();

        fs.open(path.c_str(), "rb");

        Assert::AreEqual<size_t>(3, fs.size());
        Assert::AreEqual<size_t>(0, fs.tell());
    }

    TEST_METHOD(TestReadCloseOpenRead)
    {
        auto path = tempFile("fs_read_close_read.bin");

        // Seed file with known data
        {
            std::ofstream ofs(path, std::ios::binary);
            ofs << "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        }

        FileStream fs;

        // First open and partial read
        fs.open(path.c_str(), "rb");

        char buf1[5] = {};
        size_t r1 = fs.read(buf1, 5);

        Assert::AreEqual<size_t>(5, r1);
        Assert::AreEqual(std::string("ABCDE"), std::string(buf1, 5));
        Assert::AreEqual<size_t>(5, fs.tell());

        // Close resets state
        fs.close();

        // Reopen and read again from beginning
        fs.open(path.c_str(), "rb");

        char buf2[5] = {};
        size_t r2 = fs.read(buf2, 5);

        Assert::AreEqual<size_t>(5, r2);
        Assert::AreEqual(std::string("ABCDE"), std::string(buf2, 5));
        Assert::AreEqual<size_t>(5, fs.tell());
    }

    TEST_METHOD(TestReadWriteRead)
    {
        auto path = tempFile("fs_read_write_read.bin");

        {
            std::ofstream ofs(path, std::ios::binary);
            ofs << "0123456789";
        }

        FileStream fs;
        fs.open(path.c_str(), "rb");

        char rbuf[4] = {};
        fs.read(rbuf, 4);
        Assert::AreEqual(std::string("0123"), std::string(rbuf, 4));
        Assert::AreEqual<size_t>(4, fs.tell());

        fs.close();
        fs.open(path.c_str(), "rb");

        // Now test write interleaving
        fs.seek(4, SeekMode::Begin);
        fs.close();

        fs.open(path.c_str(), "ab");
        fs.write("AB", 2);
        fs.flush();
        fs.close();

        fs.open(path.c_str(), "rb");

        char finalBuf[12] = {};
        fs.read(finalBuf, 12);

        Assert::AreEqual(
            std::string("0123456789AB"),
            std::string(finalBuf, 12)
        );
    }

    TEST_METHOD(TestReadCrossesBlockBoundary)
    {
        auto path = tempFile("fs_read_cross_block.bin");

        constexpr size_t BLOCK = 1 << 16u; // 64KB
        constexpr size_t FILE_SIZE = BLOCK + 100;

        // Seed file
        {
            std::ofstream ofs(path, std::ios::binary);
            for (auto i = 0u; i < FILE_SIZE; ++i) {
                auto c = static_cast<char>('A' + (i % 26));
                ofs.write(&c, 1);
            }
        }

        FileStream fs;
        fs.open(path.c_str(), "rb");

        // Read across the boundary in two chunks
        std::vector<char> buf(FILE_SIZE);
        auto r = fs.read(buf.data(), FILE_SIZE);

        Assert::AreEqual(FILE_SIZE, r);
        Assert::AreEqual(FILE_SIZE, fs.tell());

        // Spot check around the boundary
        Assert::AreEqual('A', buf[0]);
        Assert::AreEqual(
            static_cast<char>('A' + ((BLOCK - 1) % 26)),
            buf[BLOCK - 1]
        );
        Assert::AreEqual(
            static_cast<char>('A' + (BLOCK % 26)),
            buf[BLOCK]
        );
    }

    TEST_METHOD(TestWriteCrossesBlockBoundary)
    {
        auto path = tempFile("fs_write_cross_block.bin");

        constexpr size_t BLOCK = 1 << 16u; // 64KB
        constexpr size_t WRITE_SIZE = BLOCK + 123;

        FileStream fs;
        fs.open(path.c_str(), "wb");

        std::vector data(WRITE_SIZE, 'X');
        size_t w = fs.write(data.data(), data.size());
        fs.flush();

        Assert::AreEqual(WRITE_SIZE, w);
        Assert::AreEqual(WRITE_SIZE, fs.size());
        Assert::AreEqual(WRITE_SIZE, fs.tell());

        fs.close();

        // Verify contents
        std::ifstream ifs(path, std::ios::binary);
        std::vector<char> verify(WRITE_SIZE);
        ifs.read(verify.data(), static_cast<std::streamsize>(verify.size()));

        for (char c : verify) {
            Assert::AreEqual('X', c);
        }
    }

    
};
