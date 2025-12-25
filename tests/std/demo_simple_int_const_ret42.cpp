// Simplest possible test
template<int v>
struct MyConst {
static constexpr int value = v;
};

int main() {
return MyConst<42>::value;
}
