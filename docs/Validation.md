# Validation scope

The application was compiled for Windows x64 with Visual Studio C++17 and
`/W4 /WX`. Four offline CTest suites passed in each of these builds:

| Build | Result |
| --- | --- |
| Bundled demo Vdevice SDK, normal static-runtime build | 4/4 passed |
| Bundled demo SDK, AddressSanitizer build | 4/4 passed |
| Matching private production SDK override, separate private build | 4/4 passed |

`InputCIRunner --self-test` performs 20,035 assertions. The model test executable
adds three checks for HID output-report control state. Coverage includes argument
opt-in, invalid arguments, QPC rounding/monotonicity over 10,000 schedule offsets,
fixture/USB validation and HID GET/SET/idle/protocol behavior. Mouse and keyboard
JSON files were independently accepted by the offline executable.

The fixture model matches HID Fixture Studio byte for byte. The public Vdevice
SDK remains unchanged from the demo project. Private production SDK files and
binaries are excluded from the public distribution.

No `--live` command, driver installation, driver activation, virtual device
creation or input injection was performed during these checks. The live USB/HID
enumeration, delegated control path, cancellation, queue drain and cleanup still
require verification on the target isolated VM or hardware. Offline test success
is not evidence of application delivery timing or universal compatibility.
