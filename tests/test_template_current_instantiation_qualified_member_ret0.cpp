template<typename T>
struct Box {
T value{};

T get() const {
return Box<T>::value;
}
};

int main() {
Box<int> box;
box.value = 42;
return box.get() - 42;
}
