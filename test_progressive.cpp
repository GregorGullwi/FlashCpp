struct MyStruct {
    int value;
};

class MyClass {
public:
    int value;
};

enum Color { Red, Green, Blue };
enum class ScopedColor { Red, Green, Blue };

int test_is_void() {
    return __is_void(void) ? 1 : 0;
}

int test_is_integral() {
    int result = 0;
    if (__is_integral(int)) result += 1;
    if (__is_integral(char)) result += 2;
    if (__is_integral(bool)) result += 4;
    if (__is_integral(long)) result += 8;
    if (__is_integral(unsigned int)) result += 16;
    if (!__is_integral(float)) result += 32;
    if (!__is_integral(double)) result += 64;
    return result;
}

int test_is_floating_point() {
    int result = 0;
    if (__is_floating_point(float)) result += 1;
    if (__is_floating_point(double)) result += 2;
    if (!__is_floating_point(int)) result += 4;
    if (!__is_floating_point(char)) result += 8;
    return result;
}

int test_is_pointer() {
    int result = 0;
    if (__is_pointer(int*)) result += 1;
    if (__is_pointer(void*)) result += 2;
    if (__is_pointer(MyStruct*)) result += 4;
    if (!__is_pointer(int)) result += 8;
    if (!__is_pointer(MyStruct)) result += 16;
    return result;
}

int main() {
    return 0;
}

int test_is_reference() {
    int result = 0;
    if (__is_lvalue_reference(int&)) result += 1;
    if (__is_rvalue_reference(int&&)) result += 2;
    if (!__is_lvalue_reference(int)) result += 4;
    if (!__is_lvalue_reference(int*)) result += 8;
    return result;
}
