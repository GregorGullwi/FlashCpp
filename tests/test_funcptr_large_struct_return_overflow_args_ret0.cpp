struct Big {
	long long first;
	long long second;
	long long third;
};

Big makeBig(int a, int b, int c, int d, int e, int f, int g) {
	return Big{
		static_cast<long long>(a + b),
		static_cast<long long>(c + d),
		static_cast<long long>(e + f + g),
	};
}

int main() {
	Big (*fp)(int, int, int, int, int, int, int) = makeBig;
	Big value = fp(1, 2, 3, 4, 5, 6, 7);
	return (value.first == 3 && value.second == 7 && value.third == 18) ? 0 : 1;
}
