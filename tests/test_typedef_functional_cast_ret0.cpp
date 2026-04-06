typedef unsigned long size_t_test;
typedef long long streamoff_test;

int main() {
    size_t_test a = size_t_test(-1);
    if (a == 0) return 1;
    
    streamoff_test b = streamoff_test(42);
    if (b != 42) return 2;
    
    size_t_test c = size_t_test(100);
    if (c != 100) return 3;
    
    return 0;
}
