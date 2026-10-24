#include "Runner.h"
#include <iostream>
#include <stdexcept>

int main(){try{const auto checks=Runner::SelfTest();
    auto keyboard=Fixture::Keyboard();keyboard.report_descriptor.insert(keyboard.report_descriptor.end()-1,{0x05,0x08,0x19,0x01,0x29,0x05,0x95,0x05,0x75,0x01,0x91,0x02,0x95,0x01,0x75,0x03,0x91,0x01});
    Runner::HidControls controls(keyboard);const Fixture::Bytes output{3};if(controls.Handle({0x21,9,0,2,0,0,1,0},output).stall)throw std::runtime_error("SET_REPORT output rejected");if(controls.Handle({0xA1,1,0,2,0,0,1,0},{}).data!=output)throw std::runtime_error("GET_REPORT output mismatch");if(!controls.Handle({0x21,9,0,2,0,0,2,0},output).stall)throw std::runtime_error("SET_REPORT bad length accepted");
    std::cout<<checks+3<<" offline checks passed. No driver or input accessed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
