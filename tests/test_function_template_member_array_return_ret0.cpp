template<class T>
struct ReturnMemberArrayTemplateBox {
	T data[3];

	template<class U = void>
	T* get() {
		return data;
	}
};

int main() {
	ReturnMemberArrayTemplateBox<int> box;
	box.data[1] = 42;
	int* pointer = box.get();
	return pointer[1] == 42 ? 0 : 1;
}
