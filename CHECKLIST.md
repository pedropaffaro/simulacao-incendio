# Checklist TB1 — Simulação de Incêndio

## Estrutura do projeto
- [ ] `fire_seq.c`
- [ ] `fire_omp.c`
- [ ] `Makefile`
- [ ] `relatorio.pdf`

---

## Leitura e validação da entrada (seção 5)
- [ ] Exatamente 1 argumento (nome do arquivo)
- [ ] Arquivo abre com sucesso
- [ ] L > 0, C > 0, P ≥ 0, T > 0, LIMIAR > 0
- [ ] Componentes do vento ∈ {-1, 0, 1} e não ambos zero
- [ ] Intensidade ∈ [0, 5]
- [ ] F ≥ 0, Z ≥ 0
- [ ] Focos dentro da matriz
- [ ] Sem focos repetidos
- [ ] Focos sobre células combustíveis (vegetação ou floresta)
- [ ] Zonas completamente internas à matriz
- [ ] Limites iniciais ≤ limites finais de cada zona
- [ ] 0 ≤ passo_ativacao < P para cada zona

---

## Preparação da simulação (seção 6)
- [ ] Matriz armazenada linearmente: `indice = linha * C + coluna`
- [ ] Geração da cobertura com `rand_r(&seed) % 100` em ordem crescente (seção 6.2)
- [ ] Geração da umidade com `rand_r(&seed) % 101` imediatamente após cada cobertura (seção 6.3)
- [ ] Estados iniciais: água/solo → não combustível; vegetação/floresta → intacta (seção 6.4)
- [ ] Focos iniciais → estado em chamas com tempo correto (veg=2, floresta=4) (seção 6.5)
- [ ] Construção do vetor `ativacao[]` com menor passo por célula, -1 se sem zona (seção 6.6)
- [ ] Contagem de `combustiveis_iniciais` (vegetação + floresta) antes da simulação

---

## Loop da simulação (seção 7)
- [ ] Usar duas matrizes (`estado_atual` / `proximo_estado`) e dois vetores de tempo
- [ ] Por passo, na ordem correta:
  - [ ] 1. Ativar zonas com `ativacao[i] == p` (Quadro 7.2.1)
  - [ ] 2. Calcular próximo estado de todas as células (seção 7.3)
  - [ ] 3. Calcular estatísticas do próximo estado
  - [ ] 4. Trocar matrizes
  - [ ] 5. Verificar condição de parada
- [ ] Parar se sem células em chamas ou após P passos (seção 9)
- [ ] Não executar nenhum passo se já sem chamas na inicialização

---

## Cálculo do potencial de ignição (seção 8)
- [ ] Considerar os 8 vizinhos de Moore
- [ ] Ignorar vizinhos fora da matriz
- [ ] Apenas vizinhos em chamas contribuem
- [ ] `prop_linha = linha_celula - linha_vizinho` / `prop_coluna = coluna_celula - coluna_vizinho`
- [ ] Peso básico: 10 (ortogonal) ou 7 (diagonal)
- [ ] Alinhamento: `A = prop_linha * vento_linha + prop_coluna * vento_coluna`
- [ ] `Pv = max(1, P_basico + intensidade * A)`
- [ ] `S = Σ Pv` de todos os vizinhos em chamas
- [ ] `I = (S * fator_combustivel * (100 - umidade)) / 100` (aritmética inteira)
- [ ] Ignição se `I >= LIMIAR`

---

## Cálculo dos resultados (seção 10)
- [ ] `total_ignicoes`: células intacta → em chamas durante a simulação (focos iniciais não contam)
- [ ] `pico_ignicoes`: maior quantidade de novas ignições por passo; empate → primeiro passo
- [ ] `pico_ignicoes: -1 0` se nenhuma ignição ocorreu
- [ ] `percentual_queimado = 100 * (queimadas + em_chamas) / combustiveis_iniciais`
- [ ] `percentual_protegido = 100 * contencao / combustiveis_iniciais`
- [ ] Ambos os percentuais = 0 se `combustiveis_iniciais == 0`
- [ ] Checksum calculado **fora** do trecho cronometrado e sequencialmente (seção 10)

---

## Saída (seção 11)
- [ ] Formato exato dos 12 campos
- [ ] `percentual_queimado` e `percentual_protegido` com 2 casas decimais (`%.2f`)
- [ ] `tempo` com 6 casas decimais (`%.6f`)
- [ ] `checksum` como inteiro sem sinal (`%llu`)

---

## Medição do tempo (seção 12)
- [ ] Usar `omp_get_wtime()`
- [ ] Cronometrado: ativação de zonas, propagação, atualização de estados e tempos, estatísticas, troca de matrizes, condição de parada
- [ ] Não cronometrado: leitura, validação, alocação, geração de cobertura/umidade, mapa de ativação, focos, checksum, percentuais, impressão

---

## Versão sequencial — `fire_seq.c` (seção 13)
- [ ] Sem diretivas OpenMP de paralelização
- [ ] `omp_get_wtime()` permitido apenas para medir tempo

---

## Versão paralela — `fire_omp.c` (seção 13)
- [ ] Usa T threads
- [ ] Região paralela persistente (não abrir/fechar a cada passo)
- [ ] Ativação de zonas paralelizada
- [ ] Atualização da matriz paralelizada com `omp for`
- [ ] `simd` aplicado onde pertinente
- [ ] Reduções para contadores (sem `critical`/`atomic` no laço principal)
- [ ] Troca de matrizes sem condição de corrida
- [ ] Condição de parada compartilhada corretamente
- [ ] Resultado independente do número de threads
- [ ] Pelo menos dois `schedule` comparados (ex: `static` vs `dynamic`)
- [ ] `default(none)` nas regiões paralelas relevantes

---

## Relatório
- [ ] Descrição da solução sequencial
- [ ] Descrição da solução paralela e estratégia de paralelização
- [ ] Validação da versão paralela contra a sequencial
- [ ] Ambiente experimental (hardware, compilador, flags)
- [ ] Tabela de tempos para diferentes entradas/threads
- [ ] Speedup e eficiência
- [ ] Gráficos
- [ ] Análise dos resultados
