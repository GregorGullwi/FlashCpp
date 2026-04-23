// Test: class template with pack member function template
// Wrapper<int,double> has a member function template that takes its own pack
template<typename... Ts>
struct Wrapper {
int value;

template<typename... Us>
int call(Us... args) {
return (0 + ... + args);
}
};

int main() {
Wrapper<int, double> w;
w.value = 0;
return w.call(10, 15, 17); // = 42
}
