struct S {
	int value;
};

int main() {
	S objects[1] = {{42}};
	int S::* member = &S::value;
	return objects->*member;
}
