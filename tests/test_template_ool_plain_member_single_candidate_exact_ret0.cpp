template<class T>
struct PlainSingleCandidateExact {
	int eval(int declared);
};

template<class T>
int PlainSingleCandidateExact<T>::eval(int defined) {
	return defined + 1;
}

int main() {
	PlainSingleCandidateExact<long> value;
	return value.eval(41) == 42 ? 0 : 1;
}
