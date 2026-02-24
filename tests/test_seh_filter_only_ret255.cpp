// Minimal test for non-constant filter funclet with hardware exception
// test_filter_expression(0) triggers an access violation with a non-constant filter

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0

int test_filter_expression(int* ptr) {
    __try {
        int value = *ptr;
        return value;
    }
    __except(ptr == 0 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return -1;
    }
}

int main() {
    // -1 as unsigned 32-bit = 0xFFFFFFFF, & 0xFF = 255
    return test_filter_expression(0);
}
