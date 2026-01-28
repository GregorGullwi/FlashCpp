template<int N, bool B>
struct MultiParam {
    int data[N];
    bool flag = B;
};

// Expected return: 0
int main() {
    MultiParam<3, true> mp;
    return mp.flag ? 0 : 1;
}