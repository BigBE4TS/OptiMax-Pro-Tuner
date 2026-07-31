cmake_minimum_required(VERSION 3.10)
project(OptiMax_Profiler)

# Set C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include Directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/imgui)

# Collect Source Files
set(SOURCES
    OptiMax_Profiler.cpp
    OptiMaxEngine.h
    OptiMaxProfiler.h
    OptiMaxAutoBoost.h
    OptiMaxHardware.h
    OptiMaxDebloat.h
    OptiMaxPower.h
    OptiMaxDiagnostics.h
    OptiMaxBenchmark.h
    OptiMaxStateCache.h
    resource.h
    resource.rc
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_impl_win32.cpp
    imgui/imgui_impl_dx11.cpp
)

# Build Executable (WIN32 flag hides the console window)
add_executable(OptiMax_Profiler WIN32 ${SOURCES} "OptiMaxUi_Widgets.h" "OptiMaxExperimental.h"    "OptiMaxApp.h" "OptiMaxFileDialog.h" "OptiMaxUpdater.h" "OptiMaxVerifier.h" "OptiMaxGlobals.h" "OptiMaxCloudSync.h" "OptiMaxJson.h" "OptiMaxAuth.h" "OptiMaxImage.h" "OptiMaxDiscord.h" "OptiMaxUWP.h" "OptiMaxAttribution.h" "OptiMaxSelfGovernor.h" "OptiMaxChangelog.h" "OptiMaxUi_Cards.h")

# Link Windows System Libraries
target_link_libraries(OptiMax_Profiler 
    d3d11 
    d3dcompiler 
    dwmapi 
    setupapi 
    powrprof 
    ntdll 
    cfgmgr32 
    advapi32 
    srclient 
    psapi
    winmm
    wbemuuid
    winhttp         # Supabase REST calls (OptiMaxAuth.h)
    crypt32         # DPAPI refresh-token encryption (OptiMaxAuth.h)
    windowscodecs   # WIC avatar decoding (OptiMaxImage.h)
    wevtapi         # Event Log queries for stall attribution (OptiMaxAttribution.h)
)

# Set Optimization Flags for Release Build
if(MSVC)
    # These were applied to every configuration, which made the Debug preset
    # impossible to build: MSVC rejects /O2 together with the /RTC1 that Debug
    # adds by default, and fails outright with "command line error D8016". The
    # generator expressions scope optimisation to the configurations that want
    # it, and pick the matching static runtime rather than forcing /MT onto a
    # Debug build that CMake has already given /MDd.
    #
    # Note the semicolons: a generator expression is ONE argument, so
    # $<...:/O2 /Oi /Gy> would be passed to cl.exe as a single quoted string.
    target_compile_options(OptiMax_Profiler PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/O2;/Oi;/Gy;/MT>
        $<$<CONFIG:Debug>:/MTd>
    )
    set_target_properties(OptiMax_Profiler PROPERTIES LINK_FLAGS "/INCREMENTAL:NO /SUBSYSTEM:WINDOWS")
endif()