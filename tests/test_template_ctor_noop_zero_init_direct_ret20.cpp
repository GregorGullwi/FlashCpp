int gDataValue = -1;

struct DirectInitNoop {
	int data;

	template<typename T>
	DirectInitNoop(T v) {
		data = (int)v;
	}

	~DirectInitNoop() {
		gDataValue = data + 20;
	}
};

int main() {
	{
		DirectInitNoop direct(7);
	}
	return gDataValue;
}
