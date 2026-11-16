# Makefile de desenvolvimento do trabalho 2 (compila, testa, mede e empacota)
# O Makefile que vai na entrega é o de entrega/, que só compila o fire_mpi_omp

CC = gcc
CFLAGS = -Wall -Wextra -O2 -fopenmp -std=c99 # Mesmas flags do trabalho 1, usadas no sequencial de referência
LDFLAGS = -lm

ZIP = trab2_entrega.zip

# Compila a versão paralela (entrega/) e o sequencial de referência (testes/bin/)
all: entrega seq

# Compila a versão paralela usando o próprio Makefile da entrega, pra testar exatamente o que será entregue
entrega:
	$(MAKE) -C entrega

# Compila o sequencial do trabalho 1, usado como gabarito na validação e como base do speedup
seq: testes/bin/fire_seq

testes/bin/fire_seq: testes/referencia/fire_seq.c
	mkdir -p testes/bin
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Regenera os gabaritos de testes/esperado/ com o sequencial (saída sem a linha do tempo)
esperado: seq
	@mkdir -p testes/esperado
	@for entrada in testes/entradas/cargas/*.txt testes/entradas/bordas/*.txt; do \
	  nome=$$(basename $$entrada .txt); \
	  ./testes/bin/fire_seq $$entrada | grep -v '^tempo:' > testes/esperado/$$nome.out; \
	  echo "[ok] testes/esperado/$$nome.out"; \
	done

# Valida a versão paralela contra os gabaritos, com várias combinações de NPrc e T
test: entrega
	./testes/scripts/validar.sh

# Mesma validação, sem as três cargas (só casos de borda e entradas inválidas)
test-rapido: entrega
	./testes/scripts/validar.sh --rapido

# Roda a bateria de benchmarks e grava testes/resultados/runs.csv
bench: all
	./testes/scripts/run_benchmarks.sh

# Gera as tabelas e figuras do relatório (relatorio/plot/) a partir de testes/resultados/
plots:
	python3 testes/scripts/process_results.py

# Benchmarks e tabelas/figuras de uma vez só
resultados: bench plots

# Compila o relatório (duas passadas, por causa das referências) e copia o PDF pra entrega/
relatorio:
	cd relatorio && pdflatex -interaction=nonstopmode -halt-on-error main.tex > /dev/null && \
	  pdflatex -interaction=nonstopmode -halt-on-error main.tex > /dev/null
	cp relatorio/main.pdf entrega/relatorio.pdf
	@echo "[ok] entrega/relatorio.pdf"

# Monta o .zip da entrega, só com os três arquivos pedidos no enunciado
zip:
	@test -f entrega/relatorio.pdf || { echo "[erro] falta entrega/relatorio.pdf (rode 'make relatorio' ou copie o PDF do Overleaf)"; exit 1; }
	rm -f $(ZIP)
	cd entrega && zip ../$(ZIP) fire_mpi_omp.c Makefile relatorio.pdf
	@echo "[ok] $(ZIP)"

# Remove binários, temporários e auxiliares do LaTeX (não apaga resultados nem relatorio/plot/)
clean:
	$(MAKE) -C entrega clean
	rm -rf testes/bin testes/.tmp $(ZIP)
	rm -f relatorio/main.aux relatorio/main.log relatorio/main.out relatorio/main.toc

# Declara alvos virtuais para evitar conflito com arquivos e pastas de mesmo nome
.PHONY: all entrega seq esperado test test-rapido bench plots resultados relatorio zip clean
