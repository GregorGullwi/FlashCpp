struct MyStruct {
    int value;
};

int test_is_pointer() {
    int result = 0;
    if (__is_pointer(int*)) result += 1;
    if (__is_pointer(void*)) result += 2;
    if (__is_pointer(MyStruct*)) result += 4;
    if (!__is_pointer(int)) result += 8;
    if (!__is_pointer(MyStruct)) result += 16;
    return result;
}

int test_is_reference() {
    int result = 0;
    if (__is_lvalue_reference(int&)) result += 1;
    return result;
}

int main() {
    return 0;
}
