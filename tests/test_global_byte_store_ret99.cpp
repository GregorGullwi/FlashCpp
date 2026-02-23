unsigned char g_byte = 0;
short g_short = 0;

int main() {
    g_byte = 42;
    g_short = 1000;
    return (g_byte == 42 && g_short == 1000) ? 99 : 0;
}
