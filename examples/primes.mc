// Prints the prime numbers below 50.
int is_prime(int n) {
    if (n < 2) {
        return 0;
    }
    int d = 2;
    while (d * d <= n) {
        if (n % d == 0) {
            return 0;
        }
        d = d + 1;
    }
    return 1;
}

int main() {
    int n = 0;
    while (n < 50) {
        if (is_prime(n)) {
            print(n);
        }
        n = n + 1;
    }
    return 0;
}
