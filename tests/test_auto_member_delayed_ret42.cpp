struct Box {
	int value;
	auto twice() const { return value * 2; }
};

int main() {
	Box box{21};
	return box.twice();
}