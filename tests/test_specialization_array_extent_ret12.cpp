template<class T> struct Box { T value; };
template<class T> struct Row { Box<T> items[2]; };

constexpr Row<int> row{{{3}, {9}}};

int main() {
	return row.items[0].value + row.items[1].value;
}
