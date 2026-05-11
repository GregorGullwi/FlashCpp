template<class T>
struct Base {
	Base() {}
	template<class U>
	Base(U&&) {}
};

template<class T>
struct Box : Base<T> {
	Box() {}
	template<class... U>
	Box(U&&... u) : Base<T>(static_cast<U&&>(u)...) {}
};

int main() {
	Box<int>* p = nullptr;
	return p != nullptr;
}
