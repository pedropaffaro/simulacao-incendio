CC     = gcc
CFLAGS = -Wall -Wextra -O2 -fopenmp
INC    = -I include
SRC    = src/funcs.c

all: fire_seq fire_omp

fire_seq: src/fire_seq.c $(SRC)
	$(CC) $(CFLAGS) $(INC) $^ -o $@

fire_omp: src/fire_omp.c $(SRC)
	$(CC) $(CFLAGS) $(INC) $^ -o $@

define run_tests
	@pass=0; fail=0; got=$$(mktemp); exp=$$(mktemp); \
	for in_file in tests/in/*.in; do \
		name=$$(basename $$in_file .in); \
		./$(1) $$in_file | grep -v '^tempo:' > $$got; \
		grep -v '^tempo:' tests/out/$$name.out > $$exp; \
		cat $$got; \
		if diff -q $$got $$exp > /dev/null 2>&1; then \
			echo "[OK]   $$name"; \
			pass=$$((pass + 1)); \
		else \
			echo "[FAIL] $$name"; \
			diff $$got $$exp; \
			fail=$$((fail + 1)); \
		fi; \
		echo ""; \
	done; \
	rm -f $$got $$exp; \
	echo "---"; \
	echo "$$pass passou(aram), $$fail falhou(aram)"
endef

test_seq: fire_seq
	$(call run_tests,fire_seq)

test_omp: fire_omp
	$(call run_tests,fire_omp)

test: fire_seq fire_omp
	@echo "=== SEQ ===" && $(MAKE) -s test_seq && echo "" && echo "=== OMP ===" && $(MAKE) -s test_omp

clean:
	rm -f fire_seq fire_omp
