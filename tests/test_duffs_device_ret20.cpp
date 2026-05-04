int main() {
	const int count = 20;
	int from[20];
	int to[20] = {};

	for (int i = 0; i < count; ++i) {
		from[i] = i + 1;
	}

	int* src = from;
	int* dst = to;
	int n = (count + 7) / 8;

	switch (count % 8) {
	case 0:
		do {
			*dst++ = *src++;
	case 7:
			*dst++ = *src++;
	case 6:
			*dst++ = *src++;
	case 5:
			*dst++ = *src++;
	case 4:
			*dst++ = *src++;
	case 3:
			*dst++ = *src++;
	case 2:
			*dst++ = *src++;
	case 1:
			*dst++ = *src++;
		} while (--n > 0);
	}

	int sum = 0;
	for (int i = 0; i < count; ++i) {
		sum += to[i];
	}

	return sum == 210 ? 20 : 1;
}
