template<long L>
struct LongParam {
    long value = L;
};

// Expected return: 0
int main() {
    LongParam<123456789L> lp;
    return lp.value == 123456789L ? 0 : 1;
}