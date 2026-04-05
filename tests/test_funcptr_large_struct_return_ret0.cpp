struct Big {
	long long first;
	long long second;
	long long third;
};

Big makeBig(long long seed) {
	return Big{seed, seed + 1, seed + 2};
}

int main() {
	Big (*fp)(long long) = makeBig;
	Big value = fp(10);
	return (value.first == 10 && value.second == 11 && value.third == 12) ? 0 : 1;
}
