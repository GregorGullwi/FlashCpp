template<typename T>
struct payload_base {
	bool engaged;
};

template<typename T>
struct payload : payload_base<T> {
};

template<typename T>
struct box {
	payload<T> data;
};

int main() {
	box<int> b;
	b.data.engaged = true;
	return b.data.engaged ? 0 : 1;
}
