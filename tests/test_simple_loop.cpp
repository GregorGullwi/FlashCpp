// Simple for-loop test
extern "C" int printf(const char* format, ...);

int main() {
    printf("Before loop\n");
    
    for (int i = 0; i < 3; i = i + 1) {
        printf("i = %d\n", i);
    }
    
    printf("After loop\n");
    return 0;
}
