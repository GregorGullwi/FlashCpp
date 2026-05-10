namespace stdlike {
template<typename Type>
void swap(Type&, Type&) = delete;

void swap(int& left, int& right) {
int temp = left;
left = right;
right = temp;
}

template<typename Type>
struct pair {
Type first;

void swap_with(pair& other) {
using stdlike::swap;
swap(first, other.first);
}
};
}

int main() {
	stdlike::pair<int> left{1};
	return left.first == 1 ? 0 : 1;
}
