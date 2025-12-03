struct MyStruct {
    int value;
};

int test1() {
    int result = 0;
    if (!__is_pointer(MyStruct)) result += 16;
    return result;
}

int test2() {
    int result = 0;
    if (__is_lvalue_reference(int&)) result += 1;
    return result;
}

int main() {
    return 0;
}
