struct Big {
	long long first;
	long long second;
	long long third;
};

Big makeBig(long long seed) {
	return Big{seed, seed + 1, seed + 2};
}

long long sumBig(Big value) {
	return value.first + value.second + value.third;
}

int main() {
	Big (*fp)(long long) = makeBig;
	long long total = sumBig(fp(10));
	return total == 33 ? 0 : 1;
}
