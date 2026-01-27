template<int N, bool B, char C>
struct MultiType {
    int data[N];
    bool flag = B;
    char ch = C;
};

MultiType<3, true, 'A'> mt;

int main() {
    return 0;
}