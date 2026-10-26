"""Run only SD/IMU logic, without a board, card erase or NVS writes.

Use a host C++ compiler, or install Zig into the ignored build directory:
python -m pip install --target .pio/host-tools ziglang
python tools/test_storage_imu.py
"""
from pathlib import Path
import shutil
import subprocess
import os
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--suite", choices=["storage-imu", "settings"], default="storage-imu")
suite = parser.parse_args().suite
header, function = ("settings_selftest.h", "runSettingsSelfTests") if suite == "settings" else ("storage_imu_selftest.h", "runStorageImuSelfTests")

root = Path(__file__).resolve().parents[1]
build = root / ".pio" / "host-tests"
build.mkdir(parents=True, exist_ok=True)
zig = root / ".pio" / "host-tools" / "ziglang" / "zig.exe"
native = shutil.which("g++") or shutil.which("clang++")
if not zig.exists() and not native:
    raise SystemExit("Host C++ compiler missing; install Zig as described in this script.")
compiler = [str(zig), "c++"] if zig.exists() else [native]
(build / "Arduino.h").write_text(
    "#pragma once\n#include <cstdio>\n"
    "class Stream { public: template<class... A> int printf(const char* f,A... a)"
    " {return std::printf(f,a...);} };\n", encoding="utf-8"
)
(build / "runner.cpp").write_text(
    '#include <cstdlib>\n#include <new>\n#include <cstdio>\n#include <cstdarg>\n'
    '#ifdef ZIG_HOST_TEST\nextern "C" void __main(){}\n'
    'extern "C" int printf(const char* f,...){va_list a;va_start(a,f);'
    'int r=__stdio_common_vfprintf(0,__acrt_iob_func(1),f,nullptr,a);va_end(a);return r;}\n'
    'extern "C" int snprintf(char* b,std::size_t n,const char* f,...){va_list a;va_start(a,f);'
    'int r=__stdio_common_vsprintf(2,b,n,f,nullptr,a);va_end(a);return r;}\n#endif\n'
    'void* operator new(std::size_t n){void* p=std::malloc(n);if(!p)std::abort();return p;}\n'
    'void operator delete(void* p) noexcept {std::free(p);}\n'
    'void operator delete(void* p,std::size_t) noexcept {std::free(p);}\n'
    f'#include "{header}"\n'
    f'int main(){{Stream output;return {function}(output)?0:1;}}\n',
    encoding="utf-8",
)
binary = build / "storage_imu.exe"
environment = os.environ.copy()
environment["ZIG_GLOBAL_CACHE_DIR"] = str(build / "zig-global-cache")
environment["ZIG_LOCAL_CACHE_DIR"] = str(build / "zig-local-cache")
with (build / "compile.log").open("w", encoding="utf-8") as diagnostics:
    common = ["-std=c++17", "-O1", "-I" + str(build), "-I" + str(root / "src"),
        "-I" + str(root / "include"), str(build / "runner.cpp")]
    if zig.exists():
        # Compile using the bundled standard headers, then link this small
        # header-only test directly to Windows UCRT instead of building a CRT.
        obj = build / "runner.obj"
        commands = [compiler + common + ["-fno-exceptions", "-fno-rtti",
            "-mno-stack-arg-probe", "-D_UCRT=1", "-D__USE_MINGW_ANSI_STDIO=0", "-DZIG_HOST_TEST",
            "-c", "-o", str(obj)],
            [str(zig), "cc", "-nostdlib", str(obj), "-Wl,-e,main",
             "-lucrtbase", "-lkernel32", "-o", str(binary)]]
    else:
        commands = [compiler + common + ["-o", str(binary)]]
    for command in commands:
        compiled = subprocess.run(command, cwd=root, env=environment,
            stdout=diagnostics, stderr=subprocess.STDOUT)
        if compiled.returncode:
            break
if compiled.returncode:
    print((build / "compile.log").read_text(encoding="utf-8", errors="replace")[-6000:])
    raise SystemExit(compiled.returncode)
subprocess.run([str(binary)], check=True, cwd=root)
