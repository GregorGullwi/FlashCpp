// Bug: new/delete generates incorrect allocation sizes in larger translation units
// Status: RUNTIME CRASH (segfault) - new allocates wrong size in combined files
// Date: 2026-02-07
//
// When new int(42) is used in a file with many other functions and classes,
// the generated code attempts to allocate an enormous amount of memory
// (observed: 139TB via mmap), causing a segfault.
//
// The same new/delete code works correctly in a standalone small file.

int helper1() { return 42; }
int helper2() { return 100; }
int helper3(int a, int b) { return a + b; }

class Dummy {
public:
    int x;
    Dummy() : x(0) {}
};

int main() {
    int* p = new int(42);
    int val = *p;
    delete p;

    int* arr = new int[5];
    arr[0] = 10;
    arr[1] = 20;
    int sum = arr[0] + arr[1];
    delete[] arr;

    Dummy d;
    int r = helper1() + helper2() + helper3(1, 2) + d.x;
    return (val == 42) && (sum == 30) && (r == 145) ? 0 : 1;
}

// Expected behavior (with clang++/g++):
// Compiles and runs successfully, returns 0
//
// Actual behavior (with FlashCpp):
// Compiles and links without errors, but segfaults at runtime.
// strace shows: mmap(NULL, 139400265555968, ...) = -1 ENOMEM
// The allocation size argument to operator new is corrupted.
//
// Note: The same new/delete code in a standalone file (no other functions)
// works correctly. The bug only manifests when combined with other
// function/class definitions in the same translation unit.
//
// Fix: Investigate how the allocation size argument is computed in the
// code generator. The issue is likely a stack offset or register
// clobbering bug that only appears with larger function counts.
