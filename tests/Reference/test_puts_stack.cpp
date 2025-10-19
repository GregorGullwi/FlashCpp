int puts(const char* str);

int main() {
    // Test 1: Stack-based char array
    char msg[6];
    msg[0] = 'H';
    msg[1] = 'e';
    msg[2] = 'l';
    msg[3] = 'l';
    msg[4] = 'o';
    msg[5] = '\0';
    puts(msg);

    // Test 2: String literal from .rdata section
    puts("World");

    return 0;
}

