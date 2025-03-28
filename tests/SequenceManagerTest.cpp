//
// Created by raffael on 01.01.25.
//
#define CATCH_CONFIG_PREFIX_ALL
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <utility/json.hpp>


#include "SequenceManager.h"


class MockConfig : public Config {
public:
    MOCK_METHOD(nlohmann::json&, operator[], (const std::string& path), (override));
    MOCK_METHOD(std::string, getConfigFilePath, (), (const, override));
    MOCK_METHOD(std::string, getMappingFilePath, (), (const, override));
    MOCK_METHOD(void, print, (), (override));
};


CATCH_TEST_CASE("Sequence Manager", "[SequenceManager]") {
    MockConfig mockConfig;

    nlohmann::json jsonReturnValue;
    jsonReturnValue["key"] = false;

    REQUIRE_CALL(mockConfig, operator[]("autoabort"))
        .RETURN(jsonReturnValue);

    std::unordered_map<size_t, int> values = {{0, 42}, {1, 99}, {2, 77}};

    REQUIRE_CALL(mock, operator[](trompeloeil::_))
        .RETURN(values[trompeloeil::_1]);  // Use argument as key in map


    REQUIRE_CALL(mockConfig, getConfigFilePath())
        .RETURN("mapping.json");

    REQUIRE_CALL(mockConfig, getMappingFilePath())
        .RETURN("config.json");

    SequenceManager manager = SequenceManager();

    CATCH_SECTION("Initialization Check") {
        CATCH_REQUIRE(true);
    }
}