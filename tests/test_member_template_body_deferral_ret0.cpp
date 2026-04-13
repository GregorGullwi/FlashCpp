template <typename T>
struct Box {
T value;

Box()
: value() {}

template <typename U>
Box(const Box<U>& other)
: value() {
emplace(static_cast<T>(other.value));
}

template <typename... Args>
void emplace(Args&&... args) {
value = T(args...);
}
};

int main() {
Box<int> src;
src.value = 7;
Box<long> dst(src);
return dst.value == 7 ? 0 : 1;
}
