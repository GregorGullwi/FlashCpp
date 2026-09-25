template <typename T>
struct OrderedHolder {
	static inline T (*(*value)[3])[4] = nullptr;
};

template <typename T>
struct OrderedHolder<T*> {
	static inline T (*(*value)[3])[4] = nullptr;
};

int main() {
	OrderedHolder<int*> object;
	return OrderedHolder<int*>::value == nullptr && object.value == nullptr ? 42 : 0;
}
