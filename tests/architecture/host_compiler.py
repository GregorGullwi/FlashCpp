"""Resolve the host compiler used by the native architecture regressions.

Windows deliberately ignores PATH here. The MSVC STL rejects Clang versions
older than the toolset Visual Studio ships with (error STL1000 since VS 2026),
and the standalone LLVM installed on CI images is not kept in lockstep with it.
Visual Studio's ClangCL is: Microsoft updates the bundled Clang together with
the STL it links against, and that toolset is the one the repository's Windows
project already selects, so discovery goes through vswhere.
"""

import os
import pathlib
import subprocess


# Visual Studio installs the x64 toolset under Llvm\x64\bin; the Llvm\bin layout
# is the older 32-bit-hosted one. Prefer the x64 toolset when both exist.
VS_CLANG_CL_PATHS = (
    r"VC\Tools\Llvm\x64\bin\clang-cl.exe",
    r"VC\Tools\Llvm\bin\clang-cl.exe",
)

_clang_cl = None


def windows_clang_cl():
    """Absolute path to Visual Studio's ClangCL driver."""
    global _clang_cl
    if _clang_cl is not None:
        return _clang_cl
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = pathlib.Path(program_files) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        raise RuntimeError("vswhere.exe not found at " + str(vswhere))
    for relative in VS_CLANG_CL_PATHS:
        completed = subprocess.run(
            [str(vswhere), "-latest", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Llvm.Clang",
             "-find", relative],
            capture_output=True, text=True, check=True)
        found = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
        if found:
            _clang_cl = found[0]
            return _clang_cl
    raise RuntimeError(
        "Visual Studio's C++ Clang toolset is not installed; add the "
        "\"C++ Clang Compiler for Windows\" component, or run the regression on "
        "Linux where clang++ is used directly")
