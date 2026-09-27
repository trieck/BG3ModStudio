#include "pch.h"

#include <CppUnitTest.h>
#include "LSFReader.h"
#include "LSFWriter.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(LSFTests)
{
public:
    TEST_METHOD(TestTranslatedFSStringRoundTripWithArguments)
    {
        Resource resource;
        auto region = std::make_shared<Region>();
        region->name = "TestRegion";

        TranslatedFSStringT translated;
        translated.version = 1;
        translated.handle = "hTest";

        TranslatedFSStringArgument argument;
        argument.key = "key";
        argument.value = "value";
        argument.string = std::make_shared<TranslatedFSStringT>();
        argument.string->version = 2;
        argument.string->handle = "nested";
        translated.arguments.emplace_back(std::move(argument));

        NodeAttribute attribute(TranslatedFSString);
        attribute.setValue(std::move(translated));
        region->attributes["DisplayName"] = std::move(attribute);
        resource.regions.emplace(region->name, region);

        Stream encoded;
        LSFWriter writer;
        writer.write(encoded, resource);

        encoded.seek(0, SeekMode::Begin);
        LSFReader reader;
        auto decoded = reader.read(encoded);

        Assert::IsTrue(decoded->regions.contains("TestRegion"));
        const auto& decodedAttribute = decoded->regions.at("TestRegion")->attributes.at("DisplayName");
        const auto decodedString = std::get<TranslatedFSStringT>(decodedAttribute.value());

        Assert::AreEqual(static_cast<uint16_t>(1), decodedString.version);
        Assert::AreEqual(std::string("hTest"), decodedString.handle);
        Assert::AreEqual<size_t>(1, decodedString.arguments.size());
        Assert::AreEqual(std::string("key"), decodedString.arguments[0].key);
        Assert::AreEqual(std::string("value"), decodedString.arguments[0].value);
        Assert::IsNotNull(decodedString.arguments[0].string.get());
        Assert::AreEqual(static_cast<uint16_t>(2), decodedString.arguments[0].string->version);
        Assert::AreEqual(std::string("nested"), decodedString.arguments[0].string->handle);
    }
};
