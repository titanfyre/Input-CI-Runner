# Input CI Runner

**A small C++17 Windows x64 CLI for validated, scheduled virtual HID fixtures.**
Create repeatable input test scenarios for your own Windows applications and CI
machines using [Vdevice](https://github.com/TitanCSA/Vdevice). Fixture validation
is offline. Sending real input requires the explicit `--live` option.

For commercial production-driver bundles, OEM deployment and integration support,
add **titancsa** on Discord. This public project includes application/SDK source;
the kernel driver source is private. Microsoft signing is not an endorsement or
a promise that every device or application is compatible.

## Install and run

1. Download and extract the complete release ZIP.
2. For **offline validation**, no driver or administrator rights are needed.
3. For **live input**, run `Dependencies/VdeviceSetup.exe`, choose Install, and
   restart Windows only if setup requests it. The bundled signed Vdevice demo is
   limited to **one hour** by its driver policy. This app does not reset it.
4. Open an administrator terminal on an authorized test PC. Input affects Windows;
   close sensitive applications and use an isolated test environment.

```powershell
# Offline; neither command opens the driver:
.\InputCIRunner.exe validate .\Examples\mouse.json
.\InputCIRunner.exe .\Examples\keyboard.json

# Print a complete fixture to stdout:
.\InputCIRunner.exe example mouse
.\InputCIRunner.exe example keyboard

# Explicit live operation; Ctrl+C cancels and removes this run's device:
.\InputCIRunner.exe run .\Examples\mouse.json --live --report .\result.json
```

Exit codes are `0` for success, `1` for validation/runtime/report-write errors,
and `130` for cancellation. Error messages are English. Live operation writes a
JSON report, including failures after device creation. A report-write failure is
printed to stderr. The report path must differ from the input fixture.

## Fixture format

The schema matches HID Fixture Studio. It describes one full-speed USB HID
interface with endpoint `0x81`, a raw HID report descriptor and scheduled report
bytes. Start with `Examples/mouse.json` or `Examples/keyboard.json`.

```json
{
  "schema": 1,
  "name": "My input fixture",
  "speed": "full",
  "vid": 65535,
  "pid": 16,
  "input_bytes": 7,
  "report_descriptor": "... valid HID report descriptor bytes ...",
  "reports": [
    {"at_us": 0, "hex": "00 00 00 00 00 00 00"}
  ]
}
```

The shortened descriptor above is explanatory, not a valid fixture. Real example
files contain complete descriptors. Report times are offsets from the QPC start
**after Windows finishes configuration**, in nondecreasing microseconds. Equal
times request consecutive enqueue attempts; they do not guarantee simultaneous
delivery. Use only VID/PID identities you are authorized to use; sample IDs are
local test placeholders, not assigned production identities.

Validation rejects unknown/duplicate JSON fields, deeply nested or oversized
JSON, invalid descriptor framing, inconsistent input lengths, undefined report
IDs, schedules beyond one hour and more than 10,000 reports. JSON is limited to
4MiB, report descriptors to 4,096 bytes and full-speed input reports to 64 bytes.
This bounded schema does not emulate arbitrary USB topologies or every protocol.

## What live mode does

The runner validates the fixture and SDK configuration before touching the
driver. It creates one owned device, serves delegated HID report/idle/protocol
control requests and waits up to ten seconds for Windows to plug, configure and
power it. Unsupported control requests are stalled explicitly.

Scheduling uses QPC and a high-resolution waitable timer when Windows supports
it, with a normal waitable-timer fallback. There is no busy-spin or global timer
resolution change. Control events are serviced between reports and at most
10ms apart while otherwise waiting. A full input queue is retried using 1ms
waits, bounded by the last scheduled time plus ten seconds. Windows scheduling,
USB polling and queue pressure can all make an actual enqueue late.

At the end it waits up to five seconds for accepted reports to leave the driver
queue, then explicitly deletes its device. The SDK's owned-device lifetime is a
second cleanup mechanism. Ctrl+C interrupts waits. Forced process termination or
power loss cannot run user-mode destructors; driver session cleanup still applies.

## Read the JSON report

The report includes accepted count, cancellation/errors, timer mode, enumeration
wait, QPC frequency, queue retries, driver-observed accepted/completed/queued
counts and one observation per attempted scheduled report:

- `planned_us`: fixture timestamp.
- `first_attempt_us`: first observed enqueue attempt.
- `enqueue_start_us` / `enqueue_return_us`: QPC observations around the last SDK
  enqueue call for that report.
- `enqueue_call_us`: elapsed time inside that SDK call.
- `lateness_us`: how late that final attempt started compared with the schedule.
- `attempts`, `accepted`, `windows_error`: queue/retry outcome.

These are observations at the enqueue boundary. **Acceptance is not USB/HID
completion, application receipt, game latency or display latency.** Even the
driver completion counter does not prove that an application processed an input.
Do not compare these numbers with hardware input-to-photon measurements.

## Build and test

Install Visual Studio 2022 C++ tools, Windows SDK and CMake. No .NET or Python is
needed at runtime. The SDK is compiled from `SDK/Vdevice.cpp` and `Vdevice.h`.

```powershell
cmake -S . -B obj/Build -A x64
cmake --build obj/Build --config Release --parallel
ctest --test-dir obj/Build -C Release --output-on-failure
.\obj\Build\bin\InputCIRunner.exe --self-test
```

`-DENABLE_ASAN=ON` enables AddressSanitizer for a separate offline test build.
Compilation uses C++17, `/W4 /WX`, CFG and the static C++ runtime in the normal
build. `SDK_DIRECTORY` can be overridden privately for the matching production
Vdevice SDK; production-driver distribution and activation remain separate.

Tests are offline: argument safety, scheduling arithmetic, fixture/SDK validation,
raw HID control behavior and model stress tests. Live hardware/Windows behavior
must be verified on the intended isolated VM or test PC. This release does not
claim that live behavior was tested merely because offline tests passed.

## Responsible use

Use only on systems and applications you own or are authorized to test. This
project is intended for legitimate QA, development and accessibility work. You
remain responsible for your use, applicable rules and any effects on your system.
The author does not authorize illegal use or accept responsibility for misuse.
