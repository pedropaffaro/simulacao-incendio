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
- [~] Contagem de `combustiveis_iniciais` (vegetação + floresta) antes da simulação
  - `fire_seq.c`: correto e **fora** do trecho cronometrado (linhas 417–437).
  - `fire_omp.c`: correto, mas **dentro** do trecho cronometrado (linhas 365–377, após
    `t_inicio` na linha 363). Ver seção "Medição do tempo".

---

## Loop da simulação (seção 7)
- [x] Usar duas matrizes (`estado_atual` / `proximo_estado`) e dois vetores de tempo
- Por passo, na ordem correta:
  - [x] 1. Ativar zonas com `ativacao[i] == p` (Quadro 7.2.1)
  - [x] 2. Calcular próximo estado de todas as células (seção 7.3)
  - [~] 3. Calcular estatísticas do próximo estado
    - `fire_seq.c`: todas as 5 contagens são acumuladas no mesmo percurso, por passo.
    - `fire_omp.c`: por passo só calcula `proximo_celulas_em_chamas` e `ignicoes_no_passo`.
      `nao_combustiveis`, `intactas`, `queimadas` e `contencao` são contadas **uma única vez
      após o laço** (linhas 509–518), já fora do tempo medido. O **resultado final é o
      mesmo** (verificado), mas o trecho cronometrado das duas versões deixa de ser
      equivalente — ver seção "Medição do tempo".
  - [x] 4. Trocar matrizes
  - [x] 5. Verificar condição de parada
- [x] Parar se sem células em chamas ou após P passos (seção 9)
- [x] Não executar nenhum passo se já sem chamas na inicialização — garantido pela condição do `while`

---

## Cálculo do potencial de ignição (seção 8)
- [x] Considerar os 8 vizinhos de Moore
- [x] Ignorar vizinhos fora da matriz — `fire_omp.c` usa caminho rápido sem teste de borda para o interior e caminho genérico com teste nas bordas (linhas 424–442)
- [x] Apenas vizinhos em chamas contribuem
- [x] `prop_linha = linha_celula - linha_vizinho` / `prop_coluna = coluna_celula - coluna_vizinho`
  - Em `fire_omp.c` isso é pré-computado como `peso_vizinho(-dv, -dc, ...)` (linha 387), que é
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
- [ ] `pico_ignicoes: -1 0` se nenhuma ignição ocorreu — **DIVERGÊNCIA CONFIRMADA**
  - `fire_seq.c` linha 442: `PICO pico = {-1, 0};` → imprime `-1 0`. Correto.
  - `fire_omp.c` linha 394: `int pico_passo = 0;` → imprime `0 0`. **Incorreto.**
  - Reproduzido: entrada 500×500, LIMIAR 400, 1 foco, nenhuma ignição →
    `seq: pico_ignicoes: -1 0` vs. `omp: pico_ignicoes: 0 0`.
  - **Correção:** `int pico_passo = -1;`
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
- [~] Cronometrado: ativação de zonas, propagação, atualização de estados e tempos, estatísticas, troca de matrizes, condição de parada
- [~] Não cronometrado: leitura, validação, alocação, geração de cobertura/umidade, mapa de ativação, focos, checksum, percentuais, impressão

> **Os dois trechos cronometrados não medem o mesmo trabalho.** Em `fire_omp.c`:
> - **entra no tempo** e não deveria: a contagem inicial de combustíveis/chamas
>   (linhas 365–377) e a montagem das tabelas `deslocamento_offset`/`pesos_direcao`
>   (linhas 379–388);
> - **sai do tempo** e deveria entrar: as estatísticas por passo do item 3 da seção 7.1
>   (movidas para depois do laço, linhas 509–518).
>
> Os dois efeitos têm sinais opostos, mas o segundo é o mais relevante: a versão sequencial
> paga as 5 contagens em **todos os P passos** dentro do tempo medido, e a paralela paga
> apenas uma varredura fora dele. Isso **infla o speedup reportado**. Recomendação: mover
> `t_inicio` para depois da contagem inicial e das tabelas, e acrescentar as 4 contagens
> faltantes à `reduction` do laço principal (linha 415).

---

## Versão sequencial — `fire_seq.c` (seção 13)
- [x] Sem diretivas OpenMP de paralelização — nenhum `#pragma omp` no arquivo
- [x] `omp_get_wtime()` permitido apenas para medir tempo (linhas 439 e 561)

---

## Versão paralela — `fire_omp.c` (seção 13)
- [x] Usa T threads — `num_threads(T)` nas três regiões paralelas
- [x] Região paralela persistente (não abrir/fechar a cada passo) — aberta na linha 398, `while` dos passos dentro dela
- [x] Ativação de zonas paralelizada — `omp for schedule(static)` (linhas 407–412)
- [x] Atualização da matriz paralelizada com `omp for` — `omp for collapse(2)` (linha 415)
- [ ] `simd` aplicado onde pertinente — **nenhuma diretiva `simd` no arquivo**
  - Candidatos naturais: o laço de ativação das zonas (linhas 408–412) e os laços de contagem
    (linhas 369–377 e 511–518), que são varreduras lineares sem acesso indireto.
    Sugestão: `#pragma omp for simd schedule(static)`.
- [x] Reduções para contadores (sem `critical`/`atomic` no laço principal) — `reduction(+:...)` nas linhas 368, 415 e 509; nenhum `critical`/`atomic` no código
- [x] Troca de matrizes sem condição de corrida — dentro de `omp single` (linhas 474–495), com barreira implícita antes (fim do `omp for`) e depois (fim do `single`)
- [x] Condição de parada compartilhada corretamente — `passo_atual` e `celulas_em_chamas` atualizados no `single`; a barreira implícita do `single` implica *flush*, então todas as threads reavaliam o `while` com os mesmos valores
- [x] Resultado independente do número de threads — verificado com T = 1, 2, 4, 8: `total_ignicoes`, `pico` e `checksum` idênticos em todos os casos
- [ ] Pelo menos dois `schedule` comparados (ex: `static` vs `dynamic`) — o código tem **`schedule(static)` fixo** (linhas 407 e 415); não há parametrização nem registro de comparação
  - Sugestão: `#ifndef SCHED` `#define SCHED static` `#endif` + `schedule(SCHED)`, gerando dois
    binários com `-DSCHED='dynamic,1024'`, e reportar na tabela 8 do relatório.
- [~] `default(none)` nas regiões paralelas relevantes
  - [x] Região principal (linha 398): `default(none)` com cláusula `shared` explícita.
  - [ ] Os dois `parallel for` auxiliares (linhas 368 e 509) não declaram `default(none)`.
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
| E3 | 500×500 | 400 | 2 | 0 | **divergente** em `pico_ignicoes` (`-1 0` vs `0 0`) |
| E4 | 1000×1500 | 40 | 200 | 109 862 | **idêntico** (exceto tempo), inclusive com contenção ativa (8 526 células) |

E4 também confirmou invariância a T (1, 2, 4, 8): mesmo `checksum` `5674319175939320070`.

> Nenhuma medição de speedup foi feita: o contêiner de verificação tem **1 núcleo** (`nproc = 1`).
> Os tempos precisam ser coletados na máquina de experimentos.

---

## Pendências, em ordem de prioridade

1. **Corrigir `pico_passo = -1`** em `fire_omp.c` linha 394. É a única divergência de
   *resultado* entre as versões e viola explicitamente a seção 10 do enunciado.
2. **Alinhar o trecho cronometrado** das duas versões (mover `t_inicio` para depois da
   contagem inicial; trazer as 4 estatísticas faltantes para dentro do laço paralelo).
   Sem isso, os números de speedup do relatório não são defensáveis.
3. **Adicionar `simd`** em pelo menos um laço — requisito explícito da seção 13.
4. **Parametrizar e comparar dois `schedule`** — requisito explícito da seção 13 e
   insumo da tabela 8 e da figura 3 do relatório.
5. **Escrever o `Makefile`** (modelo no apêndice A do `relatorio.tex`).
6. **Coletar os tempos** em máquina multicore e preencher as tabelas 5–8 e as figuras 1–3.
7. Acrescentar `default(none)` aos dois `parallel for` auxiliares.
8.  Pendente: em `fire_omp.c` linha 468, `proximo_tempo[i] = tempo_atual[i]` (o sequencial usa `0`) — equivalente hoje porque
   esses estados sempre têm tempo 0, mas é frágil; padronizar para `0`.

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