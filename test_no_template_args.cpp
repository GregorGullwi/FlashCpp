template<>
int identity<int>(int x) {
return x;
}
int main() {
int result;
result = identity(42);
return 0;
}
