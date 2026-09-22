#!/usr/bin/env bash
#
# scripts/run_benchmarks.sh: bateria de benchmarks do trabalho 1 (fire_seq / fire_omp)
#
# Uso:
#   ./scripts/run_benchmarks.sh [REPS] [DET_REPS]
#
#   REPS      repeticoes por configuracao cronometrada (tempos, varredura de T,
#              comparacao de schedules). Padrao: 10.
#   DET_REPS  repeticoes do grupo de independencia de threads (Tabela 4), onde o
#              que importa e a igualdade de checksum/ignicoes/pico entre T's, nao
#              a estatistica de tempo. Padrao: 3.
#
# Pre-requisitos:
#   - `make experimentos` ja executado nesta pasta: precisa existir ./fire_seq,
#     ./fire_omp_static, ./fire_omp_static_1024, ./fire_omp_dynamic_1024 e
#     ./fire_omp_guided.
#   - pasta entradas/ com os arquivos de carga (ver Apendice C do relatorio):
#       entrada_carga_pequena.txt      (T=4)   entrada_carga_pequena_T1.txt
#       entrada_carga_media.txt        (T=8)   entrada_carga_media_T{1,2,4,16}.txt
#       entrada_carga_grande.txt       (T=8)   entrada_carga_grande_T{1,2,4,16}.txt
#       entrada_sem_ignicao.txt
#
# Saida:
#   results/raw/runs.csv: uma linha por execucao, com os 12 campos de saida do
#   programa mais metadados (grupo, carga, entrada, binario, schedule, T do
#   arquivo, repeticao). O arquivo e SOBRESCRITO a cada chamada deste script
#   (nao acumula com execucoes anteriores). O processamento (mediana, speedup,
#   tabelas .tex) fica por conta de scripts/process_results.py.

set -euo pipefail

REPS="${1:-10}"
DET_REPS="${2:-3}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

ENTRADAS_DIR="entradas"
RAW_DIR="results/raw"
PLOT_DIR="results/plot"
CSV="$RAW_DIR/runs.csv"

mkdir -p "$RAW_DIR" "$PLOT_DIR"

BIN_SEQ="./fire_seq"
declare -A SCHED_BIN=(
  [static]="./fire_omp_static"
  [static_1024]="./fire_omp_static_1024"
  [dynamic_1024]="./fire_omp_dynamic_1024"
  [guided]="./fire_omp_guided"
)

# Verificacao de pre-requisitos (falha cedo e com mensagem clara)
for req in "$BIN_SEQ" "${SCHED_BIN[@]}"; do
  if [[ ! -x "$req" ]]; then
    echo "[erro] binario nao encontrado ou nao executavel: $req" >&2
    echo "       rode 'make experimentos' antes de chamar este script." >&2
    exit 1
  fi
done

if [[ ! -d "$ENTRADAS_DIR" ]]; then
  echo "[erro] pasta '$ENTRADAS_DIR/' nao encontrada na raiz do projeto." >&2
  exit 1
fi

# Cabecalho do CSV: sempre reescrito do zero. Cada chamada deste script gera
# uma bateria completa e autocontida -- rodar de novo nao deve acumular linhas
# de execucoes/maquinas anteriores, que ficariam indistinguiveis e distorceriam
# a mediana calculada por process_results.py.
echo "grupo,carga,entrada,binario,schedule,T_arquivo,rep,passos,nao_combustiveis,intactas,em_chamas,queimadas,contencao,total_ignicoes,pico_passo,pico_qtd,percentual_queimado,percentual_protegido,checksum,tempo" > "$CSV"

# parse_and_append: converte a saida de 12 linhas do programa em uma linha CSV
parse_and_append() {
  local grupo="$1" carga="$2" entrada="$3" binario="$4" schedule="$5" t_arq="$6" rep="$7" out="$8"

  local passos nao_comb intactas em_chamas queimadas contencao total_ign
  local pico_passo pico_qtd pct_q pct_p checksum tempo

  passos=$(grep     '^passos:'              <<<"$out" | cut -d' ' -f2)
  nao_comb=$(grep   '^nao_combustiveis:'     <<<"$out" | cut -d' ' -f2)
  intactas=$(grep   '^intactas:'             <<<"$out" | cut -d' ' -f2)
  em_chamas=$(grep  '^em_chamas:'            <<<"$out" | cut -d' ' -f2)
  queimadas=$(grep  '^queimadas:'            <<<"$out" | cut -d' ' -f2)
  contencao=$(grep  '^contencao:'            <<<"$out" | cut -d' ' -f2)
  total_ign=$(grep  '^total_ignicoes:'       <<<"$out" | cut -d' ' -f2)
  pico_passo=$(grep '^pico_ignicoes:'        <<<"$out" | awk '{print $2}')
  pico_qtd=$(grep   '^pico_ignicoes:'        <<<"$out" | awk '{print $3}')
  pct_q=$(grep      '^percentual_queimado:'  <<<"$out" | cut -d' ' -f2)
  pct_p=$(grep      '^percentual_protegido:' <<<"$out" | cut -d' ' -f2)
  checksum=$(grep   '^checksum:'             <<<"$out" | cut -d' ' -f2)
  tempo=$(grep      '^tempo:'                <<<"$out" | cut -d' ' -f2)

  if [[ -z "$checksum" || -z "$tempo" ]]; then
    echo "[erro] saida incompleta para binario=$binario entrada=$entrada (rep $rep):" >&2
    echo "$out" >&2
    exit 1
  fi

  echo "$grupo,$carga,$entrada,$binario,$schedule,$t_arq,$rep,$passos,$nao_comb,$intactas,$em_chamas,$queimadas,$contencao,$total_ign,$pico_passo,$pico_qtd,$pct_q,$pct_p,$checksum,$tempo" >> "$CSV"
}

# run_case: executa um binario sobre uma entrada `reps` vezes e grava cada linha
run_case() {
  local grupo="$1" carga="$2" entrada="$3" binario="$4" schedule="$5" t_arq="$6" reps="$7"

  if [[ ! -f "$entrada" ]]; then
    echo "[erro] entrada nao encontrada: $entrada" >&2
    exit 1
  fi

  echo ">> grupo=$grupo carga=$carga binario=$(basename "$binario") schedule=$schedule T=$t_arq reps=$reps"
  local out
  for ((rep = 1; rep <= reps; rep++)); do
    out=$("$binario" "$entrada")
    parse_and_append "$grupo" "$carga" "$entrada" "$(basename "$binario")" "$schedule" "$t_arq" "$rep" "$out"
  done
}

# run_case_pinned: igual a run_case, mas fixa a execucao em um unico nucleo
# logico com `taskset -c 0`. Usado so no isolamento monothread (Secao 7.4,
# Tabelas 6/10): sem maquina dedicada de 1 nucleo, essa e a aproximacao na
# propria maquina multicore.
run_case_pinned() {
  local grupo="$1" carga="$2" entrada="$3" binario="$4" schedule="$5" t_arq="$6" reps="$7"

  if [[ ! -f "$entrada" ]]; then
    echo "[erro] entrada nao encontrada: $entrada" >&2
    exit 1
  fi
  if ! command -v taskset >/dev/null 2>&1; then
    echo "[aviso] 'taskset' nao encontrado; pulando grupo '$grupo'." >&2
    return 0
  fi

  echo ">> grupo=$grupo carga=$carga binario=$(basename "$binario") schedule=$schedule T=$t_arq reps=$reps (taskset -c 0)"
  local out
  for ((rep = 1; rep <= reps; rep++)); do
    out=$(taskset -c 0 "$binario" "$entrada")
    parse_and_append "$grupo" "$carga" "$entrada" "$(basename "$binario")" "$schedule" "$t_arq" "$rep" "$out"
  done
}

# 1) Tabelas 1/2/3/7/8/9: tempos base (T fixo no arquivo: 4/8/8, schedule static)
run_case "tempos" "pequena" "$ENTRADAS_DIR/entrada_carga_pequena.txt" "$BIN_SEQ"            "seq"    4 "$REPS"
run_case "tempos" "pequena" "$ENTRADAS_DIR/entrada_carga_pequena.txt" "${SCHED_BIN[static]}" "static" 4 "$REPS"
run_case "tempos" "media"   "$ENTRADAS_DIR/entrada_carga_media.txt"   "$BIN_SEQ"            "seq"    8 "$REPS"
run_case "tempos" "media"   "$ENTRADAS_DIR/entrada_carga_media.txt"   "${SCHED_BIN[static]}" "static" 8 "$REPS"
run_case "tempos" "grande"  "$ENTRADAS_DIR/entrada_carga_grande.txt"  "$BIN_SEQ"            "seq"    8 "$REPS"
run_case "tempos" "grande"  "$ENTRADAS_DIR/entrada_carga_grande.txt"  "${SCHED_BIN[static]}" "static" 8 "$REPS"

# 2) Tabela 4 (5.5): independencia do numero de threads, carga media, T = 1,2,4,8,16
for t in 1 2 4 16; do
  run_case "threads_det" "media" "$ENTRADAS_DIR/entrada_carga_media_T${t}.txt" "${SCHED_BIN[static]}" "static" "$t" "$DET_REPS"
done
run_case "threads_det" "media" "$ENTRADAS_DIR/entrada_carga_media.txt" "${SCHED_BIN[static]}" "static" 8 "$DET_REPS"

# 3) Tabela 12 (7.5): varredura de T, carga grande, T = 1,2,4,8,16
for t in 1 2 4 16; do
  run_case "varredura_T" "grande" "$ENTRADAS_DIR/entrada_carga_grande_T${t}.txt" "${SCHED_BIN[static]}" "static" "$t" "$REPS"
done
run_case "varredura_T" "grande" "$ENTRADAS_DIR/entrada_carga_grande.txt" "${SCHED_BIN[static]}" "static" 8 "$REPS"

# 4) Tabela 13 (4.5/7.6): comparacao de schedules, carga grande, T = 2,4,8,16
for schedule in static static_1024 dynamic_1024 guided; do
  for t in 2 4 16; do
    run_case "schedules" "grande" "$ENTRADAS_DIR/entrada_carga_grande_T${t}.txt" "${SCHED_BIN[$schedule]}" "$schedule" "$t" "$REPS"
  done
  run_case "schedules" "grande" "$ENTRADAS_DIR/entrada_carga_grande.txt" "${SCHED_BIN[$schedule]}" "$schedule" 8 "$REPS"
done

# 5) Secao 5.6: caso sem nenhuma ignicao (uma execucao de cada versao basta;
#    confirma pico_ignicoes: -1 0 e a igualdade sequencial x paralela)
run_case "sem_ignicao" "sem_ignicao" "$ENTRADAS_DIR/entrada_sem_ignicao.txt" "$BIN_SEQ"            "seq"    4 1
run_case "sem_ignicao" "sem_ignicao" "$ENTRADAS_DIR/entrada_sem_ignicao.txt" "${SCHED_BIN[static]}" "static" 4 1

# 6) Tabelas 6/10 (7.4): isolamento monothread, mesma maquina, 1 nucleo fixado
run_case_pinned "monothread" "pequena" "$ENTRADAS_DIR/entrada_carga_pequena.txt"    "$BIN_SEQ"            "seq"    4 3
run_case_pinned "monothread" "pequena" "$ENTRADAS_DIR/entrada_carga_pequena_T1.txt" "${SCHED_BIN[static]}" "static" 1 3
run_case_pinned "monothread" "media"   "$ENTRADAS_DIR/entrada_carga_media.txt"      "$BIN_SEQ"            "seq"    8 3
run_case_pinned "monothread" "media"   "$ENTRADAS_DIR/entrada_carga_media_T1.txt"   "${SCHED_BIN[static]}" "static" 1 3

echo ""
echo "Benchmark concluido. Resultados em $CSV"