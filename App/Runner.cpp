#include "Runner.h"
#include <limits>
#include <stdexcept>

namespace Runner {
namespace {
void Require(bool ok,const char* message){if(!ok)throw std::invalid_argument(message);}
std::uint16_t Key(unsigned type,unsigned id){return static_cast<std::uint16_t>((type<<8)|id);}
}
Options ParseArguments(const std::vector<std::string>& args){
    Options out;
    if(args.empty()||(args.size()==1&&(args[0]=="--help"||args[0]=="-h")))return out;
    if(args[0]=="--self-test"){Require(args.size()==1,"--self-test takes no other arguments.");out.command=Command::SelfTest;return out;}
    if(args[0]=="example"){Require(args.size()==2&&(args[1]=="mouse"||args[1]=="keyboard"),"Use example mouse or example keyboard.");out.command=Command::Example;out.example=args[1];return out;}
    if(args[0]=="validate"){Require(args.size()==2&&!args[1].empty(),"Use validate fixture.json.");out.command=Command::Validate;out.fixture=args[1];return out;}
    if(args[0]=="run"){
        Require(args.size()>=2&&!args[1].empty()&&args[1].front()!='-',"Use run fixture.json --live --report result.json.");out.command=Command::Run;out.fixture=args[1];
        bool report=false;
        for(std::size_t i=2;i<args.size();++i){
            if(args[i]=="--live"){Require(!out.live,"--live was specified twice.");out.live=true;}
            else if(args[i]=="--report"){Require(!report&&i+1<args.size()&&!args[i+1].empty()&&args[i+1].front()!='-',"--report needs one output path.");out.report=args[++i];report=true;}
            else throw std::invalid_argument("Unknown run argument. Use --live --report result.json.");
        }
        Require(out.live&&report,"Live input is opt-in: run fixture.json --live --report result.json.");return out;
    }
    Require(args.size()==1&&!args[0].empty()&&args[0].front()!='-',"Unknown command. Use --help.");out.command=Command::Validate;out.fixture=args[0];return out;
}
std::uint64_t OffsetTicks(std::uint64_t us,std::uint64_t frequency){
    Require(us<=3600000000ULL&&frequency>0&&frequency<=1000000000000ULL,"Invalid QPC frequency or schedule duration.");
    return (us/1000000ULL)*frequency+((us%1000000ULL)*frequency+999999ULL)/1000000ULL;
}
double Microseconds(std::uint64_t ticks,std::uint64_t frequency){Require(frequency>0,"QPC frequency must be positive.");return static_cast<double>(ticks)*1000000.0/static_cast<double>(frequency);}
HidControls::HidControls(const Fixture::Scenario& scenario){
    Fixture::Validate(scenario);
    struct Global{std::uint32_t size=0,count=0,id=0;};Global g;std::vector<Global> stack;std::map<std::uint16_t,std::uint64_t> bits;
    const auto& d=scenario.report_descriptor;
    for(std::size_t p=0;p<d.size();){
        const auto prefix=d[p++];const std::size_t n=(prefix&3)==3?4:(prefix&3);Require(n<=d.size()-p,"Truncated HID item.");
        std::uint32_t value=0;for(std::size_t k=0;k<n;++k)value|=std::uint32_t(d[p+k])<<(8*k);p+=n;
        const unsigned type=(prefix>>2)&3,tag=prefix>>4;
        if(type==1){if(tag==7)g.size=value;else if(tag==8){g.id=value;numbered_=true;}else if(tag==9)g.count=value;else if(tag==10)stack.push_back(g);else if(tag==11){Require(!stack.empty(),"HID stack underflow.");g=stack.back();stack.pop_back();}}
        else if(type==0&&(tag==8||tag==9||tag==11)){
            const unsigned kind=tag==8?1u:tag==9?2u:3u;auto& count=bits[Key(kind,g.id)];count+=std::uint64_t(g.size)*g.count;Require(count<=32760,"HID control report exceeds the transfer limit.");
        }
    }
    for(const auto& item:bits){const auto bytes=static_cast<std::size_t>((item.second+7)/8)+((item.first&255u)?1u:0u);Require(bytes>0&&bytes<=Vdevice::MaxTransfer,"Invalid HID control report size.");Fixture::Bytes report(bytes,0);if(item.first&255u)report[0]=static_cast<std::uint8_t>(item.first);reports_.emplace(item.first,std::move(report));}
    for(const auto& item:reports_)if((item.first>>8)==1)idle_[static_cast<std::uint8_t>(item.first)]=0;
}
void HidControls::Sent(const Fixture::Bytes& report){const auto id=numbered_?static_cast<unsigned>(report.at(0)):0u;const auto key=Key(1,id);auto entry=reports_.find(key);Require(entry!=reports_.end()&&entry->second.size()==report.size(),"Sent report is not in the fixture descriptor.");entry->second=report;}
ControlReply HidControls::Handle(const std::array<std::uint8_t,8>& q,const Fixture::Bytes& output){
    ControlReply reply;const auto length=static_cast<std::size_t>(q[6]|(unsigned(q[7])<<8));const auto id=q[2];
    if(q[4]!=0||q[5]!=0)return reply;
    if(q[0]==0xA1&&q[1]==1&&q[3]>=1&&q[3]<=3&&output.empty()){
        const auto report=reports_.find(Key(q[3],id));if(report==reports_.end())return reply;reply.data=report->second;if(reply.data.size()>length)reply.data.resize(length);reply.stall=false;
    }else if(q[0]==0x21&&q[1]==9&&(q[3]==2||q[3]==3)){
        const auto report=reports_.find(Key(q[3],id));if(report==reports_.end()||length!=output.size()||output.size()!=report->second.size()||(id&&!output.empty()&&output[0]!=id))return reply;report->second=output;reply.stall=false;
    }else if(q[0]==0xA1&&q[1]==2&&q[3]==0&&length==1&&output.empty()){
        const auto item=idle_.find(id);if(item==idle_.end())return reply;reply.data={item->second};reply.stall=false;
    }else if(q[0]==0x21&&q[1]==10&&length==0&&output.empty()){
        if(id==0){for(auto& item:idle_)item.second=q[3];reply.stall=false;}else{const auto item=idle_.find(id);if(item!=idle_.end()){item->second=q[3];reply.stall=false;}}
    }else if(q[0]==0xA1&&q[1]==3&&q[2]==0&&q[3]==0&&length==1&&output.empty()){reply.data={protocol_};reply.stall=false;}
    else if(q[0]==0x21&&q[1]==11&&q[2]<=1&&q[3]==0&&length==0&&output.empty()){protocol_=q[2];reply.stall=false;}
    return reply;
}
unsigned SelfTest(){
    unsigned checks=0;const auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Offline runner self-test failed.");};
    const auto reject=[&](const std::vector<std::string>& args){bool refused=false;try{(void)ParseArguments(args);}catch(const std::invalid_argument&){refused=true;}check(refused);};
    check(ParseArguments({}).command==Command::Help);check(ParseArguments({"fixture.json"}).command==Command::Validate);check(!ParseArguments({"validate","fixture.json"}).live);
    check(ParseArguments({"example","mouse"}).command==Command::Example);check(ParseArguments({"run","fixture.json","--report","report.json","--live"}).live);
    for(const auto& args:std::vector<std::vector<std::string>>{{"run","fixture.json"},{"run","fixture.json","--live"},{"run","fixture.json","--report","result.json"},{"run","fixture.json","--live","--live","--report","x"},{"run","fixture.json","--live","--report"},{"example","gamepad"},{"--self-test","--live"},{"validate","x","--live"},{"--unknown"}})reject(args);
    check(OffsetTicks(0,10000000)==0);check(OffsetTicks(1,10000000)==10);check(OffsetTicks(1,3)==1);check(OffsetTicks(3600000000ULL,10000000)==36000000000ULL);
    for(std::uint64_t n=0;n<10000;++n){const auto ticks=OffsetTicks(n,3579545);check(Microseconds(ticks,3579545)+0.00001>=static_cast<double>(n));if(n)check(ticks>=OffsetTicks(n-1,3579545));}
    for(const auto& scenario:{Fixture::Mouse(),Fixture::Keyboard()}){
        const auto restored=Fixture::Parse(Fixture::Serialize(scenario));const auto valid=Vdevice::Validate_Device(Fixture::Configuration(restored));check(static_cast<bool>(valid));
        HidControls controls(restored);const auto get=controls.Handle({0xA1,1,0,1,0,0,64,0},{});check(!get.stall&&get.data.size()==scenario.input_bytes);
        controls.Sent(scenario.reports[1].data);check(controls.Handle({0xA1,1,0,1,0,0,64,0},{}).data==scenario.reports[1].data);
        check(!controls.Handle({0x21,10,0,7,0,0,0,0},{}).stall);check(controls.Handle({0xA1,2,0,0,0,0,1,0},{}).data==Fixture::Bytes{7});
        check(!controls.Handle({0x21,11,0,0,0,0,0,0},{}).stall);check(controls.Handle({0xA1,3,0,0,0,0,1,0},{}).data==Fixture::Bytes{0});
        check(controls.Handle({0xA1,1,0,1,1,0,64,0},{}).stall);check(controls.Handle({0x21,9,0,2,0,0,1,0},{1}).stall);
    }
    return checks;
}
}
