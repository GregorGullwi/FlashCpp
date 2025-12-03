struct MyStruct {
    int value;
};

int test_is_pointer() {
    int result = 0;
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
