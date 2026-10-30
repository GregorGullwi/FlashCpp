// A member partial-specialization body may use parameters from the enclosing
// class template. Member substitution must combine the effective owner bindings
// with the partial's deduced bindings instead of using only the local ones.
template<class T> struct Owner {
	template<class U> struct Box { U value; };
	template<class U> struct Box<U*> {
		T owner;
		U* pointer;
	};
};

struct Payload {
	short first;
	int second;
};

int main() {
	Owner<short>::Box<int*> narrow{};
	Owner<long long>::Box<int*> wide{};
	Owner<Payload>::Box<int*> structured{};
	narrow.pointer = nullptr;
	wide.pointer = nullptr;
	structured.pointer = nullptr;
	narrow.owner = 1;
	wide.owner = 2;
	structured.owner.first = 3;
	structured.owner.second = 4;
	return static_cast<int>(narrow.owner + wide.owner + structured.owner.first + structured.owner.second) - 10;
}
