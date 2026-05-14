namespace Alpha {
	template <typename T>
	struct Box {
		static int value() {
			return 42;
		}
	};
}

namespace Beta {
	template <typename T>
	struct Box {
		static int value() {
			return 7;
		}
	};
}

template <typename T>
int call_box_value() {
	return Alpha::Box<int>::value();
}

int main() {
	return call_box_value<char>();
}
