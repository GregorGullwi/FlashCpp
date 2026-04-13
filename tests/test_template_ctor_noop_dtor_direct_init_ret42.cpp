int gValue = 0;

struct DirectInitNoop {
	template<typename T>
	DirectInitNoop(T) {}

	~DirectInitNoop() {
		gValue = 42;
	}
};

int main() {
	{
		DirectInitNoop value(7);
	}
	return gValue;
}
