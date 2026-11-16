CC = mpicc
CFLAGS = -Wall -Wextra -O2 -fopenmp -std=c99 # Flags de otimização (-O2), avisos (-Wall, -Wextra) e OpenMP.
LDFLAGS = -lm

# Limpa o binário antigo e compila a versão paralela
all: clean fire_mpi_omp

# Compila a versão paralela com MPI e OpenMP a partir de fire_mpi_omp.c
fire_mpi_omp: fire_mpi_omp.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Remove o arquivo executável gerado
clean:
	rm -f fire_mpi_omp

# Declara alvos virtuais para evitar conflito com arquivos de mesmo nome no sistema
.PHONY: all clean
