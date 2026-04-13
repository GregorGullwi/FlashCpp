template <unsigned...>
int select(int value) {
return 1;
}

int select(int value) {
return value;
}

int main() {
return select(0);
}
