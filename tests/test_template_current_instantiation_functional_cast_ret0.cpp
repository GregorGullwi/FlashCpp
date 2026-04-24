template<typename T>
struct Box {
T value;

Box() : value(42) {}

int run() {
return Box<T>().value;
}
};

int main() {
Box<int> box;
return box.run() == 42 ? 0 : 1;
}
