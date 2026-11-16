#!/usr/bin/env bash
#
# testes/scripts/validar.sh: validacao de corretude do trabalho 2 (fire_mpi_omp)
#
# Uso:
#   ./testes/scripts/validar.sh            valida tudo
#   ./testes/scripts/validar.sh --rapido   pula as cargas (so casos de borda e entradas invalidas)
#
# O que e conferido:
#   1) Entradas validas (testes/entradas/cargas e testes/entradas/bordas): para cada
#      combinacao NPrc x T de config.sh, as 11 primeiras linhas da saida tem que ser
#      identicas ao gabarito de testes/esperado/ (gerado pelo fire_seq do trabalho 1),
#      e a 12a linha tem que ser "tempo: <valor com 6 casas>".
#   2) Entradas invalidas (testes/entradas/invalidas): o programa nao pode imprimir
#      nenhum resultado, tem que escrever uma mensagem "[Erro]" e sair com codigo
#      diferente de zero.
#   3) Argumentos da linha de comando: sem arquivo, com arquivo a mais e com arquivo
#      inexistente.
#
# Pre-requisito: 'make' ja executado na raiz de trab2/ (precisa de entrega/fire_mpi_omp).
# Codigo de saida: 0 se tudo passou, 1 se alguma verificacao falhou.

set -uo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/config.sh"
cd "$ROOT_DIR"

RAPIDO=0
if [[ "${1:-}" == "--rapido" ]]; then
  RAPIDO=1
fi

checar_binarios "$BIN_PAR"
mkdir -p "$TMP_DIR"

passou=0
falhou=0
saida="$TMP_DIR/validar_stdout.txt"
erros="$TMP_DIR/validar_stderr.txt"

registrar() {
  local ok="$1" descricao="$2"
  if [[ "$ok" -eq 1 ]]; then
    passou=$((passou + 1))
  else
    falhou=$((falhou + 1))
    echo "[FAIL] $descricao"
  fi
}

# 1) Entradas validas contra o gabarito do sequencial
validar_entrada() {
  local entrada="$1"
  local nome gabarito
  nome="$(basename "$entrada" .txt)"
  gabarito="$ESPERADO_DIR/$nome.out"

  if [[ ! -f "$gabarito" ]]; then
    echo "[FAIL] $nome: gabarito $gabarito nao existe (rode 'make esperado')"
    falhou=$((falhou + 1))
    return
  fi

  local falhas_antes="$falhou"
  local np t copia
  for np in $NP_LIST; do
    for t in $T_LIST; do
      copia="$(entrada_com_threads "$entrada" "$t")"
      executar_mpi "$np" "$BIN_PAR" "$copia" > "$saida" 2> "$erros"
      local codigo=$?

      local ok=1
      [[ "$codigo" -eq 0 ]] || ok=0
      [[ "$(wc -l < "$saida")" -eq 12 ]] || ok=0
      diff -q <(grep -v '^tempo:' "$saida") "$gabarito" > /dev/null 2>&1 || ok=0
      sed -n '12p' "$saida" | grep -Eq '^tempo: [0-9]+\.[0-9]{6}$' || ok=0

      registrar "$ok" "$nome com NPrc=$np T=$t (codigo de saida $codigo)"
      if [[ "$ok" -eq 0 ]]; then
        diff <(grep -v '^tempo:' "$saida") "$gabarito" | head -n 6
        head -n 3 "$erros"
      fi
    done
  done

  if [[ "$falhou" -eq "$falhas_antes" ]]; then
    echo "[OK]   $nome"
  fi
}

echo "== 1) Saida identica a do sequencial (NPrc em {$NP_LIST}, T em {$T_LIST}) =="
if [[ "$RAPIDO" -eq 0 ]]; then
  for entrada in "$ENTRADAS_DIR"/cargas/*.txt; do
    validar_entrada "$entrada"
  done
fi
for entrada in "$ENTRADAS_DIR"/bordas/*.txt; do
  validar_entrada "$entrada"
done

# 2) Entradas invalidas
validar_invalida() {
  local descricao="$1" np="$2"
  shift 2
  executar_mpi "$np" "$BIN_PAR" "$@" > "$saida" 2> "$erros"
  local codigo=$?

  local ok=1
  [[ "$codigo" -ne 0 ]] || ok=0
  [[ ! -s "$saida" ]] || ok=0
  grep -q '^\[Erro\]' "$erros" || ok=0

  registrar "$ok" "$descricao com NPrc=$np (codigo de saida $codigo, $(wc -l < "$saida") linhas na saida)"
  return $((1 - ok))
}

echo ""
echo "== 2) Entradas invalidas sao rejeitadas =="
np_menor="${NP_LIST%% *}"
np_maior="${NP_LIST##* }"
for entrada in "$ENTRADAS_DIR"/invalidas/*.txt; do
  nome="$(basename "$entrada" .txt)"
  falhas_antes="$falhou"
  validar_invalida "$nome" "$np_menor" "$entrada"
  validar_invalida "$nome" "$np_maior" "$entrada"
  if [[ "$falhou" -eq "$falhas_antes" ]]; then
    echo "[OK]   $nome: $(grep -m1 '^\[Erro\]' "$erros")"
  fi
done

# 3) Argumentos da linha de comando
echo ""
echo "== 3) Argumentos da linha de comando =="
valida="$ENTRADAS_DIR/bordas/tres_linhas.txt"
validar_invalida "sem arquivo de entrada" "$np_maior" && echo "[OK]   sem arquivo de entrada"
validar_invalida "dois arquivos de entrada" "$np_maior" "$valida" "$valida" && echo "[OK]   dois arquivos de entrada"
validar_invalida "arquivo inexistente" "$np_maior" "$ENTRADAS_DIR/nao_existe.txt" && echo "[OK]   arquivo inexistente"

rm -rf "$TMP_DIR"

echo ""
echo "---"
echo "$passou verificacao(oes) passou(aram), $falhou falhou(aram)"
[[ "$falhou" -eq 0 ]]
