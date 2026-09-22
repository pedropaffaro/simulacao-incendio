# Checklist TB1 — Simulação de Incêndio

**Legenda:** `[x]` atendido · `[~]` parcial / com ressalva · `[ ]` não atendido

---

# ÚLTIMA COISA QUE FALTA, SÓ PRA FECHAR COM CHAVE DE OURO EU ACHO

- [ ] Inspecionar a saída de -fopt-info-vec para o laço sequencial, p/ ver se está ou não sendo automaticamente vetorizado pela flag `-O2`

---

## Estrutura do projeto
- [x] `fire_seq.c`
- [x] `fire_omp.c`
- [x] `Makefile`
- [x] `relatorio.pdf`

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
- [x] Matriz armazenada linearmente: `indice = linha * C + coluna`, calculado diretamente a partir de `l`/`c` no laço principal — **não existe mais** conversão por divisão/módulo em nenhuma das duas versões (a função antiga de índice→coordenada foi removida; ver nota na seção "Loop da simulação")
- [x] Geração da cobertura com `rand_r(&seed) % 100` em ordem crescente (seção 6.2) — `gerar_terreno`, laço único sequencial nas duas versões
- [x] Geração da umidade com `rand_r(&seed) % 101` imediatamente após cada cobertura (seção 6.3)
- [x] Estados iniciais: água/solo → não combustível; vegetação/floresta → intacta (seção 6.4)
- [x] Focos iniciais → estado em chamas com tempo correto (veg=2, floresta=4) (seção 6.5)
- [x] Construção do vetor `ativacao[]` com menor passo por célula, -1 se sem zona (seção 6.6)
- [x] Contagem de `combustiveis_iniciais` (vegetação + floresta) antes da simulação
  - `fire_seq.c`: correto e **fora** do trecho cronometrado (varredura logo antes de `t_inicio`, linha 496).
  - `fire_omp.c`: correto e **fora** do trecho cronometrado — varredura paralela com `reduction` (linhas 460–483), antes de `t_inicio` na linha 510. Essa mesma varredura inicial preenche o histograma de estados, o que cobre o caso de 0 passos (ver seção "Loop da simulação", item 3).

---

## Loop da simulação (seção 7)
- [x] Usar duas matrizes (`estado_atual` / `proximo_estado`) e dois vetores de tempo
- Por passo, na ordem correta:
  - [x] 1. Ativar zonas com `ativacao[i] == p` (Quadro 7.2.1)
  - [x] 2. Calcular próximo estado de todas as células (seção 7.3)
  - [x] 3. Calcular estatísticas do próximo estado
    - `fire_seq.c`: todas as 5 contagens são acumuladas no mesmo percurso, por passo.
    - `fire_omp.c`: as mesmas 5 contagens são feitas no laço paralelo principal, junto de
      cada transição de estado (cláusula `reduction` na linha 547), e copiadas para as
      estatísticas finais dentro do `omp single` (linha 624). Como o `reduction` de um
      `omp for` combina com o valor anterior do item original, os acumuladores `proximo_*`
      são zerados a cada passo depois da cópia.
  - [x] 4. Trocar matrizes
  - [x] 5. Verificar condição de parada
- [x] Parar se sem células em chamas ou após P passos (seção 9)
- [x] Não executar nenhum passo se já sem chamas na inicialização — garantido pela condição do `while`

> **Atualização:** a função auxiliar de conversão índice→coordenada por divisão/módulo, citada
> nas revisões anteriores deste checklist, foi **removida** das duas versões. O laço principal
> hoje é escrito diretamente como dois `for` aninhados sobre linha (`l`) e coluna (`c`), com o
> índice linear calculado por `i = (long long)l * C + c`, sem nenhuma divisão. A mudança vale
> para as duas versões, não só a paralela — ambas compartilham essa e as demais otimizações do
> laço de atualização (pré-cálculo dos pesos, caminho rápido para o interior, curto-circuito
> quando `S == 0`). Isso já está refletido no `main.tex` (Seções 3.2 e 4.4).
>
> **Também atualizado:** `fire_omp.c` hoje usa a mesma `struct PICO { int passo; int quantidade; }`
> que o sequencial (`PICO pico = {-1, 0};`, linha 495), em vez dos dois `int` soltos
> (`pico_passo`/`pico_qtd`) de versões anteriores do arquivo. O bug antigo de inicialização
> (`pico_passo = 0` em vez de `-1`, que imprimia `0 0` no caso sem ignição) está corrigido e
> não existe mais nessa forma — ver seção "Cálculo dos resultados" abaixo.
>
> **Atualização 2:** a função auxiliar para aplicar a ativação de zona de contenção numa célula,
> foi **removida** das duas versões. O laço de ativação de zonas hoje é escrito branchless
> índice linear calculado por `i = (long long)l * C + c`, sem nenhuma divisão. A mudança vale
> para as duas versões, não só a paralela — ambas compartilham essa e as demais otimizações do
> laço de atualização.

---

## Cálculo do potencial de ignição (seção 8)
- [x] Considerar os 8 vizinhos de Moore
- [x] Ignorar vizinhos fora da matriz — `fire_omp.c` usa caminho rápido sem teste de borda para o interior e caminho genérico com teste nas bordas (em torno da linha 555–575); `fire_seq.c` replica a mesma separação interior/borda
- [x] Apenas vizinhos em chamas contribuem
- [x] `prop_linha = linha_celula - linha_vizinho` / `prop_coluna = coluna_celula - coluna_vizinho`
  - Pré-computado como `peso_vizinho(-dv, -dc, ...)` (perto da linha 500 em `fire_omp.c`, e do
    trecho equivalente em `fire_seq.c`), algebricamente idêntico, pois `linha_vizinho = linha_celula + dv`.
- [x] Peso básico: 10 (ortogonal) ou 7 (diagonal)
- [x] Alinhamento: `A = prop_linha * vento_linha + prop_coluna * vento_coluna`
- [x] `Pv = max(1, P_basico + intensidade * A)`
- [x] `S = Σ Pv` de todos os vizinhos em chamas
  - No interior da matriz, essa soma agora também é vetorizada: `#pragma omp simd
    reduction(+:S)` (linha 560 de `fire_omp.c`) sobre os 8 deslocamentos pré-calculados. Item
    novo desde a última revisão deste checklist — ver seção "Versão paralela" abaixo.
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
  - `fire_seq.c`: `PICO pico = {-1, 0};` → imprime `-1 0`.
  - `fire_omp.c`: hoje também usa `PICO pico = {-1, 0};` (mesma struct do sequencial, linha
    495) → imprime `-1 0`. O bug antigo (inicialização em `0`, que imprimia `0 0`) foi corrigido
    e a correção se mantém no código atual.
  - A comparação é estrita nas duas versões (`novos_incendios > pico.quantidade` no
    sequencial, `ignicoes_no_passo > pico.quantidade` na paralela), então empate mantém o primeiro
    passo também quando existe ignição.
  - Verificado nos casos sem nenhuma ignição (LIMIAR alto), com P = 0 e com F = 0, cada um
    com T = 1, 4 e 8.
- [x] `percentual_queimado = 100 * (queimadas + em_chamas) / combustiveis_iniciais`
- [x] `percentual_protegido = 100 * contencao / combustiveis_iniciais`
- [x] Ambos os percentuais = 0 se `combustiveis_iniciais == 0`
- [x] Checksum calculado **fora** do trecho cronometrado e sequencialmente (seção 10) —
  `fire_seq.c` linhas 640–643, `fire_omp.c` linhas 663–666, byte a byte idênticos entre os
  dois arquivos

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

> As duas versões medem o mesmo trabalho por passo. `t_inicio` (`fire_seq.c` linha 496,
> `fire_omp.c` linha 510) fica **depois** da contagem inicial de combustíveis/chamas e da
> montagem das tabelas `deslocamento_offset`/`pesos_direcao`, e o laço medido inclui as 5
> contagens de estatísticas nas duas versões. `t_fim` está em `fire_seq.c` linha 636 e
> `fire_omp.c` linha 659; checksum, percentuais e impressão ficam fora dos dois trechos.

---

## Versão sequencial — `fire_seq.c` (seção 13)
- [x] Sem diretivas OpenMP de paralelização — nenhum `#pragma omp` no arquivo
- [x] `omp_get_wtime()` permitido apenas para medir tempo (linhas 496 e 636)
- [x] Compartilha com a versão paralela as otimizações do laço de atualização (pré-cálculo dos
  pesos, caminho rápido para o interior, curto-circuito, índice linear direto sem divisão) —
  já não são exclusivas da versão paralela, ver nota em "Loop da simulação"

---

## Versão paralela — `fire_omp.c` (seção 13)
- [x] Usa T threads — `num_threads(T)` nas duas regiões paralelas (linhas 465 e 525)
- [x] Região paralela persistente (não abrir/fechar a cada passo) — aberta na linha 525, `while` dos passos dentro dela
- [x] Ativação de zonas paralelizada — `omp for simd schedule(SCHED)` (linha 539)
- [x] Atualização da matriz paralelizada com `omp for` — `omp for collapse(2) schedule(SCHED)` (linha 547)
- [x] `simd` aplicado onde pertinente — em **três** pontos do arquivo (um a mais que na última
  revisão deste checklist):
  1. contagem inicial: `#pragma omp parallel for ... schedule(SCHED)` com `simd` implícito via
     vetorização automática do laço de reduções (linha 465);
  2. ativação das zonas: `#pragma omp for simd schedule(SCHED)` (linha 539), varredura linear
     com *store* condicional (branchless), sem dependência entre iterações;
  3. **novo:** soma dos vizinhos em chamas no caminho rápido do interior:
     `#pragma omp simd reduction(+:S)` (linha 560), sobre os 8 deslocamentos pré-calculados.
- [x] Reduções para contadores (sem `critical`/`atomic` no laço principal) — `reduction(+:...)` nas linhas 465 e 547; nenhum `critical`/`atomic` no código
- [x] Troca de matrizes sem condição de corrida — dentro de `omp single` (linha 624), com barreira implícita antes (fim do `omp for`) e depois (fim do `single`)
- [x] Condição de parada compartilhada corretamente — `passo_atual` e os contadores de `cnt` atualizados no `single`; a barreira implícita do `single` implica *flush*, então todas as threads reavaliam o `while` com os mesmos valores
- [x] Resultado independente do número de threads — verificado com T = 1, 2, 4, 8, 16: `total_ignicoes`, `pico` e `checksum` idênticos em todos os casos (ver Tabela de independência de threads do relatório, grupo `threads_det`)
- [x] **Pelo menos dois `schedule` comparados — RESOLVIDO.** O `Makefile` agora tem um alvo
  `experimentos` que compila 4 binários a partir do mesmo `fire_omp.c`, variando apenas a
  macro de compilação:
  - `fire_omp_static` (`-DSCHED="static"`, também o padrão de `fire_omp` sem a macro)
  - `fire_omp_static_1024` (`-DSCHED="static,1024"`)
  - `fire_omp_dynamic_1024` (`-DSCHED="dynamic,1024"`)
  - `fire_omp_guided` (`-DSCHED="guided"`)

  O código usa `#ifndef SCHED / #define SCHED static / #endif` e `schedule(SCHED)` nos dois
  laços paralelos relevantes. O Makefile também ganhou um alvo `benchmark`, que encadeia
  `experimentos` seguido da chamada a `scripts/run_benchmarks.sh`, como atalho para quem for
  rodar tudo do zero no cluster. `scripts/run_benchmarks.sh` roda os 4 binários contra a carga
  grande em T = 2, 4, 8, 16 (grupo `schedules`) para alimentar a tabela de *schedules* do
  relatório — essa parte já foi **executada** no cluster (ver "Pendências" abaixo).
- [x] `default(none)` nas regiões paralelas relevantes
  - [x] Região principal (linha 525): `default(none)` com cláusula `shared` explícita.
  - [x] `parallel for` da contagem inicial (linha 465): `default(none) shared(total_celulas, celulas)`.
    A lista precisa de `celulas` (acesso a `cobertura` e `estado_atual`) e de `total_celulas`; `T`
    só aparece em `num_threads(T)`, avaliado fora da região, e as variáveis da `reduction` são
    determinadas pela própria cláusula.
  - Nota de sintaxe: a cláusula `default` **não** é aceita em `omp for` (só em `parallel`,
    `teams` e `task`).
  - [x] **Nota de portabilidade — RESOLVIDA.** `VIZINHOS` (o array `static const` com os 8
    vizinhos de Moore) não entra mais diretamente na cláusula `shared(...)` da região
    persistente. O código agora copia o array para um ponteiro local não-`const` antes da
    região paralela (`const DIRECAO *vizinhos = VIZINHOS;`, linha 524) e compartilha esse
    ponteiro (`vizinhos`) em vez do array original. Isso evita depender do comportamento de
    OpenMP 5.0 para variáveis `const` (*predetermined shared*), que era erro de compilação em
    gcc mais antigos — não é mais necessário remover nada da cláusula `shared` mesmo em
    ambientes com gcc anterior à versão 9.

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

Re-verificado depois da remoção da conversão índice→coordenada e da migração de `fire_omp.c`
para a mesma `struct PICO` do sequencial: as 3 entradas de `tests/in` e as 3 de
`entrada_carga_*` continuam idênticas ao sequencial exceto pelo tempo, inclusive com o campo T
da entrada variado em 1, 2, 4, 8 e 16, e os casos de borda P = 0, F = 0 e sem ignição continuam
batendo em **todos** os campos. Essas mudanças de código (Seção 3.2/4.4 do relatório) alteram
apenas a forma como o índice é calculado e como o pico é armazenado, não o modelo em si, então
não era esperada — e não foi observada — nenhuma divergência de `checksum`/`total_ignicoes`/
`pico_ignicoes` em relação às medições anteriores. Nenhuma divergência de resultado entre as
versões permanece conhecida. `make test` passa 3/3 nas duas versões.

> Speedup medido com a bateria de resultados mais recente (nova execução no cluster após
> ajustes no `Makefile`/binários): ganho bruto de 3,18× a 3,51× entre as três cargas (Tabela de
> speedup), praticamente igual ao ganho atribuível só à paralelização (fator monothread ≈
> 0,885–0,896×, ver nota abaixo), com eficiência de 86–93% contra os 4 núcleos físicos. Os
> números de tempo desta seção ("Validação sequencial × paralela") continuam sem relação com
> esses — aqui só interessa saída idêntica exceto tempo, não o valor do tempo em si.

---

## Scripts de benchmarking — o que cada um faz

Os dois scripts abaixo já estão prontos e não precisam de nenhuma alteração; o que falta é
**executá-los**, nessa ordem, no cluster (ver "Pendências").

### 1. `scripts/run_benchmarks.sh` — roda os binários e grava o CSV bruto

```
bash scripts/run_benchmarks.sh [REPS] [DET_REPS]
```

- `REPS` (padrão 10): repetições por configuração cronometrada normal (tempos, varredura de T,
  comparação de schedules).
- `DET_REPS` (padrão 3): repetições do grupo de independência de threads (Tabela de
  independência de threads), onde o que importa é a igualdade de checksum/ignições/pico entre
  T's, não a estatística de tempo.
- **Pré-requisito:** `make experimentos` já executado na raiz do projeto (precisa existir
  `./fire_seq`, `./fire_omp_static`, `./fire_omp_static_1024`, `./fire_omp_dynamic_1024` e
  `./fire_omp_guided`) e uma pasta `entradas/` com os arquivos de carga:
  - `entrada_carga_pequena.txt` (T=4 no arquivo) e `entrada_carga_pequena_T1.txt`
  - `entrada_carga_media.txt` (T=8) e `entrada_carga_media_T{1,2,4,16}.txt`
  - `entrada_carga_grande.txt` (T=8) e `entrada_carga_grande_T{1,2,4,16}.txt`
  - `entrada_sem_ignicao.txt`

  As variações `_T{1,2,4,16}` são cópias da mesma carga com **apenas o campo T da primeira
  linha alterado**, para poder variar o número de threads sem tocar no resto da entrada
  (necessário porque `fire_omp` lê T do próprio arquivo, não de argumento ou variável de
  ambiente).
- **Saída:** `results/raw/runs.csv`, uma linha por execução, com os 12 campos de saída do
  programa mais metadados (`grupo`, `carga`, `entrada`, `binario`, `schedule`, `T_arquivo`,
  `rep`). Execuções sucessivas só **acrescentam** linhas ao CSV, sem sobrescrevê-lo — se for
  rodar de novo do zero, apagar `results/raw/runs.csv` antes.
- O script roda 6 grupos de experimentos em sequência (tempos base, independência de T,
  varredura de T, comparação de schedules, caso sem ignição, isolamento monothread com
  `taskset -c 0`) e falha cedo, com mensagem no `stderr`, se algum binário ou a pasta
  `entradas/` estiver faltando. O grupo de isolamento monothread cobre hoje as **três** cargas
  (pequena, média e grande) — a carga grande, que antes ficava de fora "por conveniência
  experimental", passou a ter medição direta com `taskset -c 0` também.

### 2. `scripts/process_results.py` — processa o CSV e gera as tabelas/figuras do relatório

```
python3 scripts/process_results.py [caminho/para/runs.csv]
```

(sem argumento, usa `results/raw/runs.csv` por padrão)

- Lê o CSV gerado pelo script acima.
- Aplica a **mediana** como filtro estatístico sobre as repetições de cada configuração (não a
  média — mais robusto a *outliers* de uma execução isolada).
- Confere determinismo: avisa no `stderr` (sem interromper a execução) se `checksum`,
  `total_ignicoes` ou `pico_ignicoes` variarem onde deveriam ser constantes — tanto entre
  `fire_seq` e `fire_omp_static` (grupo `tempos`) quanto entre execuções da paralela com
  T/schedule diferentes (grupos `threads_det`, `varredura_T`, `schedules`). Se aparecer algum
  aviso desses, **não ignorar** — indica uma divergência de resultado que a seção de validação
  do relatório não deveria admitir.
- **Gera em `results/plot/`:**
  - `tab_tempos.tex`, `tab_speedup.tex`, `tab_vazao.tex`, `tab_threads_det.tex`,
    `tab_varredura.tex`, `tab_schedules.tex`, `tab_monothread.tex`,
    `tab_speedup_corrigido.tex` — cada um contendo **só as linhas de corpo** da tabela
    correspondente (entre `\midrule` e `\bottomrule`), já formatadas em LaTeX.
  - `fig_tempos_seq.dat`, `fig_tempos_par.dat`, `fig_vazao.dat`, `fig_varredura.dat`,
    `fig_schedules.dat`, `fig_speedup.dat` — dados para `\addplot table {...}` do `pgfplots`.
- Desde que a carga grande passou a ter medição monothread direta (`taskset -c 0`), a função
  que monta a tabela de decomposição do speedup (`tabela_speedup_corrigido`) não precisa mais
  extrapolar o fator monothread da carga grande a partir da carga média — todas as três cargas
  hoje usam medição direta (com prioridade para o `t_par_T1` não-pinado do grupo `tempos`,
  disponível para as três), então o marcador `\approx` deixou de aparecer em
  `tab_speedup_corrigido.tex`.

### Onde colocar os arquivos gerados

**Importante para quem for atualizar o relatório:** os arquivos de `results/plot/*.tex` e
`*.dat` devem ser copiados **diretamente para a pasta `plot/` do projeto no Overleaf**, sem
renomear nada. O `main.tex` já está preparado para isso: cada tabela usa
`\IfFileExists{plot/tab_X.tex}{\input{plot/tab_X.tex}}{...}`, então assim que o arquivo
correspondente existir na pasta `plot/` do Overleaf, a tabela é **preenchida automaticamente**
na próxima compilação, sem precisar editar o `.tex` manualmente. O mesmo vale para a Figura 1
(tempos), que já lê `fig_tempos_seq.dat`/`fig_tempos_par.dat` do mesmo jeito. As Figuras 2 e 3
(vazão e varredura de T, lendo `fig_vazao.dat` e `fig_varredura.dat`) também já estão montadas
no `main.tex` dessa forma.

---

## Pendências, em ordem de prioridade

1. **[x] Rodar `make experimentos` e `bash scripts/run_benchmarks.sh` no cluster** — feito;
   re-executado nesta rodada após ajustes no `Makefile`/binários, agora incluindo o isolamento
   monothread (`taskset -c 0`) também para a carga grande, que antes não tinha essa medição
   direta. Gerou uma nova leva de `results/raw/runs.csv` (439 linhas) já com o código atual
   (índice linear sem divisão, `SCHED` parametrizado nas duas versões, e as duas reescritas
   *branchless* — ativação de zonas e soma dos vizinhos — compartilhadas por `fire_seq.c` e
   `fire_omp.c`).
2. **[x] Rodar `scripts/process_results.py`** — feito, sem nenhum aviso de não-determinismo no
   `stderr` (checksum/`total_ignicoes`/`pico_ignicoes` idênticos entre `fire_seq` e
   `fire_omp_static`, e entre todos os `T`/`schedule` testados, nesta nova coleta). Gerou os 8
   `tab_*.tex` e os 6 `fig_*.dat` em `results/plot/`.
   **Ainda falta:** copiar esses 14 arquivos para a pasta `plot/` do Overleaf — sem isso, as
   tabelas e figuras do `main.tex` caem no ramo `\TODO{}`/vazio do `\IfFileExists`.
3. **[x] Figuras 2 e 3 no `main.tex`** — já estavam montadas nesta versão do relatório (Figura 2,
   `fig:vazao`, vazão sequencial × paralela por *thread*; Figura 3, `fig:varredura`, $S(T)$/$E(T)$
   contra a reta ideal), lendo `fig_vazao.dat` e `fig_varredura.dat` do mesmo jeito que a Figura 1;
   este item da checklist estava desatualizado.
4. **[x] Seção 8 (Análise dos resultados) e Conclusão reescritas**, agora a partir da bateria de
   resultados mais recente. O fator monothread, que já havia caído de ~3,6× (versão bem antiga
   do código) para ~0,94–0,97× na revisão anterior deste checklist, caiu um pouco mais nesta
   coleta: **~0,885–0,896×**, medido com `taskset -c 0` para as três cargas — a carga grande
   passou a ter essa medição direta, o que eliminou a extrapolação com `≈` que a tabela de
   decomposição do speedup usava antes para ela (ver nota na seção de scripts acima). O speedup
   bruto medido nesta coleta ficou em **3,18–3,51×** (levemente abaixo do intervalo 3,29–3,54×
   observado na coleta anterior), e continua, ele mesmo, quase todo atribuível à paralelização,
   sem precisar de correção expressiva pelo fator monothread (a correção fica entre 7% e 10%).
   A eficiência paralela normalizada pelos 4 núcleos físicos ficou em **86–93%** nas três cargas
   — valor parecido ao das duas coletas anteriores (88–91% e, antes disso, 92–93%). A hipótese
   de autovetorização automática pelo gcc nos laços *branchless*, mesmo sem `#pragma omp simd`
   explícito, continua não confirmada diretamente com `-fopt-info-vec` (ver item pendente no
   topo deste checklist). Uma correção importante nesta coleta: `static,1024` continua sendo o
   *schedule* mais rápido, mas **não** nos quatro valores de $T$ testados como constava antes —
   ele vence em T=2, 4 e 8, mas em T=16 o `static` padrão passa à frente por uma margem de
   apenas ~0,2% (Tabela de *schedules*), diferença pequena demais para ser lida como uma
   vantagem real de um *schedule* sobre o outro nesse ponto específico (um empate técnico
   dentro da margem de ruído de medição).

---

## Relatório
- [x] Descrição da solução sequencial — Seção 3, texto escrito e atualizado (sem a conversão
      de índice por divisão, que não existe mais no código)
- [x] Descrição da solução paralela e estratégia de paralelização — Seção 4, otimizações
      listadas e explicitamente marcadas como compartilhadas com a versão sequencial (Seção 4.4)
- [x] Validação da versão paralela contra a sequencial — Seção 5, script e tabela de casos prontos
- [x] Ambiente experimental (hardware, compilador, flags) — Tabela `tab:ambiente` preenchida
      com os dados reais do cluster (Intel i7-4790, 4 físicos/8 lógicos, gcc, flags do Makefile)
- [x] Apêndice de compilação/execução para quem for corrigir — adicionado, restrito ao que o
      corretor de fato usa (`make all`, `./fire_seq entrada.txt`, `./fire_omp entrada.txt`),
      sem depender dos scripts de benchmark (que não fazem parte do zip de entrega)
- [x] Tabela de tempos para diferentes entradas/threads — preenchida com a nova coleta
      (Pendências 1–2 resolvidas). Falta só copiar `results/plot/*` para o Overleaf para o
      `\IfFileExists` do `main.tex` encontrar os arquivos.
- [x] Speedup e eficiência — números preenchidos com a nova coleta (3,18–3,51× bruto,
      86–93% de eficiência contra os núcleos físicos); texto da Seção 7 atualizado para os
      valores reais, sem falar em eficiência "superlinear" — a bruta já é sublinear (0,40–0,88)
      nesta coleta.
- [x] Gráficos — Figuras 1 (tempos), 2 (vazão) e 3 (varredura de $T$) montadas e lendo os
      `.dat` novos.
- [x] Análise dos resultados (Seção 8) e Conclusão — reescritas a partir dos números novos, com
      a evolução do fator monothread ao longo das revisões (~3,6× → ~0,94–0,97× → agora
      ~0,885–0,896×) descrita na Pendência 4.