# Checklist TB1 — Simulação de Incêndio 

**Legenda:** `[x]` atendido · `[~]` parcial / com ressalva · `[ ]` não atendido

---

## Estrutura do projeto
- [x] `fire_seq.c`
- [x] `fire_omp.c`
- [x] `Makefile` 
- [ ] `relatorio.pdf`

---

## Leitura e validação da entrada (seção 5)
- [x] Exatamente 1 argumento (nome do arquivo) — `validar_argc`
- [x] Arquivo abre com sucesso — `abrir_arquivo`
- [x] L > 0, C > 0, P ≥ 0, T > 0, LIMIAR > 0 — `ler_config_geral`
- [x] Componentes do vento ∈ {-1, 0, 1} e não ambos zero — `ler_config_vento`
- [x] Intensidade ∈ [0, 5] — `ler_config_vento`
- [x] F ≥ 0, Z ≥ 0 — `ler_contagem_focos_zonas`
- [x] Focos dentro da matriz — `ler_focos`
- [x] Sem focos repetidos — detectado indiretamente (`estado_atual[idx] == ESTADO_EM_CHAMAS`); funciona porque nenhuma célula inicia em chamas
- [x] Focos sobre células combustíveis (vegetação ou floresta) — `ler_focos`
- [x] Zonas completamente internas à matriz — `ler_zonas_contencao`
- [x] Limites iniciais ≤ limites finais de cada zona — `ler_zonas_contencao`
- [x] 0 ≤ passo_ativacao < P para cada zona — `ler_zonas_contencao`

---

## Preparação da simulação (seção 6)
- [x] Matriz armazenada linearmente: `indice = linha * C + coluna`
- [x] Geração da cobertura com `rand_r(&seed) % 100` em ordem crescente (seção 6.2) — `gerar_terreno`, laço único sequencial nas duas versões
- [x] Geração da umidade com `rand_r(&seed) % 101` imediatamente após cada cobertura (seção 6.3)
- [x] Estados iniciais: água/solo → não combustível; vegetação/floresta → intacta (seção 6.4)
- [x] Focos iniciais → estado em chamas com tempo correto (veg=2, floresta=4) (seção 6.5)
- [x] Construção do vetor `ativacao[]` com menor passo por célula, -1 se sem zona (seção 6.6)
- [x] Contagem de `combustiveis_iniciais` (vegetação + floresta) antes da simulação
  - `fire_seq.c`: correto e **fora** do trecho cronometrado (linhas 417–437).
  - `fire_omp.c`: correto e **fora** do trecho cronometrado (linhas 362–389, antes de
    `t_inicio` na linha 403). Essa mesma varredura inicial preenche o histograma de estados,
    o que cobre o caso de 0 passos (ver seção "Loop da simulação", item 3).

---

## Loop da simulação (seção 7)
- [x] Usar duas matrizes (`estado_atual` / `proximo_estado`) e dois vetores de tempo
- Por passo, na ordem correta:
  - [x] 1. Ativar zonas com `ativacao[i] == p` (Quadro 7.2.1)
  - [x] 2. Calcular próximo estado de todas as células (seção 7.3)
  - [x] 3. Calcular estatísticas do próximo estado
    - `fire_seq.c`: todas as 5 contagens são acumuladas no mesmo percurso, por passo.
    - `fire_omp.c`: as mesmas 5 contagens são feitas no laço paralelo principal, junto de
      cada transição de estado (cláusula `reduction` na linha 440), e copiadas para as
      estatísticas finais dentro do `omp single` (linhas 521–533). Como o `reduction` de um
      `omp for` combina com o valor anterior do item original, os acumuladores `proximo_*`
      são zerados a cada passo depois da cópia.
  - [x] 4. Trocar matrizes
  - [x] 5. Verificar condição de parada
- [x] Parar se sem células em chamas ou após P passos (seção 9)
- [x] Não executar nenhum passo se já sem chamas na inicialização — garantido pela condição do `while`

---

## Cálculo do potencial de ignição (seção 8)
- [x] Considerar os 8 vizinhos de Moore
- [x] Ignorar vizinhos fora da matriz — `fire_omp.c` usa caminho rápido sem teste de borda para o interior e caminho genérico com teste nas bordas (linhas 452–470)
- [x] Apenas vizinhos em chamas contribuem
- [x] `prop_linha = linha_celula - linha_vizinho` / `prop_coluna = coluna_celula - coluna_vizinho`
  - Em `fire_omp.c` isso é pré-computado como `peso_vizinho(-dv, -dc, ...)` (linha 399), que é
    algebricamente idêntico, pois `linha_vizinho = linha_celula + dv`.
- [x] Peso básico: 10 (ortogonal) ou 7 (diagonal)
- [x] Alinhamento: `A = prop_linha * vento_linha + prop_coluna * vento_coluna`
- [x] `Pv = max(1, P_basico + intensidade * A)`
- [x] `S = Σ Pv` de todos os vizinhos em chamas
- [x] `I = (S * fator_combustivel * (100 - umidade)) / 100` (aritmética inteira)
- [x] Ignição se `I >= LIMIAR`
  - `fire_omp.c` usa `if (S > 0 && potencial... >= LIMIAR)`. Equivalente, pois `S == 0 ⇒ I == 0`
    e `LIMIAR > 0` é garantido pela validação.

> Sem risco de overflow: `S ≤ 8 × 20 = 160`, logo `I ≤ 160 × 12 × 100 = 192 000`.

---

## Cálculo dos resultados (seção 10)
- [x] `total_ignicoes`: células intacta → em chamas durante a simulação (focos iniciais não contam)
- [x] `pico_ignicoes`: maior quantidade de novas ignições por passo; empate → primeiro passo
  (comparação estrita `>` nas duas versões)
- [x] `pico_ignicoes: -1 0` se nenhuma ignição ocorreu
  - `fire_seq.c` linha 442: `PICO pico = {-1, 0};` → imprime `-1 0`.
  - `fire_omp.c` linha 409: `int pico_passo = -1;` → imprime `-1 0`. Estava `0` até esta
    correção, o que imprimia `0 0` e era a única divergência de *resultado* entre as versões.
  - A comparação é estrita nas duas versões (`novos_incendios > pico.quantidade` no
    sequencial, `ignicoes_no_passo > pico_qtd` na paralela), então empate mantém o primeiro
    passo também quando existe ignição.
  - Verificado nos casos sem nenhuma ignição (LIMIAR alto), com P = 0 e com F = 0, cada um
    com T = 1, 4 e 8.
- [x] `percentual_queimado = 100 * (queimadas + em_chamas) / combustiveis_iniciais`
- [x] `percentual_protegido = 100 * contencao / combustiveis_iniciais`
- [x] Ambos os percentuais = 0 se `combustiveis_iniciais == 0`
- [x] Checksum calculado **fora** do trecho cronometrado e sequencialmente (seção 10)

---

## Saída (seção 11)
- [x] Formato exato dos 12 campos — conferido linha a linha contra o enunciado
- [x] `percentual_queimado` e `percentual_protegido` com 2 casas decimais (`%.2f`)
- [x] `tempo` com 6 casas decimais (`%.6f`)
- [x] `checksum` como inteiro sem sinal (`%llu`)

---

## Medição do tempo (seção 12)
- [x] Usar `omp_get_wtime()`
- [x] Cronometrado: ativação de zonas, propagação, atualização de estados e tempos, estatísticas, troca de matrizes, condição de parada
- [x] Não cronometrado: leitura, validação, alocação, geração de cobertura/umidade, mapa de ativação, focos, checksum, percentuais, impressão

> As duas versões medem o mesmo trabalho por passo. `t_inicio` (`fire_seq.c` linha 439,
> `fire_omp.c` linha 403) fica **depois** da contagem inicial de combustíveis/chamas e da
> montagem das tabelas `deslocamento_offset`/`pesos_direcao` (`fire_omp.c` linhas 362–400), e
> o laço medido inclui as 5 contagens de estatísticas nas duas versões. `t_fim` está em
> `fire_seq.c` linha 561 e `fire_omp.c` linha 547; checksum (linhas 564–568 e 553–557),
> percentuais e impressão ficam fora dos dois trechos.

---

## Versão sequencial — `fire_seq.c` (seção 13)
- [x] Sem diretivas OpenMP de paralelização — nenhum `#pragma omp` no arquivo
- [x] `omp_get_wtime()` permitido apenas para medir tempo (linhas 439 e 561)

---

## Versão paralela — `fire_omp.c` (seção 13)
- [x] Usa T threads — `num_threads(T)` nas duas regiões paralelas (linhas 371 e 421)
- [x] Região paralela persistente (não abrir/fechar a cada passo) — aberta na linha 421, `while` dos passos dentro dela
- [x] Ativação de zonas paralelizada — `omp for schedule(static)` (linhas 432–437)
- [x] Atualização da matriz paralelizada com `omp for` — `omp for collapse(2)` (linha 440)
- [x] `simd` aplicado onde pertinente — nos dois laços de varredura linear do arquivo:
  - contagem inicial: `#pragma omp parallel for simd` (linha 371);
  - ativação das zonas: `#pragma omp for simd schedule(static)` (linha 432), que é uma
    varredura linear com store condicional, sem dependência entre iterações.
  - O laço de propagação ficou **de fora**: o acesso aos vizinhos é indireto
    (`deslocamento_offset`/`pesos_direcao`) e há desvios por estado, então o vetorizador
    recusa (`missed: not vectorized: control flow in loop`).
  - Efeito conferido com `-fopt-info-vec-optimized`: com as flags do `Makefile` (`-O2`) nenhum
    laço vetoriza e o `simd` fica sem efeito prático; com `-O2 -march=native` o laço de ativação
    vetoriza (32 B); com `-O3` o da contagem inicial (16 B); com `-O3 -march=native` os dois
    (32 B). Em todos esses conjuntos de flags a saída segue idêntica à do sequencial.
- [x] Reduções para contadores (sem `critical`/`atomic` no laço principal) — `reduction(+:...)` nas linhas 371 e 440; nenhum `critical`/`atomic` no código
- [x] Troca de matrizes sem condição de corrida — dentro de `omp single` (linhas 511–543), com barreira implícita antes (fim do `omp for`) e depois (fim do `single`)
- [x] Condição de parada compartilhada corretamente — `passo_atual` e `celulas_em_chamas` atualizados no `single`; a barreira implícita do `single` implica *flush*, então todas as threads reavaliam o `while` com os mesmos valores
- [x] Resultado independente do número de threads — verificado com T = 1, 2, 4, 8: `total_ignicoes`, `pico` e `checksum` idênticos em todos os casos
- [ ] Pelo menos dois `schedule` comparados (ex: `static` vs `dynamic`) — o código tem **`schedule(static)` fixo** (linhas 432 e 440); não há parametrização nem registro de comparação
  - Sugestão: `#ifndef SCHED` `#define SCHED static` `#endif` + `schedule(SCHED)`, gerando dois
    binários com `-DSCHED='dynamic,1024'`, e reportar na tabela 8 do relatório.
- [x] `default(none)` nas regiões paralelas relevantes
  - [x] Região principal (linha 421): `default(none)` com cláusula `shared` explícita.
  - [x] `parallel for` da contagem inicial (linha 371): `default(none) shared(total_celulas, grade)`.
    A lista precisa de `grade` (acesso a `cobertura` e `estado_atual`) e de `total_celulas`; `T`
    só aparece em `num_threads(T)`, avaliado fora da região, e as variáveis da `reduction` são
    determinadas pela própria cláusula. Sem elas o gcc acusa `not specified in enclosing
    parallel` — foi um dos erros do commit da main.
  - Nota de sintaxe: a cláusula `default` **não** é aceita em `omp for` (só em `parallel`,
    `teams` e `task`). Era o outro erro daquele commit.
  - Nota de portabilidade: `VIZINHOS` aparece em `shared(...)` e é `static const`. Isso é
    aceito a partir do OpenMP 5.0 (gcc ≥ 9, verificado no gcc 13.3), mas era **erro de
    compilação** em versões anteriores, em que variáveis `const` eram *predetermined shared*.
    Se o ambiente de avaliação usar gcc antigo, remover `VIZINHOS` da lista.

---

## Validação sequencial × paralela (executada)

| Entrada | L×C | LIMIAR | Passos | Ignições | Resultado |
|---|---|---|---|---|---|
| E1 | 1000×1500 | 100 | 9 | 23 | **idêntico** (exceto tempo) |
| E2 | 2000×2000 | 90 | 8 | 8 | **idêntico** (exceto tempo) |
| E3 | 500×500 | 400 | 2 | 0 | **idêntico** (exceto tempo), inclusive `pico_ignicoes: -1 0` |
| E4 | 1000×1500 | 40 | 200 | 109 862 | **idêntico** (exceto tempo), inclusive com contenção ativa (8 526 células) |
| E5 | 1200×1500 | 5000 | 4 | 0 | **idêntico** (exceto tempo) — nenhuma ignição, `pico_ignicoes: -1 0` |
| E6 | 1200×1500 | 35 | 0 | 0 | **idêntico** (exceto tempo) — P = 0, nenhum passo executado |
| E7 | 1200×1500 | 35 | 0 | 0 | **idêntico** (exceto tempo) — F = 0, nenhuma célula em chamas na inicialização |

E4 também confirmou invariância a T (1, 2, 4, 8): mesmo `checksum` `5674319175939320070`.

Re-verificado depois do alinhamento do trecho cronometrado e da correção do `pico_passo`: as 3
entradas de `tests/in` e as 3 de `entrada_carga_*` continuam idênticas ao sequencial exceto pelo
tempo, inclusive com o campo T da entrada variado em 1, 2, 4 e 8, e os casos de borda P = 0,
F = 0 e sem ignição agora batem em **todos** os campos. Nenhuma divergência de resultado entre
as versões permanece conhecida. `make test` passa 3/3 nas duas versões, e a versão paralela
roda limpa sob `-fsanitize=address,undefined`.

> Nenhuma medição de speedup foi feita: o contêiner de verificação tem **1 núcleo** (`nproc = 1`).
> Os tempos precisam ser coletados na máquina de experimentos.

---

## Pendências, em ordem de prioridade

1. **Parametrizar e comparar dois `schedule`** — requisito explícito da seção 13 e
   insumo da tabela 8 e da figura 3 do relatório.
2. **Escrever o `Makefile`** (modelo no apêndice A do `relatorio.tex`).
3. **Coletar os tempos** em máquina multicore e preencher as tabelas 5–8 e as figuras 1–3.
4. **Decidir as flags de compilação do experimento**: com `-O2` (atual) o `simd` não tem efeito
   nenhum; `-O3` e/ou `-march=native` fazem os laços vetorizarem. A escolha precisa ser
   registrada na tabela 4 do relatório, já que afeta os tempos e o speedup.

---

## Relatório
- [~] Descrição da solução sequencial — seção 3, estrutura pronta, texto a escrever
- [~] Descrição da solução paralela e estratégia de paralelização — seção 4, com as
      otimizações do código já identificadas e listadas
- [~] Validação da versão paralela contra a sequencial — seção 5, script e tabelas prontos
- [ ] Ambiente experimental (hardware, compilador, flags) — tabela 4, a preencher
- [ ] Tabela de tempos para diferentes entradas/threads — tabela 5, a preencher
- [ ] Speedup e eficiência — tabela 6, formulário pronto
- [~] Gráficos — 3 figuras em `pgfplots` com dados de exemplo, a substituir
- [~] Análise dos resultados — seção 8, roteiro de 6 eixos de análise pronto