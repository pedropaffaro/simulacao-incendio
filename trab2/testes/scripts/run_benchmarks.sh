#!/usr/bin/env bash
#
# testes/scripts/run_benchmarks.sh: bateria de benchmarks do trabalho 2 (fire_seq / fire_mpi_omp)
#
# Uso:
#   ./testes/scripts/run_benchmarks.sh
#
#   A configuracao (hostfile, flags do mpirun, lista de NPrc, lista de T e repeticoes)
#   fica em testes/scripts/config.sh e pode ser sobrescrita pelo ambiente:
#
#   HOSTFILE=testes/hosts/hosts.txt NP_LIST="1 2 4 8" T_LIST="1 2 4 8" REPS=10 \
#       ./testes/scripts/run_benchmarks.sh
#
# Pre-requisitos:
#   - 'make' ja executado na raiz de trab2/: precisa existir entrega/fire_mpi_omp e
#     testes/bin/fire_seq (o sequencial do trabalho 1, usado como base do speedup).
#   - no cluster, o projeto precisa estar em um diretorio visivel por todos os nos.
#
# O que e executado, para cada carga de testes/entradas/cargas/:
#   - fire_seq, REPS vezes                       (base do speedup absoluto)
#   - fire_mpi_omp para toda combinacao NPrc x T, REPS vezes cada
#
# Saida:
#   testes/resultados/runs.csv      uma linha por execucao, com os 12 campos de saida
#                                   do programa mais metadados (versao, carga, NPrc, T,
#                                   repeticao)
#   testes/resultados/ambiente.txt  descricao da maquina e das ferramentas, pra tabela
#                                   de ambiente experimental do relatorio
#   O processamento (mediana, speedup, tabelas e figuras .tex) fica por conta de
#   testes/scripts/process_results.py.

set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/config.sh"
cd "$ROOT_DIR"

checar_binarios "$BIN_PAR" "$BIN_SEQ"
mkdir -p "$RESULTADOS_DIR" "$TMP_DIR"

CSV="$RESULTADOS_DIR/runs.csv"
AMBIENTE="$RESULTADOS_DIR/ambiente.txt"

# Uma bateria nova nunca se mistura com a anterior: o CSV antigo e guardado com a data no nome
if [[ -f "$CSV" ]]; then
  mv "$CSV" "$RESULTADOS_DIR/runs_$(date -r "$CSV" +%Y%m%d_%H%M%S).csv"
fi
echo "versao,carga,entrada,np,t,rep,passos,nao_combustiveis,intactas,em_chamas,queimadas,contencao,total_ignicoes,pico_passo,pico_qtd,percentual_queimado,percentual_protegido,checksum,tempo" > "$CSV"

# coletar_ambiente: grava em ambiente.txt, no formato chave=valor, o que entra na tabela de ambiente
coletar_ambiente() {
  campo_lscpu() {
    LC_ALL=C lscpu 2> /dev/null | grep -m1 "^$1:" | cut -d: -f2- | sed 's/^ *//'
  }

  local sockets nucleos_por_socket
  sockets="$(campo_lscpu 'Socket(s)')"
  nucleos_por_socket="$(campo_lscpu 'Core(s) per socket')"

  local num_nos=1
  if [[ -n "$HOSTFILE" ]]; then
    num_nos="$(grep -Ev '^\s*(#|$)' "$HOSTFILE" | wc -l)"
  fi

  {
    echo "data=$(date '+%d/%m/%Y %H:%M')"
    echo "cpu_modelo=$(campo_lscpu 'Model name')"
    echo "nucleos_fisicos=$((${sockets:-1} * ${nucleos_por_socket:-1}))"
    echo "cpus_logicas=$(campo_lscpu 'CPU(s)')"
    echo "threads_por_nucleo=$(campo_lscpu 'Thread(s) per core')"
    echo "cache_l1d=$(campo_lscpu 'L1d cache')"
    echo "cache_l2=$(campo_lscpu 'L2 cache')"
    echo "cache_l3=$(campo_lscpu 'L3 cache')"
    echo "nos_numa=$(campo_lscpu 'NUMA node(s)')"
    echo "memoria=$(LC_ALL=C free -h 2> /dev/null | awk '/^Mem:/ { print $2 }')"
    echo "sistema=$(. /etc/os-release 2> /dev/null && echo "$PRETTY_NAME")"
    echo "compilador=$(mpicc --version 2> /dev/null | head -n 1)"
    echo "mpi=$("$MPIRUN" --version 2> /dev/null | head -n 1)"
    echo "mpi_padrao=$(printf '#include <mpi.h>\nMPI_VERSION.MPI_SUBVERSION\n' | mpicc -E -P - 2> /dev/null | tail -n 1 | tr -d ' ')"
    echo "openmp=$(echo | mpicc -fopenmp -dM -E - 2> /dev/null | awk '/_OPENMP/ { print $3 }')"
    echo "flags=$(grep -m1 '^CFLAGS' entrega/Makefile | cut -d= -f2- | cut -d'#' -f1 | sed 's/^ *//; s/ *$//')"
    echo "num_nos=$num_nos"
    echo "hostfile=${HOSTFILE:-(sem hostfile, execucao local)}"
    echo "mpirun_flags=$MPIRUN_FLAGS"
    echo "np_list=$NP_LIST"
    echo "t_list=$T_LIST"
    echo "reps=$REPS"
  } > "$AMBIENTE"
}

# parse_and_append: converte a saida de 12 linhas do programa em uma linha CSV
parse_and_append() {
  local versao="$1" carga="$2" entrada="$3" np="$4" t="$5" rep="$6" out="$7"

  local passos nao_comb intactas em_chamas queimadas contencao total_ign
  local pico_passo pico_qtd pct_q pct_p checksum tempo

  passos=$(grep     '^passos:'               <<< "$out" | cut -d' ' -f2)
  nao_comb=$(grep   '^nao_combustiveis:'     <<< "$out" | cut -d' ' -f2)
  intactas=$(grep   '^intactas:'             <<< "$out" | cut -d' ' -f2)
  em_chamas=$(grep  '^em_chamas:'            <<< "$out" | cut -d' ' -f2)
  queimadas=$(grep  '^queimadas:'            <<< "$out" | cut -d' ' -f2)
  contencao=$(grep  '^contencao:'            <<< "$out" | cut -d' ' -f2)
  total_ign=$(grep  '^total_ignicoes:'       <<< "$out" | cut -d' ' -f2)
  pico_passo=$(grep '^pico_ignicoes:'        <<< "$out" | awk '{print $2}')
  pico_qtd=$(grep   '^pico_ignicoes:'        <<< "$out" | awk '{print $3}')
  pct_q=$(grep      '^percentual_queimado:'  <<< "$out" | cut -d' ' -f2)
  pct_p=$(grep      '^percentual_protegido:' <<< "$out" | cut -d' ' -f2)
  checksum=$(grep   '^checksum:'             <<< "$out" | cut -d' ' -f2)
  tempo=$(grep      '^tempo:'                <<< "$out" | cut -d' ' -f2)

  if [[ -z "$checksum" || -z "$tempo" ]]; then
    echo "[erro] saida incompleta para versao=$versao entrada=$entrada NPrc=$np T=$t (rep $rep):" >&2
    echo "$out" >&2
    exit 1
  fi

  echo "$versao,$carga,$entrada,$np,$t,$rep,$passos,$nao_comb,$intactas,$em_chamas,$queimadas,$contencao,$total_ign,$pico_passo,$pico_qtd,$pct_q,$pct_p,$checksum,$tempo" >> "$CSV"
}

# run_seq: executa o sequencial REPS vezes sobre uma carga
# O T do arquivo nao tem efeito no sequencial; np e t ficam como 1 no CSV
run_seq() {
  local carga="$1" entrada="$2"
  echo ">> seq      carga=$carga reps=$REPS"
  local out rep
  for ((rep = 1; rep <= REPS; rep++)); do
    out=$("$BIN_SEQ" "$entrada")
    parse_and_append "seq" "$carga" "$entrada" 1 1 "$rep" "$out"
  done
}

# run_par: executa a versao MPI+OpenMP REPS vezes com NPrc processos e T threads por processo
run_par() {
  local carga="$1" entrada="$2" np="$3" t="$4"
  local copia
  copia="$(entrada_com_threads "$entrada" "$t")"
  echo ">> mpi_omp  carga=$carga NPrc=$np T=$t reps=$REPS"
  local out rep
  for ((rep = 1; rep <= REPS; rep++)); do
    out=$(executar_mpi "$np" "$BIN_PAR" "$copia")
    parse_and_append "mpi_omp" "$carga" "$entrada" "$np" "$t" "$rep" "$out"
  done
}

coletar_ambiente

for entrada_abs in "$ENTRADAS_DIR"/cargas/*.txt; do
  # Caminho relativo a raiz de trab2/, pra o CSV nao depender de onde o projeto esta
  entrada="${entrada_abs#"$ROOT_DIR"/}"
  carga="$(basename "$entrada" .txt)"
  carga="${carga#entrada_carga_}"

  run_seq "$carga" "$entrada"
  for np in $NP_LIST; do
    for t in $T_LIST; do
      run_par "$carga" "$entrada" "$np" "$t"
    done
  done
done

rm -rf "$TMP_DIR"

echo ""
echo "Benchmark concluido. Resultados em ${CSV#"$ROOT_DIR"/} e ${AMBIENTE#"$ROOT_DIR"/}"
echo "Proximo passo: python3 testes/scripts/process_results.py (ou 'make plots')"
