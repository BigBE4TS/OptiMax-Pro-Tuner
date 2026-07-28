# OptiMax Pro-Tuner — source

Full source for OptiMax Pro-Tuner v4.2.0. C++17, Win32 + Dear ImGui + D3D11,
header-only apart from `OptiMax_Profiler.cpp`.

## Building

Requires Visual Studio with the C++ desktop workload and CMake. The bundled
presets use Ninja:

```
cmake --preset x64-release
cmake --build out/build/x64-release
```

Or open the folder in Visual Studio and pick the `x64 Release` configuration.

The build produces a single self-contained `OptiMax_Profiler.exe`. It
self-elevates on launch, so run it from a normal shell and accept the UAC
prompt rather than starting it elevated.

## Layout

| | |
|---|---|
| `OptiMax_Profiler.cpp` | entry point, window, D3D11 setup, persistent state |
| `OptiMaxApp.h` | the UI and the telemetry/auto-boost worker loop |
| `OptiMaxEngine.h` | timer resolution, process priority, memory, restore points |
| `OptiMaxExperimental.h` | MSI mode, GPU IRQ pinning, TCP, FSE, GameDVR, MMCSS |
| `OptiMaxDebloat.h` | telemetry, services, privacy, DISM/SFC health repair |
| `OptiMaxDiscord.h` | Discord debloat |
| `OptiMaxUpdater.h` | self-update: version check, download, checksum, swap |
| `OptiMaxAuth.h` / `OptiMaxCloudSync.h` | accounts and community profiles |
| `imgui/` | Dear ImGui, unmodified |

## Notes

Headers are not fully self-contained — `OptiMaxExperimental.h` expects
`OptiMaxEngine.h` and `OptiMaxDebloat.h` to be included first. Include order in
`OptiMaxApp.h` is load-bearing; `<winsock2.h>` must precede `<windows.h>`.

The Supabase key embedded in `OptiMaxAuth.h` is the publishable (anon) key,
which is public by design — access is controlled by server-side row-level
security, not by that key. The backend schema and its RLS policies are not
published here.
