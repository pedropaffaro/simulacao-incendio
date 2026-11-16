# Trabalho 2: simulação de incêndio em C/MPI/OpenMP

SSC0903, Computação de Alto Desempenho. Versão paralela híbrida (`fire_mpi_omp.c`) da simulação do trabalho 1, mais os testes, os scripts de medição e o relatório.

## Organização

```
trab2/
├── entrega/                  O QUE VAI NO .ZIP, e só isso
│   ├── fire_mpi_omp.c
│   ├── Makefile              compila só o fire_mpi_omp, com mpicc
│   └── relatorio.pdf         entra aqui no final (make relatorio, ou o PDF do Overleaf)
├── testes/                   tudo que serve pra testar e medir, nada daqui é entregue
│   ├── referencia/fire_seq.c sequencial entregue no trabalho 1 (gabarito e base do speedup)
│   ├── entradas/cargas/      as 3 cargas (pequena, média, grande), usadas na validação e nos benchmarks
│   ├── entradas/bordas/      15 casos de borda válidos
│   ├── entradas/invalidas/   25 entradas que têm que ser rejeitadas
│   ├── esperado/             gabaritos gerados pelo fire_seq (saída sem a linha do tempo)
│   ├── hosts/hosts.exemplo   modelo de hostfile
│   ├── scripts/config.sh     configuração única: hostfile, flags do mpirun, NPrc, T, repetições
│   ├── scripts/validar.sh    corretude
│   ├── scripts/run_benchmarks.sh   medições -> testes/resultados/
│   └── scripts/process_results.py  resultados -> relatorio/plot/
├── relatorio/
│   ├── main.tex              relatório, lê tabelas, figuras e números de plot/
│   └── plot/                 gerado pelo process_results.py
├── Makefile                  atalhos de desenvolvimento (não é o da entrega)
└── Trab02-Fire-C-MPI-OMP.pdf enunciado
```

## Passo a passo

```bash
make                # compila entrega/fire_mpi_omp e testes/bin/fire_seq
make test           # valida contra o sequencial (use make test-rapido pra pular as 3 cargas)
make resultados     # roda os benchmarks e gera relatorio/plot/
make relatorio      # compila o PDF e copia pra entrega/relatorio.pdf (ou use o Overleaf)
make zip            # gera trab2_entrega.zip com fire_mpi_omp.c, Makefile e relatorio.pdf
```

Execução manual, no formato do enunciado:

```bash
mpirun --hostfile testes/hosts/hosts.txt -np 4 entrega/fire_mpi_omp testes/entradas/cargas/entrada_carga_grande.txt
```

O número de threads de cada processo é o `T` da primeira linha do arquivo de entrada.

## Configuração dos testes no cluster

Tudo fica em `testes/scripts/config.sh` e pode ser sobrescrito pelo ambiente:

```bash
cp testes/hosts/hosts.exemplo testes/hosts/hosts.txt    # e edite com os nós de verdade
HOSTFILE=testes/hosts/hosts.txt NP_LIST="1 2 4 8" T_LIST="1 2 4 8" REPS=5 make resultados
```

| Variável | Padrão | Significado |
|---|---|---|
| `HOSTFILE` | vazio (máquina local) | arquivo de hosts passado ao `mpirun` |
| `MPIRUN_FLAGS` | `--map-by node --bind-to none` | flags extras do `mpirun` |
| `NP_LIST` | `1 2 4` | números de processos testados |
| `T_LIST` | `1 2 4` | threads por processo testadas |
| `REPS` | `5` | repetições por configuração (o relatório usa a mediana) |

Pontos de atenção:

- **`--bind-to none` importa.** Com NPrc igual a 1 ou 2, o Open MPI prende cada processo a um núcleo e as T threads disputam esse núcleo. O resultado continua certo, mas o tempo fica ruim. Os scripts já passam a flag; no comando puro do enunciado ela não existe.
- **`--map-by node`** espalha os processos entre os nós. Sem ela o Open MPI enche o primeiro nó antes de usar o segundo.
- Se NPrc passar dos `slots` do hostfile, o `mpirun` recusa. Aumente os `slots` ou acrescente `--oversubscribe` em `MPIRUN_FLAGS`.
- O projeto precisa estar em um diretório visível por todos os nós (os scripts criam as entradas temporárias em `testes/.tmp/` por isso).
- Escolha `NP_LIST` e `T_LIST` de forma que alguns totais NPrc x T se repitam (por exemplo 2x4 e 4x2). É isso que alimenta a comparação "processos ou threads" do relatório.
- Cada bateria nova guarda o `runs.csv` anterior com a data no nome, nunca mistura.

## Relatório

`make resultados` gera em `relatorio/plot/` as tabelas (`tab_*.tex`), as figuras (`fig_*.tex`) e os números citados no texto (`valores.tex`). No Overleaf, envie `relatorio/main.tex` e a pasta `relatorio/plot/` inteira e compile com pdfLaTeX.

O que não é automático: os trechos em vermelho `[TODO: ...]` na análise. São frases de interpretação que dependem do formato das curvas medidas, e cada TODO diz exatamente o que olhar e o que escrever. Enquanto `plot/` não existir, todas as tabelas, figuras e números também aparecem como TODO vermelho, então dá pra compilar antes de medir.

O texto supõe as três cargas com os nomes `pequena`, `media` e `grande`. Se uma carga for acrescentada em `testes/entradas/cargas/`, ela entra nos benchmarks e nas tabelas-resumo sozinha, e a maior passa a ser a carga de referência das análises, mas as tabelas detalhadas citadas pelo nome no `main.tex` continuam sendo as dessas três.

## Como a solução atende a seção 13 do enunciado

| Requisito | Onde |
|---|---|
| Usar os NPrc processos do `mpirun` | decomposição em blocos de linhas, um por processo (`dividir_linhas`) |
| Usar T threads | `num_threads(T)` na região paralela de cada processo |
| Região paralela persistente | um único `omp parallel` em volta do `while` dos passos |
| Ativação das zonas paralelizada | `omp for simd` sobre as células reais do bloco |
| Atualização da matriz paralelizada | `omp for` sobre as linhas do miolo e duas tarefas pras linhas de fronteira |
| Primitivas MPI coletivas | `MPI_Bcast`, `MPI_Scatterv`, `MPI_Neighbor_alltoallv`, `MPI_Allreduce`, `MPI_Gatherv`; não há `MPI_Send`/`MPI_Recv` |
| `omp tasks` | linhas de fronteira do bloco, criadas depois da troca das linhas fantasmas |
| `omp for`, `simd` | ativação (`for simd`), miolo (`for`), soma dos 8 vizinhos (`simd reduction`) |
| Reduções nos contadores | `reduction` no `omp for` e `MPI_Allreduce` entre processos |
| Sem condição de corrida, sem `critical`/`atomic` | cada linha é escrita por uma iteração ou uma tarefa; cada tarefa devolve as próprias estatísticas |
| Troca segura das matrizes | pela thread principal, entre duas barreiras |
| Condição de parada compartilhada | contadores globais do `MPI_Allreduce`, iguais em todos os processos, e barreira antes do `while` |
| Resultado independente de NPrc e T | `make test`, com NPrc maior que o número de linhas incluso |

## Decisões que valem conferir com o grupo

1. **Trecho cronometrado.** Inclui a troca de fronteiras e o `MPI_Allreduce` de cada passo. Não inclui o `MPI_Scatterv` inicial nem o `MPI_Gatherv` final, tratados como parte da preparação e do checksum, que o enunciado exclui. O enunciado não fala de distribuição explicitamente; se o professor entender diferente, é só mover `t_inicio` pra antes dos `MPI_Scatterv`.
2. **Geração da matriz na raiz.** É sequencial por causa do `rand_r`, então a raiz guarda a matriz global e o tamanho do problema é limitado pela memória de um nó.
3. **MPI-3.** A troca de fronteiras usa `MPI_Neighbor_alltoallv`. Se o `mpi.h` do cluster for anterior ao MPI-3, o código compila sozinho uma alternativa com `MPI_Sendrecv` (testada), que não é coletiva.
4. **OpenMP 4.0.** O código não usa nada além do OpenMP 4.0 (sem `taskloop`, sem redução em tarefa), por causa do gcc 5.3.1 registrado no relatório do trabalho 1.
5. **Erro de entrada.** A raiz imprime `[Erro] ...`, avisa os outros processos pelo broadcast e todos saem com código de falha, então o `mpirun` também acusa saída diferente de zero na stderr. Nada é impresso na stdout.
