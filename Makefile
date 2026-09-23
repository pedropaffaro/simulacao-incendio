CC = gcc
CFLAGS = -Wall -Wextra -fopenmp -O2 -std=c99 # Flags de otimização (-O2), avisos (-Wall, -Wextra) e OpenMP. NÃO PRECISA DE -march=native
INC = -Iinclude
LDFLAGS = -lm

# Lmpa binários antigos e compila as versões sequencial e paralela
all: clean fire_seq fire_omp

# Compila a versão sequencial a partir de src/fire_seq.c
fire_seq: fire_seq.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@ $(LDFLAGS)

# Compila a versão paralela com OpenMP a partir de src/fire_omp.c
fire_omp: fire_omp.c
	$(CC) $(CFLAGS) $(INC) $^ -o $@ $(LDFLAGS)

# Remove todos os arquivos executáveis gerados
clean:
	rm -f fire_seq fire_omp
