Universidade de São Paulo - ICMC/SSC
SSC0903 - Computação de Alto Desempenho
Primeiro Trabalho Prático (TB1) - Resolução em Grupo

Disponível em 01º/09/2026 - Entrega via e-disciplinas até 23/09/2026 às 23:59h

Trabalho prático: simulação paralela da propagação direcional de incêndio com zonas de
contenção

## 1. Visão geral do trabalho

O trabalho visa simular a propagação de incêndio em uma área florestal, utilizando C e OpenMP.
Devem ser desenvolvidas duas versões do algoritmo, uma sequencial em C e outra paralela em
C/OpenMP. A simulação considera uma matriz retangular cujas células representam regiões
específicas da floresta, a direção do vento, a intensidade do vento e possíveis zonas de
contenção de incêndio em células específicas. As zonas de contenção, se existirem em uma
célula, são ativadas em momentos (passos) pré-determinados. A simulação deverá calcular
como o incêndio se propaga a partir de focos iniciais, considerando o tipo de cobertura de cada
célula, a umidade da vegetação, a quantidade de células vizinhas em chamas, a posição dessas
células vizinhas, a direção e a intensidade do vento, a ativação programada de zonas de
contenção em células e o tempo necessário para a queima de cada tipo de vegetação nas
células. A simulação será determinística. Para uma mesma entrada, as versões sequencial e
paralela devem produzir o mesmo resultado.

## 2. O que deverá ser calculado

A simulação considera passos que representam momentos no tempo. A simulação determinará,
a cada passo, se a célula permanece intacta, se entra em chamas, se continua em chamas, se
termina sua queima ou se é transformada em uma célula de contenção.

Ao final da simulação, deverão ser calculados o número de passos executados, a quantidade
final de células não combustíveis, a quantidade final de células intactas, a quantidade final de
células em chamas, a quantidade final de células queimadas, a quantidade final de células de
contenção, o total de novas ignições durante a simulação, o passo com maior número de novas
ignições, a quantidade de ignições ocorridas nesse passo, o percentual da área inicialmente
combustível que foi queimada, o percentual da área inicialmente combustível que foi protegida,
o checksum da configuração final, e tempo de execução do núcleo da simulação.

## 3. Execução dos programas

As versões sequencial e paralela deverão ser executadas por:

```
./fire_seq entrada.txt
./fire_omp entrada.txt
```

Os programas receberão apenas o nome do arquivo de entrada como argumento.

## 4. Formato do arquivo de entrada

O arquivo de entrada conterá quatro conjuntos de dados: (1) configuração geral da simulação,
(2) configuração do vento para a floresta, (3) quantidade de focos de incêndio e de zonas de
contenção, e (4) descrição dos focos de incêndio e das zonas de contenção. Estes conjuntos de
dados serão detalhados a seguir.

### 4.1 Primeira linha: configuração geral

A primeira linha do arquivo de entrada é usada para determinar a configuração geral da
simulação, contendo L, C, P, T, seed e LIMIAR. Os parâmetros L e C determinam o número de
linhas e colunas da matriz. O número máximo de passos para a simulação é dado por P; e T
determina o número de threads da versão OpenMP. A semente para geração pseudoaleatória
é dada por seed e o potencial mínimo para ocorrer um novo incêndio em uma célula é dado por
LIMIAR. Os passos, durante a simulação, serão numerados de 0 a P-1.

### 4.2 Segunda linha: configuração do vento

O vento manterá a direção e intensidade durante toda a simulação e será formado por duas
componentes: vento_linha, representando a componente vertical da direção do vento e
vento_coluna, representando a componente horizontal.

As componentes da direção poderão assumir -1, 0 ou 1, mas não poderão ser simultaneamente
zero (vide Quadro 4.2.1).

Quadro 4.2.1 – Valores usados nas componentes da direção do vento.

| vento_linha | vento_coluna | Direção   |
|:-----------:|:------------:|-----------|
| -1          | 0            | norte     |
| -1          | 1            | nordeste  |
| 0           | 1            | leste     |
| 1           | 1            | sudeste   |
| 1           | 0            | sul       |
| 1           | -1           | sudoeste  |
| 0           | -1           | oeste     |
| -1          | -1           | noroeste  |

A intensidade V do vento é um valor determinado pelo usuário, entre 0 e 5.

### 4.3 Focos e zonas

A partir da terceira linha do arquivo de entrada são determinados os focos iniciais de incêndio
(F) e a quantidade de zonas de contenção (Z).

As próximas F linhas determinam os focos iniciais de incêndio, contendo a dupla linha coluna
para indicar uma célula que deverá iniciar a simulação em chamas.

Após, as próximas Z linhas determinam as zonas de contenção de incêndio. Estas linhas contêm
o passo de ativação da contenção, a linha e coluna inicial e a linha e coluna_final da zona
de contenção. Desta forma, cada zona será um retângulo com limites inclusivos. O exemplo
abaixo ilustra esta descrição:

```
15 100 200 120 260
```

e significa que, no início do passo 15, será ativada uma zona contendo linhas de 100 a 120 e
colunas de 200 a 260. Zonas de contenção diferentes poderão se sobrepor.

### 4.4 Exemplo de arquivo de entrada

O exemplo de arquivo de entrada a seguir (Quadro 4.4.1) ilustra a seguinte parametrização da
simulação: (1ª linha) matriz com 1000 linhas e 1500 colunas, no máximo 200 passos, 8 threads,
semente 2027, limiar de ignição 100, (2ª linha) vento para leste, com intensidade 3, (3ª linha)
três focos iniciais e duas zonas de contenção. Nas 4ª, 5ª e 6ª linhas, as células [500, 300],
[500,750] e [500,1200] indicam os focos iniciais de incêndio que iniciarão a simulação em
chamas. As duas últimas linhas (7ª e 8ª linhas) determinam as zonas de contenção, como
explicado anteriormente.

Quadro 4.4.1 – Exemplo de arquivo de entrada para a configuração da simulação.

```
1000 1500 200 8 2027 100
0 1 3
3 2
500 300
500 750
500 1200
20 400 600 600 620
35 300 900 700 915
```

## 5. Validação da entrada

Os algoritmos sequencial e paralelo que implementam a simulação proposta deverão validar a
entrada do arquivo texto contendo os parâmetros já descritos, considerando: presença de um
único argumento, abertura do arquivo, L > 0, C > 0, P ≥ 0, T > 0, LIMIAR > 0, componentes do
vento entre -1 e 1, direção diferente de (0,0), intensidade entre 0 e 5, F ≥ 0, Z ≥ 0; focos dentro
da matriz, ausência de focos repetidos, focos posicionados sobre células combustíveis, zonas
completamente internas à matriz, limites iniciais não superiores aos finais, e 0 ≤ passo_ativacao
< P.

## 6. Descrição do uso das entradas na preparação da simulação

### 6.1 Construção da matriz

A área deverá ser representada por uma matriz com L × C células, armazenada linearmente:
indice = linha * C + coluna. Cada célula possuirá o seu tipo de cobertura, umidade, estado e
tempo de queima.

### 6.2 Geração da cobertura

A geração da cobertura (vide Quadro 6.2.1) será feita por uma única thread que percorrerá a
matriz em ordem crescente de linha e coluna. Para cada célula será calculado o valor =
rand_r(&seed) % 100, e a cobertura será dada por:

Quadro 6.2.1 – Distribuição da cobertura da área em células, considerando 10% de células
contendo água, 10% contendo solo sem cobertura, 35% com vegetação rasteira e 45% com
florestas.

| Valor gerado | Código | Cobertura                |
|:------------:|:------:|--------------------------|
| 0 a 9        | 0      | Água (10%)               |
| 10 a 19      | 1      | solo exposto (10%)       |
| 20 a 54      | 2      | vegetação rasteira (35%) |
| 55 a 99      | 3      | Floresta (45%)           |

Os fatores de combustível aplicados para cada tipo de cobertura estão descritos no Quadro
6.2.2.

Quadro 6.2.2 – Fatores (pesos) para o início de incêndios, considerando o tipo de cobertura
existente nas células.

| Cobertura          | Fator |
|--------------------|:-----:|
| Água               | 0     |
| Solo exposto       | 0     |
| Vegetação rasteira | 8     |
| Floresta           | 12    |

### 6.3 Geração da umidade

A umidade de cada célula será um inteiro entre 0 e 100 e será gerada imediatamente após a
geração da cobertura da respectiva célula, desta forma: umidade = rand_r(&seed) % 101.
Deve-se seguir esta ordem: gerar a cobertura da célula, gerar sua umidade, e avançar para a
próxima célula.

### 6.4 Estados das células

Os estados previstos para as células estão descritos no Quadro 6.4.1, com seus respectivos
códigos. As coberturas Água e Solo Exposto são do tipo "não combustível". Todas as células
com coberturas Vegetação Rasteira e Floresta são iniciadas como "intactas", até a aplicação
dos focos iniciais de incêndio.

Quadro 6.4.1 – Estados possíveis para as células, com seus respectivos códigos.

| Código | Estado          |
|:------:|-----------------|
| 0      | não combustível |
| 1      | intacta         |
| 2      | em chamas       |
| 3      | queimada        |
| 4      | contenção       |

### 6.5 Aplicação dos focos iniciais de incêndio

Após a geração da matriz, as posições informadas como focos deverão passar ao estado em
chamas. O tempo inicial de queima será de 02 passos para Vegetação Rasteira e de 04
passos para Floresta. Um foco de incêndio sobre uma célula contendo água ou solo exposto
deverá ser considerado uma entrada inválida.

### 6.6 Construção do mapa de contenção

Antes do início da simulação, deverá ser criado o vetor ativacao[indice], contendo o valor -1 se
a célula não pertencer a uma zona ou o número do passo de ativação da zona, caso contrário.
Se uma célula pertencer a zonas sobrepostas, deverá ser armazenado o menor passo de
ativação. É importante reforçar que a construção desse mapa não fará parte do trecho
cronometrado.

## 7. Funcionamento da simulação

A simulação deverá utilizar duas matrizes, sendo uma delas com o estado_atual e a outra com
o proximo_estado. Deverá ter também dois vetores de tempo com tempo_atual e
proximo_tempo. Os valores do próximo passo deverão ser calculados exclusivamente a partir
dos valores do passo atual.

### 7.1 Ordem de execução de cada passo

Para cada passo p, a ordem será:

1. ativar as zonas programadas para p;
2. calcular o próximo estado de todas as células;
3. calcular as estatísticas do próximo estado;
4. trocar as matrizes;
5. verificar a condição de parada.

### 7.2 Ativação das zonas

No início do passo p, cada célula para a qual ativacao[indice] == p deverá ser tratada como
apresentado no Quadro 7.2.1.

Quadro 7.2.1 – Descrição da ativação das zonas considerando o vetor de ativação e o estado
atual.

| Estado atual    | Estado após a ativação |
|-----------------|------------------------|
| Intacta         | Contenção              |
| Em chamas       | Em chamas              |
| Queimada        | Queimada               |
| Não combustível | Não combustível        |
| Contenção       | Contenção              |

Uma zona atua somente no passo indicado. A contenção não pode ser aplicada em uma célula
que já esteja em chamas, i.e., a contenção não pode ser empregada para apagar um incêndio.

### 7.3 Atualização das células

Uma célula não combustível permanece não combustível. Uma célula intacta precisa determinar
seu potencial de ignição. Se o potencial for maior ou igual a LIMIAR, a célula entrará em chamas.
Caso contrário, permanecerá intacta. As células em chamas terão seu tempo de queima
reduzido em uma unidade. Se o novo tempo for zero, passará ao estado queimada. Células
queimadas permanecerão queimadas até o fim da simulação. Células de contenção
permanecerão como contenção até o fim da simulação.

## 8. Cálculo do potencial de ignição

O cálculo do potencial de ignição deverá considerar os 08 (oito) Vizinhos de Moore. Vizinhos
fora da matriz serão ignorados. Somente vizinhos em chamas contribuirão para o potencial. O
potencial de ignição de uma célula é feito em uma sequência de passos.

Inicialmente deve-se calcular o sentido de propagação do fogo. Para um vizinho em chamas, o
sentido de propagação do fogo, partindo do vizinho para a célula avaliada, é dado por:

```
prop_linha  = linha_celula - linha_vizinho
prop_coluna = coluna_celula - coluna_vizinho
```

O peso básico de uma célula vizinha para o cálculo do potencial de ignição da célula em análise
é dado pelo peso 10 para posição ortogonal dos vizinhos e 07 para posição diagonal. O vizinho
será ortogonal quando abs(prop_linha) + abs(prop_coluna) == 1.

O alinhamento com o vento em uma célula, para cada vizinho em chamas, é dado por:

```
A = prop_linha × vento_linha + prop_coluna × vento_coluna
```

O valor de A poderá ser -2, -1, 0, 1 ou 2, sendo que 2 indica uma propagação diagonal totalmente
alinhada, 1 indica uma propagação favorecida, 0 uma influência neutra, -1 uma propagação
desfavorecida e -2 propagação diagonal totalmente contrária.

O peso do vizinho será dado por:

```
Pv = max(1, Pbásico + intensidade × A)
```

A intensidade zero faz com que o peso direcional seja igual ao peso básico.

Os pesos de todos os vizinhos de Moore em chamas serão somados:

```
S = Σ Pv
```

O potencial de ignição será dado por:

```
I = floor( S × fator_combustível × (100 − umidade) / 100 )
```

E a célula entrará em chamas quando: I ≥ LIMIAR.

Ao entrar em chamas, a célula receberá tempo 2 se for vegetação rasteira, ou o tempo 4 se for
floresta. Todos os cálculos deverão usar aritmética inteira.

## 9. Condição de parada

A simulação terminará quando não existirem células em chamas ou quando forem concluídos P
passos. Se não existirem células em chamas depois da inicialização, nenhum passo será
executado.

## 10. Cálculo dos resultados

O total de ignições será a soma das células que passaram de "intactas" para "em chamas"
durante a simulação. Os focos iniciais não serão incluídos nesse total.

Para o cálculo do pico de ignições, informando \<passo\> \<quantidade aferida\>, deverão ser
registrados, a cada passo, o número do atual passo e a quantidade de novas ignições naquele
passo. O pico será o maior valor observado e, em caso de empate, prevalecerá o primeiro passo.
Se não ocorrer nenhuma nova ignição na simulação: pico_ignicoes: -1 0.

Para calcular o percentual queimado, primeiro, deve-se registrar a quantidade de células
combustíveis iniciais, sendo: vegetação rasteira + floresta. Ao final, deve-se calcular:

```
percentual_queimado = 100 × (queimadas + em_chamas) / combustíveis_iniciais
```

As células que ainda estiverem em chamas serão contabilizadas porque já foram atingidas pelo
incêndio.

O percentual protegido será determinado pela equação a seguir:

```
percentual_protegido = 100 × contencao / combustiveis_iniciais
```

Se não houver células inicialmente combustíveis, os dois percentuais serão zero.

O checksum será determinado depois da simulação e fora do trecho cronometrado, da seguinte
forma:

```c
unsigned long long checksum = 0;

for (long long i = 0; i < L * C; i++) {
    checksum = checksum * 31ULL
             + (unsigned long long)estado_atual[i];

    checksum = checksum * 31ULL
             + (unsigned long long)tempo_atual[i];
}
```

O checksum deverá ser calculado sequencialmente e na ordem linear da matriz.

## 11. Saída

A aplicação deverá imprimir, exatamente, estes valores:

```
passos: <valor>
nao_combustiveis: <valor>
intactas: <valor>
em_chamas: <valor>
queimadas: <valor>
contencao: <valor>
total_ignicoes: <valor>
pico_ignicoes: <passo> <quantidade>
percentual_queimado: <valor com 2 casas>
percentual_protegido: <valor com 2 casas>
checksum: <valor inteiro sem sinal>
tempo: <valor com 6 casas>
```

Exemplo de um arquivo de saída:

```
passos: 37
nao_combustiveis: 298441
intactas: 704328
em_chamas: 0
queimadas: 412906
contencao: 84325
total_ignicoes: 412903
pico_ignicoes: 18 24681
percentual_queimado: 34.38
percentual_protegido: 7.03
checksum: 1274839201837462911
tempo: 1.274839
```

Os números acima são ilustrativos. As versões sequencial e paralela deverão produzir valores
idênticos em todos os campos, exceto no tempo.

## 12. Medição do tempo

O trecho cronometrado deverá incluir a ativação das zonas, cálculo da propagação, atualização
dos estados, atualização dos tempos de queima, cálculo das estatísticas, troca das matrizes e
verificação da condição de parada.

O trecho cronometrado não deverá incluir a leitura e validação da entrada, alocação de memória,
geração da cobertura e da umidade, construção do mapa de ativação, definição dos focos,
checksum, cálculo dos percentuais, e impressão.

O tempo deverá ser medido com omp_get_wtime().

## 13. Implementações solicitadas

A versão sequencial deve estar no arquivo fire_seq.c. A versão sequencial não deverá utilizar
diretivas OpenMP para paralelização. A biblioteca OpenMP poderá ser usada somente para
medir o tempo.

A versão paralela deve estar no arquivo fire_omp.c. A versão paralela deverá utilizar T threads,
empregar uma região paralela persistente, paralelizar a ativação das zonas, paralelizar a
atualização da matriz, utilizar omp for, simd, utilizar reduções para os contadores, evitar
condições de corrida, trocar as matrizes de forma segura, compartilhar corretamente a condição
de parada, evitar critical e atomic dentro do laço principal quando reduções puderem ser
usadas, produzir resultados independentes do número de threads, comparar pelo menos dois
schedules, e utilizar default(none) nas regiões relevantes. O uso de task, paralelismo aninhado
e visualização não é obrigatório.

## 14. Entrega

O arquivo .zip deverá conter o fire_seq.c, fire_omp.c, Makefile e o relatorio.pdf.
O relatório deverá incluir: descrição das soluções sequencial e paralela, incluindo a estratégia
de paralelização, validação da versão paralela contra a versão sequencial, ambiente
experimental das duas versões, tempos, speedup, eficiência, gráficos, e análise dos resultados
obtidos.

## 15. Observação final

Este trabalho foi elaborado especificamente para a disciplina SSC0903 – Computação de Alto
Desempenho. A simulação utiliza conceitos gerais de autômatos celulares e de programação
paralela em memória compartilhada. As regras de propagação, contenção, geração da floresta,
formato de entrada e estatísticas foram definidas para esta atividade e não pretendem reproduzir
um modelo físico específico de propagação de incêndios.
