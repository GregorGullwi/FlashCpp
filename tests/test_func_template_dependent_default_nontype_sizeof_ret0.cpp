template<typename Type, int Size = sizeof(Type)>
int getSize(Type) {
	return Size;
}

int main() {
	return getSize(42) == static_cast<int>(sizeof(int)) ? 0 : 1;
}
