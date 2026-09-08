# all:
# 	clang -Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include -L/opt/homebrew/opt/libomp/lib -lomp src/fire_omp.c -o fire_omp

# run: 
# 	./fire_omp
CC = clang
CFLAGS = -Wall -Xpreprocessor -fopenmp -Iinclude -I/opt/homebrew/opt/libomp/include
LDFLAGS = -L/opt/homebrew/opt/libomp/lib -lomp -lm

# Compila todos os arquivos .c da pasta src/ (ex: fire_omp.c, funcs.c)
SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)
TARGET = fire_omp

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) entrada_carga_pequena.txt

clean:
	rm -f src/*.o $(TARGET)

.PHONY: all run clean