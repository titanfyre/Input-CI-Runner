#pragma once
#include "Vdevice.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Fixture {
using Bytes=std::vector<std::uint8_t>;
struct Report {std::uint64_t at_us=0;Bytes data;};
struct Scenario {
    std::string name="Mouse step";
    std::uint16_t vid=65535,pid=16;
    std::uint32_t input_bytes=7;
    Bytes report_descriptor;
    std::vector<Report> reports;
};
Bytes ParseHex(std::string_view text,std::size_t maximum=4096);
std::string Hex(const Bytes& bytes);
void Validate(const Scenario& scenario);
Scenario Parse(std::string_view json);
std::string Serialize(const Scenario& scenario);
Vdevice::Device_Config Configuration(const Scenario& scenario);
// Initial GET_REPORT cache only: zero data with a declared report ID when
// present. Zero is not a universal semantic neutral value for arbitrary HID.
Bytes InitialInputState(const Scenario& scenario);
Scenario Mouse();
Scenario Keyboard();
}
