// Test file for __countof_helper construct
// This uses a technique to get array size at compile time

#define _UNALIGNED

template <typename _CountofType, size_t _SizeOfArray>
char (*__countof_helper(_UNALIGNED _CountofType (&_Array)[_SizeOfArray]))[_SizeOfArray];

#define __crt_countof(_Array) (sizeof(*__countof_helper(_Array)) + 0)

int main() {
    int arr[10];
    int size = __crt_countof(arr);
    return size;
}
