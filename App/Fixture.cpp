#include "Fixture.h"
#include "ThirdParty/json.hpp"
#include <array>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Fixture {
namespace {
using Json=nlohmann::json;
void Require(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
int Digit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
bool Space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='-';}
void Keys(const Json& j,std::initializer_list<const char*> names){Require(j.is_object(),"Expected a JSON object.");for(auto i=j.begin();i!=j.end();++i){bool found=false;for(auto name:names)if(i.key()==name)found=true;Require(found,"Unknown JSON property.");}}
std::uint64_t Number(const Json& j,std::uint64_t maximum){Require(j.is_number_unsigned()||j.is_number_integer(),"Expected an unsigned integer.");if(j.is_number_integer())Require(j.get<std::int64_t>()>=0,"Negative integers are not allowed here.");const auto n=j.get<std::uint64_t>();Require(n<=maximum,"Integer exceeds the supported limit.");return n;}
std::string Text(const Json& j,std::size_t limit){Require(j.is_string(),"Expected a JSON string.");auto s=j.get<std::string>();Require(s.size()<=limit,"String exceeds the supported limit.");return s;}
std::map<std::uint32_t,std::uint64_t> Descriptor(const Scenario& s){
    struct Global{std::uint32_t size=0,count=0,id=0;};Global g;
    std::vector<Global> stack;unsigned collections=0;bool numbered=false,unnumbered=false;
    std::map<std::uint32_t,std::uint64_t> inputs;
    const auto& d=s.report_descriptor;
    for(std::size_t p=0;p<d.size();){
        const auto prefix=d[p++];Require(prefix!=0xFE,"Long HID items are unsupported by this fixture validator.");
        const std::size_t n=(prefix&3)==3?4:(prefix&3);Require(n<=d.size()-p,"Truncated HID descriptor item.");
        std::uint32_t value=0;for(std::size_t k=0;k<n;++k)value|=std::uint32_t(d[p+k])<<(8*k);p+=n;
        const unsigned type=(prefix>>2)&3,tag=prefix>>4;
        if(type==1){
            if(tag==7){Require(value<=512,"HID report size is too large.");g.size=value;}
            else if(tag==8){Require(value>0&&value<=255,"HID report IDs must be 1..255.");g.id=value;numbered=true;}
            else if(tag==9){Require(value<=512,"HID report count is too large.");g.count=value;}
            else if(tag==10){Require(stack.size()<16,"HID global stack is too deep.");stack.push_back(g);}
            else if(tag==11){Require(!stack.empty(),"HID global stack underflow.");g=stack.back();stack.pop_back();}
        }else if(type==0){
            if(tag==10){Require(++collections<=32,"HID collection nesting is too deep.");}
            else if(tag==12){Require(collections>0,"HID collection underflow.");--collections;}
            else if(tag==8){Require(collections>0,"HID input must be inside a collection.");Require(g.size&&g.count,"HID input requires a nonzero report size and count.");auto& bits=inputs[g.id];bits+=std::uint64_t(g.size)*g.count;Require(bits<=512,"HID input exceeds the 64-byte full-speed fixture limit.");if(!g.id)unnumbered=true;}
        }
    }
    Require(!collections&&stack.empty(),"Unbalanced HID collections or global stack.");Require(!inputs.empty(),"HID descriptor contains no input report.");Require(!(numbered&&unnumbered),"Mixing numbered and unnumbered input reports is unsupported.");
    for(const auto& item:inputs){const auto bytes=(item.second+7)/8+(item.first?1:0);Require(bytes==s.input_bytes,"Every HID input report must match input_bytes, including its report ID.");}
    for(const auto& r:s.reports)if(numbered)Require(inputs.count(r.data.front())!=0,"Report uses an undefined HID report ID.");
    return inputs;
}
Vdevice::Descriptor String(unsigned index,const std::u16string& text){Vdevice::Descriptor d;d.index=index;d.language=0x409;d.data={static_cast<std::uint8_t>(2+text.size()*2),3};for(auto c:text){d.data.push_back(static_cast<std::uint8_t>(c));d.data.push_back(static_cast<std::uint8_t>(c>>8));}return d;}
}
Bytes ParseHex(std::string_view text,std::size_t maximum){Require(text.size()<=maximum*4+128,"Hex text is too large.");Bytes bytes;int high=-1;for(char c:text){if(Space(c)){Require(high<0,"Separate bytes, not individual hex nibbles.");continue;}const int n=Digit(c);Require(n>=0,"Hex input accepts byte pairs and whitespace only.");if(high<0)high=n;else{Require(bytes.size()<maximum,"Hex input exceeds the byte limit.");bytes.push_back(static_cast<std::uint8_t>(high*16+n));high=-1;}}Require(high<0,"Hex input has an incomplete byte.");return bytes;}
std::string Hex(const Bytes& bytes){std::ostringstream out;out<<std::hex<<std::uppercase<<std::setfill('0');for(std::size_t i=0;i<bytes.size();++i){if(i)out<<' ';out<<std::setw(2)<<unsigned(bytes[i]);}return out.str();}
void Validate(const Scenario& s){Require(!s.name.empty()&&s.name.size()<=120,"Fixture name must contain 1..120 UTF-8 bytes.");for(unsigned char c:s.name)Require(c>=32,"Fixture name contains a control character.");Require(s.vid&&s.pid,"VID and PID must be nonzero 16-bit values.");Require(s.input_bytes>=1&&s.input_bytes<=64,"input_bytes must be 1..64.");Require(!s.report_descriptor.empty()&&s.report_descriptor.size()<=4096,"HID report descriptor must contain 1..4096 bytes.");Require(s.reports.size()<=10000,"At most 10000 scheduled reports are allowed.");std::uint64_t previous=0;for(const auto& r:s.reports){Require(r.data.size()==s.input_bytes,"Input report length differs from input_bytes.");Require(r.at_us>=previous&&r.at_us<=3600000000ULL,"Report times must increase and remain within one hour.");previous=r.at_us;}Descriptor(s);}
Scenario Parse(std::string_view text){Require(text.size()<=4*1024*1024,"Fixture JSON exceeds 4 MiB.");unsigned depth=0;std::vector<std::set<std::string>> keys;const auto callback=[&](int,Json::parse_event_t event,Json& value){if(event==Json::parse_event_t::object_start||event==Json::parse_event_t::array_start){Require(++depth<=16,"JSON nesting is too deep.");if(event==Json::parse_event_t::object_start)keys.emplace_back();}else if(event==Json::parse_event_t::object_end||event==Json::parse_event_t::array_end){--depth;if(event==Json::parse_event_t::object_end)keys.pop_back();}else if(event==Json::parse_event_t::key){Require(!keys.empty()&&keys.back().insert(value.get<std::string>()).second,"Duplicate JSON properties are not allowed.");}return true;};const auto j=Json::parse(text.begin(),text.end(),callback);Keys(j,{"schema","name","speed","vid","pid","input_bytes","report_descriptor","reports"});Require(Number(j.at("schema"),1)==1,"Unsupported fixture schema.");Require(Text(j.at("speed"),16)=="full","Schema 1 supports full-speed USB fixtures only.");Scenario s;s.name=Text(j.at("name"),120);s.vid=static_cast<std::uint16_t>(Number(j.at("vid"),65535));s.pid=static_cast<std::uint16_t>(Number(j.at("pid"),65535));s.input_bytes=static_cast<std::uint32_t>(Number(j.at("input_bytes"),64));s.report_descriptor=ParseHex(Text(j.at("report_descriptor"),16512));const auto& reports=j.at("reports");Require(reports.is_array()&&reports.size()<=10000,"reports must be an array of at most 10000 items.");for(const auto& r:reports){Keys(r,{"at_us","hex"});s.reports.push_back({Number(r.at("at_us"),3600000000ULL),ParseHex(Text(r.at("hex"),384),64)});}Validate(s);return s;}
std::string Serialize(const Scenario& s){Validate(s);Json j={{"schema",1},{"name",s.name},{"speed","full"},{"vid",s.vid},{"pid",s.pid},{"input_bytes",s.input_bytes},{"report_descriptor",Hex(s.report_descriptor)},{"reports",Json::array()}};for(const auto& r:s.reports)j["reports"].push_back({{"at_us",r.at_us},{"hex",Hex(r.data)}});return j.dump(2)+"\n";}
Bytes InitialInputState(const Scenario& s){Validate(s);const auto layouts=Descriptor(s);Bytes data(s.input_bytes,0);const auto id=layouts.begin()->first;if(id)data.front()=static_cast<std::uint8_t>(id);return data;}
Vdevice::Device_Config Configuration(const Scenario& s){Validate(s);Vdevice::Device_Config c;c.delegate_controls=true;Vdevice::Descriptor device;device.data={18,1,0,2,0,0,0,64,static_cast<std::uint8_t>(s.vid),static_cast<std::uint8_t>(s.vid>>8),static_cast<std::uint8_t>(s.pid),static_cast<std::uint8_t>(s.pid>>8),0,1,1,2,0,1};Vdevice::Descriptor report;report.kind=Vdevice::Descriptor_Kind::Report;report.data=s.report_descriptor;Vdevice::Descriptor usb;usb.data={9,2,34,0,1,1,0,0x80,50,9,4,0,0,1,3,0,0,0,9,0x21,0x11,1,0,1,0x22,static_cast<std::uint8_t>(report.data.size()),static_cast<std::uint8_t>(report.data.size()>>8),7,5,0x81,3,static_cast<std::uint8_t>(s.input_bytes),0,1};Vdevice::Descriptor language;language.data={4,3,9,4};c.descriptors={std::move(device),std::move(usb),std::move(report),std::move(language),String(1,u"Titan input tools"),String(2,u"HID Fixture Studio")};return c;}
Scenario Mouse(){Scenario s;s.report_descriptor=ParseHex("05 01 09 02 A1 01 09 01 A1 00 05 09 19 01 29 05 15 00 25 01 95 05 75 01 81 02 95 01 75 03 81 01 05 01 09 30 09 31 16 00 80 26 FF 7F 75 10 95 02 81 06 09 38 15 81 25 7F 75 08 95 01 81 06 05 0C 0A 38 02 95 01 81 06 C0 C0");s.reports={{0,Bytes(7)},{10000,ParseHex("00 C8 00 00 00 00 00")},{20000,Bytes(7)}};return s;}
Scenario Keyboard(){Scenario s;s.name="Keyboard A pulse";s.pid=17;s.input_bytes=8;s.report_descriptor=ParseHex("05 01 09 06 A1 01 05 07 19 E0 29 E7 15 00 25 01 75 01 95 08 81 02 95 01 75 08 81 01 95 06 75 08 15 00 25 65 19 00 29 65 81 00 C0");s.reports={{0,Bytes(8)},{10000,ParseHex("00 00 04 00 00 00 00 00")},{20000,Bytes(8)}};return s;}
}
