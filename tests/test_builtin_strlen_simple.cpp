// Simple test for __builtin_strlen
extern "C" const char* get_string();

int main() {
    const char* str = get_string();
    return __builtin_strlen(str);
}
