#include "pch.h"

#include <atlcomcli.h>
#include <CppUnitTest.h>
#include <limits>
#include "Exception.h"
#include "MemStream.h"
#include "Stream.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(MemStreamTests)
{

public:

    TEST_CLASS_INITIALIZE(ClassInitialize)
    {
    }

    TEST_METHOD_INITIALIZE(MethodInitilize)
    {
    }

    TEST_METHOD(TestCreation)
    {
        CComPtr stream = new MemStream();
        Assert::IsNotNull<MemStream>(stream);

        stream.Release();
        Assert::IsNull<MemStream>(stream);
    }

    TEST_METHOD(TestWriteAndRead)
    {
        CComPtr<IStream> stream = new MemStream();

        constexpr auto* text = "Hello, MemStream!";

        ULONG written = 0;
        auto hr = stream->Write(text, static_cast<ULONG>(strlen(text)), &written);

        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(static_cast<ULONG>(strlen(text)), written);

        LARGE_INTEGER seek{};
        ULARGE_INTEGER pos;
        hr = stream->Seek(seek, STREAM_SEEK_SET, &pos);

        Assert::IsTrue(SUCCEEDED(hr));

        char buffer[64]{};
        ULONG read = 0;
        hr = stream->Read(buffer, static_cast<ULONG>(strlen(text)), &read);

        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(written, read);
        Assert::AreEqual(0, strcmp(text, buffer));
    }

    TEST_METHOD(TestCloneSharesData)
    {
        CComPtr stream = new MemStream();

        constexpr auto* text = "Shared Data";

        ULONG written;
        auto hr = stream->Write(text, static_cast<ULONG>(strlen(text)), &written);
        Assert::IsTrue(SUCCEEDED(hr));

        CComPtr<IStream> clone;
        hr = stream->Clone(&clone);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::IsNotNull<IStream>(clone);

        // Rewind clone to start
        LARGE_INTEGER zero{};
        hr = clone->Seek(zero, STREAM_SEEK_SET, nullptr);
        Assert::IsTrue(SUCCEEDED(hr));

        char buffer[64] = {};
        ULONG read = 0;
        hr = clone->Read(buffer, static_cast<ULONG>(strlen(text)), &read);
        Assert::IsTrue(SUCCEEDED(hr));
        Assert::AreEqual(static_cast<ULONG>(strlen(text)), read);
        Assert::AreEqual(0, strcmp(text, buffer));
    }

    TEST_METHOD(TestFixedReadRejectsTruncatedData)
    {
        Stream stream("abc", 3);

        Assert::ExpectException<Exception>([&] {
            stream.read(4);
        });
    }

    TEST_METHOD(TestFixedReadRejectsOversizedData)
    {
        Stream stream("abc", 3);

        Assert::ExpectException<Exception>([&] {
            stream.read(std::numeric_limits<size_t>::max());
        });
    }
};

