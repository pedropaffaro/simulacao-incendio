#!/usr/bin/env bash
#
# testes/scripts/config.sh: configuracao comum de validar.sh e run_benchmarks.sh
#
# Este arquivo nao e executado sozinho, ele e carregado (source) pelos outros scripts.
# Toda variavel pode ser trocada aqui ou sobrescrita pelo ambiente na hora de rodar:
#
#   NP_LIST="1 2" T_LIST="1 4" ./testes/scripts/validar.sh
#   HOSTFILE=testes/hosts/hosts.txt REPS=10 ./testes/scripts/run_benchmarks.sh

# Arquivo de hosts do cluster. Vazio = roda so na maquina local, sem --hostfile.
HOSTFILE="${HOSTFILE-}"

# Comando do mpirun e flags extras.
#   --map-by node   espalha os processos entre os nos do hostfile (um por no, em rodizio),
#                   em vez de encher o primeiro no antes de usar o segundo
#   --bind-to none  nao prende o processo a um unico nucleo. Sem isso o Open MPI prende cada
#                   processo a 1 nucleo quando NPrc <= 2, e as T threads disputam esse nucleo
# Se NPrc passar do numero de slots disponiveis, acrescente --oversubscribe.
MPIRUN="${MPIRUN-mpirun}"
MPIRUN_FLAGS="${MPIRUN_FLAGS---map-by node --bind-to none}"

# Numeros de processos (NPrc) e de threads por processo (T) testados.
# Toda combinacao NPrc x T e executada.
NP_LIST="${NP_LIST-1 2 4}"
T_LIST="${T_LIST-1 2 4}"

# Repeticoes por configuracao cronometrada (so run_benchmarks.sh). O relatorio usa a mediana.
REPS="${REPS-5}"

# Caminhos do projeto, sempre relativos a raiz de trab2/
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN_PAR="$ROOT_DIR/entrega/fire_mpi_omp"
BIN_SEQ="$ROOT_DIR/testes/bin/fire_seq"
ENTRADAS_DIR="$ROOT_DIR/testes/entradas"
ESPERADO_DIR="$ROOT_DIR/testes/esperado"
RESULTADOS_DIR="$ROOT_DIR/testes/resultados"

# Pasta temporaria dentro do projeto (e nao em /tmp) pra que as entradas geradas tambem
# existam nos outros nos quando o projeto esta em um diretorio compartilhado do cluster
TMP_DIR="$ROOT_DIR/testes/.tmp"

# executar_mpi NP COMANDO...: chama o mpirun no formato do enunciado, com as flags acima
executar_mpi() {
  local np="$1"
  shift
  local args=()
  if [[ -n "$HOSTFILE" ]]; then
    args+=(--hostfile "$HOSTFILE")
  fi
  # MPIRUN_FLAGS fica sem aspas de proposito, pra ser quebrada em varias flags
  # shellcheck disable=SC2086
  "$MPIRUN" "${args[@]}" $MPIRUN_FLAGS -np "$np" "$@"
}

# entrada_com_threads ENTRADA T: copia a entrada pra pasta temporaria trocando o campo T
# (4o valor da primeira linha) e escreve o caminho da copia na saida padrao
entrada_com_threads() {
  local entrada="$1" threads="$2"
  mkdir -p "$TMP_DIR"
  local destino
  destino="$TMP_DIR/$(basename "$entrada" .txt)_T${threads}.txt"
  awk -v t="$threads" 'NR == 1 { $4 = t } { print }' "$entrada" > "$destino"
  echo "$destino"
}

# checar_binarios BIN...: falha cedo e com mensagem clara se algum binario nao foi compilado
checar_binarios() {
  local bin
  for bin in "$@"; do
    if [[ ! -x "$bin" ]]; then
      echo "[erro] binario nao encontrado ou nao executavel: $bin" >&2
      echo "       rode 'make' na raiz de trab2/ antes de chamar este script." >&2
      exit 1
    fi
  done
  if [[ -n "$HOSTFILE" && ! -f "$HOSTFILE" ]]; then
    echo "[erro] hostfile nao encontrado: $HOSTFILE" >&2
    exit 1
  fi
}
