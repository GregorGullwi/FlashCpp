int gValue = 0;

struct ListInitNoop {
	template<typename T>
	ListInitNoop(T) {}

	~ListInitNoop() {
		gValue = 42;
	}
};

int main() {
	{
		ListInitNoop value{7};
	}
	return gValue;
}
