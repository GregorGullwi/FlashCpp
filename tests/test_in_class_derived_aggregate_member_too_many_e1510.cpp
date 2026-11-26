struct Base { int value; };
struct Derived : Base { int extra; };
struct Holder { Derived storage{11, 12, 13}; };

int main() {
	Holder holder;
	return holder.storage.extra;
}
