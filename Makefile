all:
	clang -Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include -L/opt/homebrew/opt/libomp/lib -lomp fire_omp.c -o fire_omp

run: 
	./fire_omp