struct MyStruct {
    int value;
};

int test_is_pointer() {
    int result = 0;
    if (!__is_pointer(MyStruct)) result += 16;
    return result;
}

int another_function() {
    return 0;
}

int main() {
    return 0;
}
