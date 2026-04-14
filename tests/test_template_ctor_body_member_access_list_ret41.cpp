int gDataValue = -1;

struct ListInitNoop {
	int data;

	template<typename T>
	ListInitNoop(T v) {
		data = (int)v;
	}

	~ListInitNoop() {
		gDataValue = data + 30;
	}
};

int main() {
	{
		ListInitNoop list{11};
	}
	return gDataValue;
}
