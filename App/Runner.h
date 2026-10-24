#pragma once
#include "Fixture.h"
#include <array>
#include <map>
#include <string>
#include <vector>

namespace Runner {
enum class Command { Help, Validate, Example, Run, SelfTest };
struct Options { Command command=Command::Help; std::string fixture,example,report; bool live=false; };
Options ParseArguments(const std::vector<std::string>& arguments);
std::uint64_t OffsetTicks(std::uint64_t microseconds,std::uint64_t frequency);
double Microseconds(std::uint64_t ticks,std::uint64_t frequency);
struct ControlReply { Fixture::Bytes data; bool stall=true; };
class HidControls {
public:
    explicit HidControls(const Fixture::Scenario& scenario);
    void Sent(const Fixture::Bytes& report);
    ControlReply Handle(const std::array<std::uint8_t,8>& setup,const Fixture::Bytes& output);
private:
    std::map<std::uint16_t,Fixture::Bytes> reports_;
    std::map<std::uint8_t,std::uint8_t> idle_;
    std::uint8_t protocol_=1;
    bool numbered_=false;
};
unsigned SelfTest();
}
