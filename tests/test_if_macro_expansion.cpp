// Test macro expansion in #if directives
#define MY_VALUE 300

#if MY_VALUE > 200
    int result = 1;  // Should be included
#else
    int result = 0;  // Should be skipped
#endif

int main() {
    return result;
}
