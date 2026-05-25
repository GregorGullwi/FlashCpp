template<typename U>
int id(U value) {
return value == 42 ? 0 : 1;
}

template<typename T>
struct Wrapper {
using value_type = T;

static int call(value_type v) {
return id(v);
}
};

int main() {
return Wrapper<int>::call(42);
}
