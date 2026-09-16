CC = gcc
CFLAGS = -Wall -Wextra -O2 -fopenmp
INC = -Iinclude
LDFLAGS = -lm

all: clean fire_seq fire_omp

fire_seq: src/fire_seq.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@ $(LDFLAGS)

fire_omp: src/fire_omp.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@ $(LDFLAGS)

experimentos: fire_seq
	$(CC) $(CFLAGS) -DSCHED="static" $(INC) src/fire_omp.c -o fire_omp_static $(LDFLAGS)
	$(CC) $(CFLAGS) -DSCHED="dynamic,16" $(INC) src/fire_omp.c -o fire_omp_dyn_16 $(LDFLAGS)
	$(CC) $(CFLAGS) -DSCHED="dynamic,128" $(INC) src/fire_omp.c -o fire_omp_dyn_128 $(LDFLAGS)
	$(CC) $(CFLAGS) -DSCHED="dynamic,1024" $(INC) src/fire_omp.c -o fire_omp_dyn_1024 $(LDFLAGS)

define run_tests
  @pass=0; fail=0; got=$$(mktemp); exp=$$(mktemp); out=$$(mktemp); \
  for in_file in tests/in/*.in; do \
    name=$$(basename $$in_file .in); \
    ./$(1) $$in_file > $$out; \
    cat $$out; \
    grep -v '^tempo:' $$out > $$got; \
    grep -v '^tempo:' tests/out/$$name.out > $$exp; \
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
  rm -f $$got $$exp $$out; \
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
	rm -f fire_seq fire_omp fire_omp_static fire_omp_dyn_*

.PHONY: all test test_seq test_omp run clean