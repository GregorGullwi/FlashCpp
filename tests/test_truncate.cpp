int main() { long long big = 0x123456789ABCDEF0LL; int truncated = (int)big; return truncated & 0xFF; }
