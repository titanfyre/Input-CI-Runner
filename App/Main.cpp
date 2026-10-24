#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Runner.h"
#include "ThirdParty/json.hpp"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

namespace {
using Json=nlohmann::json;
template<class T,class=void>struct HasTrial:std::false_type{};
template<class T>struct HasTrial<T,std::void_t<decltype(std::declval<T>().trial_expired)>>:std::true_type{};
template<class T>bool Unavailable(const T& info){if constexpr(HasTrial<T>::value)return info.trial_expired||info.quiescing||info.removing;else return !info.licensed||info.quiescing||info.removing;}
std::atomic<HANDLE> stop_handle{nullptr};
BOOL WINAPI Cancel(DWORD kind){if(kind==CTRL_C_EVENT||kind==CTRL_BREAK_EVENT||kind==CTRL_CLOSE_EVENT||kind==CTRL_LOGOFF_EVENT||kind==CTRL_SHUTDOWN_EVENT){const auto stop=stop_handle.load();if(stop)SetEvent(stop);return TRUE;}return FALSE;}
struct Handle{HANDLE value=nullptr;explicit Handle(HANDLE handle=nullptr):value(handle){}~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;};
[[noreturn]] void WinError(const char* stage,DWORD error=GetLastError()){throw std::system_error(static_cast<int>(error?error:ERROR_GEN_FAILURE),std::system_category(),stage);}
void Check(const Vdevice::Result& result,const char* stage){if(!result)throw std::system_error(static_cast<int>(result.error?result.error:ERROR_GEN_FAILURE),std::system_category(),std::string(stage)+": "+result.message);}
std::uint64_t Qpc(){LARGE_INTEGER value{};if(!QueryPerformanceCounter(&value)||value.QuadPart<0)WinError("QueryPerformanceCounter");return static_cast<std::uint64_t>(value.QuadPart);}
std::uint64_t Frequency(){LARGE_INTEGER value{};if(!QueryPerformanceFrequency(&value)||value.QuadPart<=0)WinError("QueryPerformanceFrequency");return static_cast<std::uint64_t>(value.QuadPart);}
std::string Utf8(const wchar_t* value){const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,nullptr,0,nullptr,nullptr);if(!n)WinError("Convert argument to UTF-8");std::string out(static_cast<std::size_t>(n),0);if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,out.data(),n,nullptr,nullptr))WinError("Convert argument to UTF-8");out.pop_back();return out;}
std::string Read(const std::filesystem::path& path){if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Fixture must be a regular JSON file.");std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Fixture file could not be opened.");std::string text;char part[8192];while(file){file.read(part,sizeof(part));const auto n=file.gcount();if(text.size()+static_cast<std::size_t>(n)>4*1024*1024)throw std::runtime_error("Fixture exceeds 4 MiB.");text.append(part,static_cast<std::size_t>(n));}if(!file.eof())throw std::runtime_error("Fixture file could not be read.");return text;}
bool SamePath(const std::filesystem::path& a,const std::filesystem::path& b){std::error_code error;if(std::filesystem::equivalent(a,b,error)&&!error)return true;const auto first=std::filesystem::weakly_canonical(a).wstring(),second=std::filesystem::weakly_canonical(b).wstring();return CompareStringOrdinal(first.c_str(),-1,second.c_str(),-1,TRUE)==CSTR_EQUAL;}
void WriteReport(const std::filesystem::path& path,const Json& report){
    const auto parent=path.has_parent_path()?path.parent_path():std::filesystem::current_path();if(!std::filesystem::is_directory(parent))throw std::runtime_error("Report parent directory does not exist.");
    const auto temporary=parent/(path.filename().wstring()+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(Qpc()));
    Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));if(file.value==INVALID_HANDLE_VALUE)WinError("Create report temporary file");
    bool moved=false;
    try{const auto data=report.dump(2)+"\n";std::size_t offset=0;while(offset<data.size()){DWORD count=0;const auto length=static_cast<DWORD>(std::min<std::size_t>(data.size()-offset,1024*1024));if(!WriteFile(file.value,data.data()+offset,length,&count,nullptr)||!count)WinError("Write JSON report");offset+=count;}if(!FlushFileBuffers(file.value))WinError("Flush JSON report");CloseHandle(file.value);file.value=nullptr;if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))WinError("Commit JSON report");moved=true;}catch(...){if(file.value){CloseHandle(file.value);file.value=nullptr;}if(!moved)DeleteFileW(temporary.c_str());throw;}
}
class Timer {
public:
    Timer(){value_.value=CreateWaitableTimerExW(nullptr,nullptr,0x00000002u,TIMER_ALL_ACCESS);high_=value_.value!=nullptr;if(!value_.value)value_.value=CreateWaitableTimerW(nullptr,FALSE,nullptr);if(!value_.value)WinError("Create waitable timer");}
    bool HighResolution()const noexcept{return high_;}
    bool Wait(HANDLE stop,std::uint64_t deadline,std::uint64_t frequency){
        const auto now=Qpc();if(now>=deadline){const auto stopped=WaitForSingleObject(stop,0);if(stopped==WAIT_FAILED)WinError("Check cancel event");return stopped!=WAIT_OBJECT_0;}
        const auto remaining=deadline-now;const auto hundreds=(remaining/frequency)*10000000ULL+((remaining%frequency)*10000000ULL+frequency-1)/frequency;
        LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>(std::max<std::uint64_t>(hundreds,1));if(!SetWaitableTimer(value_.value,&due,0,nullptr,nullptr,FALSE))WinError("Arm waitable timer");const HANDLE events[]={stop,value_.value};const auto result=WaitForMultipleObjects(2,events,FALSE,INFINITE);if(result==WAIT_OBJECT_0)return false;if(result!=WAIT_OBJECT_0+1)WinError("Wait for deadline");return true;
    }
private:Handle value_;bool high_=false;
};
class OwnedDevice {
public:
    Vdevice::Device value;
    ~OwnedDevice(){if(value){try{(void)Vdevice::Delete_Device(value);}catch(...){}}}
    OwnedDevice()=default;OwnedDevice(const OwnedDevice&)=delete;OwnedDevice& operator=(const OwnedDevice&)=delete;
};
void Help(){std::cout<<"Input CI Runner - scheduled virtual HID fixtures\n\n"
    "  InputCIRunner validate fixture.json       Offline; never opens the driver\n"
    "  InputCIRunner fixture.json                Same offline validation\n"
    "  InputCIRunner example mouse|keyboard      Write a fixture to stdout\n"
    "  InputCIRunner run fixture.json --live --report result.json\n"
    "  InputCIRunner --self-test                 Offline model tests\n\n"
    "Live mode creates a real virtual device and affects Windows. Run as administrator,\n"
    "use only an authorized isolated test machine, and press Ctrl+C to cancel.\n"
    "Acceptance and enqueue timing are not application delivery or game latency.\n"
    "Commercial production-driver bundles, OEM deployment and support: Discord titancsa.\n";}
Json BaseReport(const Fixture::Scenario& scenario){return {{"schema",1},{"tool","Input CI Runner"},{"fixture",scenario.name},{"mode","live"},{"validated_before_driver",true},{"scheduled_reports",scenario.reports.size()},{"scheduled_duration_us",scenario.reports.empty()?0:scenario.reports.back().at_us},{"accepted_count",0},{"cancelled",false},{"status","starting"},{"errors",Json::array()},{"observations",Json::array()},{"measurement_scope","QPC observations around Vdevice enqueue calls. Acceptance is not USB/HID completion, application receipt, game latency or display latency."},{"machine_enforcement",false},{"driver_edition",HasTrial<Vdevice::Info>::value?"demo":"production"},{"driver_policy",HasTrial<Vdevice::Info>::value?"The bundled Vdevice demo is limited to one hour by its driver policy. This runner does not reset or bypass it.":"The separately licensed production driver requires its normal authorization. This runner does not issue or bypass driver keys."}};}
int Live(const Fixture::Scenario& scenario,const std::filesystem::path& output){
    auto report=BaseReport(scenario);OwnedDevice device;std::string stage="prepare";DWORD failure=0;bool cancelled=false;
    Handle stop(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!stop.value)WinError("Create cancel event");
    struct Registration{bool registered=false;~Registration(){if(registered)SetConsoleCtrlHandler(Cancel,FALSE);stop_handle.store(nullptr);}}registration;
    stop_handle.store(stop.value);if(!SetConsoleCtrlHandler(Cancel,TRUE))WinError("Register console cancellation");registration.registered=true;
    try{
        const auto frequency=Frequency();(void)Runner::OffsetTicks(3600000000ULL,frequency);Timer timer;Runner::HidControls controls(scenario);report["qpc_frequency_hz"]=frequency;report["high_resolution_waitable_timer"]=timer.HighResolution();report["control_service_max_wait_us"]=10000;
        stage="create_device";device.value=Vdevice::Create_Device(Fixture::Configuration(scenario));if(!device.value)throw std::system_error(static_cast<int>(device.value.error?device.value.error:ERROR_GEN_FAILURE),std::system_category(),device.value.message);
        report["owned_device_id"]=device.value.Id();report["device_removed"]=false;
        const auto cancelledNow=[&]{const auto result=WaitForSingleObject(stop.value,0);if(result==WAIT_FAILED)WinError("Check cancel event");return result==WAIT_OBJECT_0;};
        std::uint64_t controls_seen=0,data_seen=0;
        const auto pump=[&]{for(unsigned n=0;n<64;++n){const auto event=Vdevice::Get_Output(0);if(!event){if(event.timed_out||event.error==ERROR_TIMEOUT||event.error==ERROR_NO_MORE_ITEMS||event.error==ERROR_NO_DATA)break;Check(event,"Receive device output");}if(event.device_id!=device.value.Id())throw std::runtime_error("Unexpected output device identity in this runner session.");if(event.kind==Vdevice::Output_Kind::Control){const auto response=controls.Handle(event.setup,event.data);Check(Vdevice::Respond_Output(event,response.data,response.stall),"Respond to HID control request");++controls_seen;}else ++data_seen;}};
        const auto details=[&]{const auto info=Vdevice::Get_Info();Check(info,"Query Vdevice state");if(Unavailable(info))throw std::runtime_error("Vdevice is unavailable, expired, not authorized or stopping. Check the matching driver edition and setup.");const auto found=std::find_if(info.devices.begin(),info.devices.end(),[&](const auto& d){return d.id==device.value.Id();});if(found==info.devices.end())throw std::runtime_error("The owned virtual device disappeared.");if(found->error)throw std::system_error(static_cast<int>(found->error),std::system_category(),"Owned device state");return *found;};
        const auto slice=Runner::OffsetTicks(10000,frequency),ready_start=Qpc(),ready_deadline=ready_start+Runner::OffsetTicks(10000000,frequency);
        stage="await_configuration";
        for(;;){if(cancelledNow()){cancelled=true;break;}pump();const auto state=details();if(state.plugged&&state.configured&&state.powered)break;if(Qpc()>=ready_deadline)throw std::system_error(ERROR_TIMEOUT,std::system_category(),"Windows did not configure and power the device within 10 seconds");if(!timer.Wait(stop.value,std::min(ready_deadline,Qpc()+slice),frequency)){cancelled=true;break;}}
        report["enumeration_wait_us"]=Runner::Microseconds(Qpc()-ready_start,frequency);
        std::uint64_t accepted=0,retries=0;const auto start=Qpc();const auto last_due=scenario.reports.empty()?0:scenario.reports.back().at_us;const auto run_deadline=start+Runner::OffsetTicks(last_due,frequency)+Runner::OffsetTicks(10000000ULL,frequency);
        stage="scheduled_enqueue";
        for(std::size_t i=0;!cancelled&&i<scenario.reports.size();++i){
            if(Qpc()>=run_deadline)throw std::system_error(ERROR_TIMEOUT,std::system_category(),"The run exceeded the final scheduled time plus its 10-second queue allowance");
            const auto& input=scenario.reports[i];const auto due=start+Runner::OffsetTicks(input.at_us,frequency);
            while(Qpc()<due){pump();if(cancelledNow()||!timer.Wait(stop.value,std::min(due,Qpc()+slice),frequency)){cancelled=true;break;}}
            if(cancelled||cancelledNow()){cancelled=true;break;}
            const auto first=Qpc();auto before=first,after=first;Vdevice::Result sent;unsigned attempts=0;
            for(;;){if(cancelledNow()){cancelled=true;break;}if(Qpc()>=run_deadline)throw std::system_error(ERROR_TIMEOUT,std::system_category(),"The input queue allowance expired");before=Qpc();sent=Vdevice::Send_Input(device.value,0x81,input.data);after=Qpc();++attempts;if(sent||sent.error!=ERROR_BUSY)break;++retries;if(after>=run_deadline)break;pump();if(!timer.Wait(stop.value,std::min(run_deadline,after+Runner::OffsetTicks(1000,frequency)),frequency)){cancelled=true;break;}}
            Json observation={{"index",i},{"planned_us",input.at_us},{"first_attempt_us",Runner::Microseconds(first-start,frequency)},{"enqueue_start_us",Runner::Microseconds(before-start,frequency)},{"enqueue_return_us",Runner::Microseconds(after-start,frequency)},{"enqueue_call_us",Runner::Microseconds(after-before,frequency)},{"lateness_us",Runner::Microseconds(before>due?before-due:0,frequency)},{"attempts",attempts},{"accepted",!cancelled&&static_cast<bool>(sent)},{"windows_error",sent.error}};
            report["observations"].push_back(std::move(observation));if(cancelled)break;Check(sent,"Enqueue input report");controls.Sent(input.data);++accepted;report["accepted_count"]=accepted;
            pump();
        }
        report["queue_busy_retries"]=retries;
        stage="drain_driver_queue";const auto drain_deadline=Qpc()+Runner::OffsetTicks(5000000,frequency);
        while(!cancelled){pump();const auto state=details();report["driver_inputs_accepted"]=state.inputs_accepted;report["driver_inputs_completed"]=state.inputs_completed;report["driver_inputs_queued"]=state.queued_inputs;if(state.queued_inputs==0&&state.inputs_completed>=accepted)break;if(cancelledNow()||!timer.Wait(stop.value,std::min(drain_deadline,Qpc()+slice),frequency)){cancelled=true;break;}if(Qpc()>=drain_deadline)throw std::system_error(ERROR_TIMEOUT,std::system_category(),"Accepted reports did not leave the driver queue within 5 seconds; application delivery remains unverified");}
        report["control_requests_served"]=controls_seen;report["other_outputs_observed"]=data_seen;report["elapsed_us"]=Runner::Microseconds(Qpc()-start,frequency);report["status"]=cancelled?"cancelled":"completed";
    }catch(const std::system_error& error){failure=static_cast<DWORD>(error.code().value());report["errors"].push_back({{"stage",stage},{"windows_error",failure},{"message",error.what()}});report["status"]="failed";}
    catch(const std::exception& error){failure=ERROR_GEN_FAILURE;report["errors"].push_back({{"stage",stage},{"windows_error",failure},{"message",error.what()}});report["status"]="failed";}
    if(device.value){const auto removed=Vdevice::Delete_Device(device.value);report["device_removed"]=static_cast<bool>(removed);if(!removed){failure=removed.error?removed.error:ERROR_GEN_FAILURE;report["errors"].push_back({{"stage","delete_owned_device"},{"windows_error",failure},{"message",removed.message}});report["status"]="failed";}}
    report["cancelled"]=cancelled;WriteReport(output,report);std::cout<<"Status: "<<report["status"].get<std::string>()<<". Reports accepted: "<<report["accepted_count"]<<". JSON report saved.\n";
    if(failure)return 1;return cancelled?130:0;
}
}
int wmain(int argc,wchar_t** argv){try{
    std::vector<std::string> arguments;for(int i=1;i<argc;++i)arguments.push_back(Utf8(argv[i]));const auto options=Runner::ParseArguments(arguments);
    if(options.command==Runner::Command::Help){Help();return 0;}
    if(options.command==Runner::Command::Example){std::cout<<Fixture::Serialize(options.example=="mouse"?Fixture::Mouse():Fixture::Keyboard());return 0;}
    if(options.command==Runner::Command::SelfTest){std::cout<<Runner::SelfTest()<<" offline checks passed. No driver opened, device created or input sent.\n";return 0;}
    const auto fixture_path=std::filesystem::u8path(options.fixture);const auto scenario=Fixture::Parse(Read(fixture_path));const auto config=Fixture::Configuration(scenario);Check(Vdevice::Validate_Device(config),"Offline USB/HID configuration validation");
    if(options.command==Runner::Command::Validate){std::cout<<"Valid fixture: "<<scenario.name<<"; "<<scenario.reports.size()<<" reports; "<<scenario.input_bytes<<" input bytes. No driver opened.\n";return 0;}
    const auto report_path=std::filesystem::u8path(options.report);if(SamePath(fixture_path,report_path))throw std::runtime_error("Report path must not overwrite the input fixture.");if(!std::filesystem::is_directory(report_path.has_parent_path()?report_path.parent_path():std::filesystem::current_path()))throw std::runtime_error("Report parent directory does not exist.");
    std::cout<<"LIVE MODE: creating a real virtual HID device. Ctrl+C cancels and removes the owned device.\n";return Live(scenario,report_path);
}catch(const std::exception& error){std::cerr<<"Error: "<<error.what()<<"\n";return 1;}}
