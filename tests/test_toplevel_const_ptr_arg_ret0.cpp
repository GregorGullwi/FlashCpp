// Test: void* const argument passed to void* parameter (top-level const on pointer)
// Top-level const on a pointer means the pointer variable itself is const,
// but the value can still be copied to a non-const parameter.
extern "C" {
    void* __cdecl memset(void* _Dst, int _Val, unsigned __int64 _Size);
}

static int test_func(void* const _Destination, unsigned __int64 const _DestinationSize)
{
    // void* const -> void* should be allowed (top-level const is irrelevant for pass-by-value)
    memset(_Destination, 0, _DestinationSize);
    return 0;
}

int main() {
    char buf[16];
    return test_func(buf, 16);
}
