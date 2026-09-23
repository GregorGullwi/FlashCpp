template <class T = int> using J = T;
template <class T = short> using Small = T;

struct Box {
	int value;
};

J<> number = 37;
::J<> other = 1;
Small<> small = 2;

int main() {
	J<Box> box{2};
	return number + other + small + box.value;
}
