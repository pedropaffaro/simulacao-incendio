#define _POSIX_C_SOURCE 200112L
#include <mpi.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

// Configuração de compilação
/* Tipo de escalonamento padrão das cláusulas OpenMP, podendo ser redefinido na compilação (-DSCHED=...) */
#ifndef SCHED
    #define SCHED static
#endif

/* Rank do processo raiz, que lê e valida a entrada, gera a matriz, distribui as linhas, junta o resultado e imprime */
#define PROC_RAIZ 0

// Constantes e tipos do modelo
/* Representa uma direção(vertical, horizontal) */
// Usada pra descrever o deslocamento até os vizinhos de uma célula
typedef struct {
    int vertical;
    int horizontal;
} DIRECAO;

#define DIRECAO_NORTE    (DIRECAO){-1,  0}
#define DIRECAO_NORDESTE (DIRECAO){-1,  1}
#define DIRECAO_LESTE    (DIRECAO){ 0,  1}
#define DIRECAO_SUDESTE  (DIRECAO){ 1,  1}
#define DIRECAO_SUL      (DIRECAO){ 1,  0}
#define DIRECAO_SUDOESTE (DIRECAO){ 1, -1}
#define DIRECAO_OESTE    (DIRECAO){ 0, -1}
#define DIRECAO_NOROESTE (DIRECAO){-1, -1}

/* Tabela com os 8 vizinhos de Moore, pra não ficar repetindo as 8 direções toda vez que algum trecho do código precisar varrer a vizinhança */
static const DIRECAO VIZINHOS[8] = {
    DIRECAO_NORTE,    DIRECAO_NORDESTE,
    DIRECAO_LESTE,    DIRECAO_SUDESTE,
    DIRECAO_SUL,      DIRECAO_SUDOESTE,
    DIRECAO_OESTE,    DIRECAO_NOROESTE
};

/* Tipo de cobertura de terreno de cada célula, sorteado na geração do mapa (gerar_terreno()) */
typedef enum {
    COBERTURA_CODIGO_AGUA = 0,
    COBERTURA_CODIGO_SOLO,
    COBERTURA_CODIGO_VEGETACAO,
    COBERTURA_CODIGO_FLORESTA
} COBERTURA_CODIGO;

/* Geração da cobertura (rand % 100) */
#define COBERTURA_MAX_AGUA     9  // 0 a 9   (10%)
#define COBERTURA_MAX_SOLO     19 // 10 a 19 (10%)
#define COBERTURA_MAX_RASTEIRA 54 // 20 a 54 (35%)
#define COBERTURA_MAX_FLORESTA 99 // 55 a 99 (45%)

/* Multiplicador de combustível usado no cálculo do potencial de ignição (potencial_ignicao())
Água e solo não queimam, então ficam em 0 */
#define FATOR_AGUA 0
#define FATOR_SOLO 0
#define FATOR_RASTEIRA 8
#define FATOR_FLORESTA 12

/* Estado de queima de cada célula durante a simulação, guardado em estado_atual/proximo_estado */
typedef enum {
    ESTADO_NAO_COMBUSTIVEL = 0,
    ESTADO_INTACTA,
    ESTADO_EM_CHAMAS,
    ESTADO_QUEIMADA,
    ESTADO_CONTENCAO
} ESTADO_CODIGO;

/* Quantos passos uma célula fica em chamas antes de virar queimada */
#define TEMPO_QUEIMA_RASTEIRA 2
#define TEMPO_QUEIMA_FLORESTA 4

/* Valor em ativacao[] pra célula que não pertence a nenhuma zona de contenção */
#define CELULA_SEM_CONTENCAO -1

/* Peso base de um vizinho no cálculo de S, antes do ajuste pelo vento */
#define PESO_ORTOGONAL 10
#define PESO_DIAGONAL 7

/* Faixa de valores válidos pra intensidade do vento, conferida na leitura da entrada */
#define INTENSIDADE_MIN 0
#define INTENSIDADE_MAX 5

/* Classifica o alinhamento entre a direção de um vizinho e a do vento
Só o primeiro (-2) precisa ser setado na mão porque o enum incrementa o resto em +1 */
typedef enum {
    ALINHAMENTO_DIAGONAL_CONTRARIO = -2,
    ALINHAMENTO_DESFAVORIDO,
    ALINHAMENTO_NEUTRO,
    ALINHAMENTO_FAVORAVEL,
    ALINHAMENTO_DIAGONAL_FAVORAVEL
} ALINHAMENTO_VENTO;

/* Código de retorno genérico pra função que não tem valor válido pra devolver no caso de erro */
#define ERRO -1

/* Ponteiros para os vetores da grade de células (matriz linearizada, célula (l, c) no índice l * C + c)
A raiz tem uma cópia global, e cada processo tem uma cópia local do seu bloco de linhas
Na cópia local, a linha 0 e a linha linhas_locais + 1 são linhas fantasmas, cópias das linhas vizinhas dos outros processos */
typedef struct {
    int *cobertura;
    int *umidade;
    int *estado_atual;
    int *tempo_atual;
    int *proximo_estado;
    int *proximo_tempo;
    int *ativacao;
} CELULAS;

/* Resultado de uma função de leitura/validação da entrada
Indica se deu certo, se foi erro de sistema ou valor inválido no arquivo de entrada */
typedef enum {
    LEITURA_OK = 0,
    LEITURA_ERRO_SISTEMA,
    LEITURA_ERRO_ENTRADA
} LEITURA_STATUS;

// Representa o pico de ignições da simulação(passo, quantidade)
/* Guarda em que passo ocorreu a maior quantidade de novas ignições, pra imprimir no resultado final */
typedef struct {
    int passo;
    int quantidade;
} PICO;

/* Contadores de células por estado da matriz inteira, mais o total de combustíveis iniciais e o total de ignições
Atualizados a cada passo e usados tanto na condição de parada quanto na impressão do resultado final */
typedef struct {
    int combustiveis_iniciais;
    int nao_combustiveis;
    int intactas;
    int em_chamas;
    int queimadas;
    int contencao;
    int total_ignicoes;
} COUNTERS;

// Tipos da versão paralela
/* Contagens do próximo estado de uma linha da matriz, devolvidas por atualizar_linha() */
typedef struct {
    int em_chamas;
    int ignicoes;
    int nao_combustiveis;
    int intactas;
    int queimadas;
    int contencao;
} ESTATISTICAS;

/* Posições do vetor de configuração que a raiz envia pra todos os processos
CFG_STATUS leva o resultado da leitura, pra todos encerrarem juntos se a entrada for inválida */
typedef enum {
    CFG_STATUS = 0,
    CFG_L,
    CFG_C,
    CFG_P,
    CFG_T,
    CFG_LIMIAR,
    CFG_VENTO_LINHA,
    CFG_VENTO_COLUNA,
    CFG_VENTO_INTENSIDADE,
    CFG_COMBUSTIVEIS_INICIAIS,
    CFG_NAO_COMBUSTIVEIS,
    CFG_INTACTAS,
    CFG_EM_CHAMAS,
    CFG_QUEIMADAS,
    CFG_CONTENCAO,
    CFG_TAMANHO
} CFG_POSICAO;

/* Posições do vetor de estatísticas de um passo, somado entre todos os processos */
typedef enum { EST_EM_CHAMAS = 0, EST_IGNICOES, EST_NAO_COMBUSTIVEIS, EST_INTACTAS, EST_QUEIMADAS, EST_CONTENCAO, EST_TAMANHO } EST_POSICAO;

/* Divisão da matriz entre os processos e dados usados na comunicação entre eles */
typedef struct {
    int rank;
    int num_processos;
    int *linhas_por_processo; // Quantas linhas cada processo recebe
    int *primeira_linha;      // Linha da matriz em que começa o bloco de cada processo
    int linhas_locais;        // Quantas linhas tem o bloco deste processo
    MPI_Comm comm_ativos;     // Só os processos que receberam pelo menos uma linha
    MPI_Comm comm_grade;      // Os processos ativos em fila, na ordem dos blocos
    int vizinho_cima;         // Dono do bloco anterior (MPI_PROC_NULL se não existir)
    int vizinho_baixo;        // Dono do bloco seguinte (MPI_PROC_NULL se não existir)
    MPI_Datatype tipo_linha;  // Uma linha inteira da matriz (C inteiros)
} DECOMPOSICAO;

/* Deslocamento de índice e peso (já com o vento) de cada um dos 8 vizinhos, na ordem de VIZINHOS[] */
typedef struct {
    long long deslocamento_offset[8];
    int pesos_direcao[8];
} VIZINHANCA;

// Regras do modelo
/* Devolve o multiplicador de combustível de uma cobertura, usado no cálculo do potencial de ignição */
int fator_cobertura(COBERTURA_CODIGO cobertura) {
    switch (cobertura) {
        case COBERTURA_CODIGO_VEGETACAO:
            return FATOR_RASTEIRA;
        case COBERTURA_CODIGO_FLORESTA:
            return FATOR_FLORESTA;
        default:
            return 0;
    }
}

/* Devolve quantos passos uma célula dessa cobertura fica em chamas
Só faz sentido pra vegetação/floresta, por isso o default devolve ERRO (a função não deve ser chamada com água/solo) */
int tempo_queima_inicial(COBERTURA_CODIGO cobertura) {
    switch (cobertura) {
        case COBERTURA_CODIGO_VEGETACAO:
            return TEMPO_QUEIMA_RASTEIRA;
        case COBERTURA_CODIGO_FLORESTA:
            return TEMPO_QUEIMA_FLORESTA;
        default:
            return ERRO;
    }
}

/* Calcula o peso de contribuição de um vizinho em chamas pro cálculo de S
Combina o peso base (ortogonal ou diagonal) com o alinhamento entre a direção do vizinho e a do vento, e nunca deixa o peso ficar menor que 1 */
int peso_vizinho(int prop_linha, int prop_coluna, int vento_linha, int vento_coluna, int intensidade) {
    int p_basico = (abs(prop_linha) + abs(prop_coluna) == 1) ? PESO_ORTOGONAL : PESO_DIAGONAL;

    int A = prop_linha * vento_linha + prop_coluna * vento_coluna;

    int peso_vizinho = p_basico + intensidade * A;

    return peso_vizinho < 1 ? 1 : peso_vizinho;
}

/* Calcula o potencial de ignição I a partir da soma dos pesos dos vizinhos em chamas (S), do fator de combustível da célula e da umidade */
int potencial_ignicao(int S, int fator_combustivel, int umidade) {
    return (S * fator_combustivel * (100 - umidade)) / 100;
}

/* Percentual de área queimada (em chamas + já queimada) em relação ao total de células combustíveis no início da simulação */
double percentual_queimado(int queimadas, int em_chamas, int combustiveis_iniciais) {
    if (combustiveis_iniciais == 0)
        return 0.0;

    return (100.0 * (queimadas + em_chamas)) / combustiveis_iniciais;
}

/* Percentual de área protegida por zona de contenção em relação ao total de células combustíveis no início da simulação */
double percentual_protegido(int contencoes, int combustiveis_iniciais) {
    if (combustiveis_iniciais == 0)
        return 0.0;

    return (100.0 * contencoes) / combustiveis_iniciais;
}

// Memória e geração da matriz
/* Aloca os 5 vetores da grade global, usada só pela raiz pra gerar a matriz e calcular o checksum
Devolve 0 se algum malloc falhar (com os outros já alocados, por isso sempre precisa chamar liberar_cels() depois) */
int alocar_cels_globais(CELULAS *g, long long total_celulas) {
    *g              = (CELULAS){0};
    g->cobertura    = malloc(total_celulas * sizeof(int));
    g->umidade      = malloc(total_celulas * sizeof(int));
    g->estado_atual = malloc(total_celulas * sizeof(int));
    g->tempo_atual  = malloc(total_celulas * sizeof(int));
    g->ativacao     = malloc(total_celulas * sizeof(int));

    if (!g->cobertura || !g->umidade || !g->estado_atual || !g->tempo_atual || !g->ativacao) {
        fprintf(stderr, "[Erro] Não foi possível alocar memoria para as estruturas da matriz.\n");
        return 0;
    }

    return 1;
}

/* Aloca os 7 vetores da grade local de um processo, já contando as duas linhas fantasmas
As linhas fantasmas começam como não combustível, então a borda da matriz, que não tem processo vizinho, nunca propaga fogo
Devolve 0 se algum malloc falhar (com os outros já alocados, por isso sempre precisa chamar liberar_cels() depois) */
int alocar_cels_locais(CELULAS *g, int linhas_locais, int C) {
    long long total_celulas = (long long)(linhas_locais + 2) * C;

    *g                = (CELULAS){0};
    g->cobertura      = malloc(total_celulas * sizeof(int));
    g->umidade        = malloc(total_celulas * sizeof(int));
    g->estado_atual   = malloc(total_celulas * sizeof(int));
    g->tempo_atual    = malloc(total_celulas * sizeof(int));
    g->proximo_estado = malloc(total_celulas * sizeof(int));
    g->proximo_tempo  = malloc(total_celulas * sizeof(int));
    g->ativacao       = malloc(total_celulas * sizeof(int));

    if (!g->cobertura || !g->umidade || !g->estado_atual || !g->tempo_atual || !g->proximo_estado || !g->proximo_tempo || !g->ativacao) {
        fprintf(stderr, "[Erro] Não foi possível alocar memoria para as estruturas da matriz.\n");
        return 0;
    }

    long long fantasma_inferior = (long long)(linhas_locais + 1) * C;
    for (int c = 0; c < C; c++) {
        g->estado_atual[c]                       = ESTADO_NAO_COMBUSTIVEL;
        g->proximo_estado[c]                     = ESTADO_NAO_COMBUSTIVEL;
        g->estado_atual[fantasma_inferior + c]   = ESTADO_NAO_COMBUSTIVEL;
        g->proximo_estado[fantasma_inferior + c] = ESTADO_NAO_COMBUSTIVEL;
    }

    return 1;
}

/* Libera os vetores alocados em alocar_cels_globais()/alocar_cels_locais() e zera os ponteiros, pra poder ser chamada mais de uma vez */
void liberar_cels(CELULAS *g) {
    free(g->cobertura);
    free(g->umidade);
    free(g->estado_atual);
    free(g->tempo_atual);
    free(g->proximo_estado);
    free(g->proximo_tempo);
    free(g->ativacao);
    *g = (CELULAS){0};
}

/* Sorteia a cobertura e a umidade de cada célula com rand_r, na ordem pedida, cobertura primeiro, depois umidade, célula por célula
É sequencial de propósito, porque a sequência do rand_r depende da ordem das chamadas */
void gerar_terreno(CELULAS *g, long long total_celulas, unsigned int seed) {
    for (long long i = 0; i < total_celulas; i++) {
        int val = rand_r(&seed) % 100;

        if (val <= COBERTURA_MAX_AGUA) {
            g->cobertura[i]    = COBERTURA_CODIGO_AGUA;
            g->estado_atual[i] = ESTADO_NAO_COMBUSTIVEL;
        } else if (val <= COBERTURA_MAX_SOLO) {
            g->cobertura[i]    = COBERTURA_CODIGO_SOLO;
            g->estado_atual[i] = ESTADO_NAO_COMBUSTIVEL;
        } else if (val <= COBERTURA_MAX_RASTEIRA) {
            g->cobertura[i]    = COBERTURA_CODIGO_VEGETACAO;
            g->estado_atual[i] = ESTADO_INTACTA;
        } else {
            g->cobertura[i]    = COBERTURA_CODIGO_FLORESTA;
            g->estado_atual[i] = ESTADO_INTACTA;
        }

        g->umidade[i]     = rand_r(&seed) % 101;
        g->tempo_atual[i] = 0;
        g->ativacao[i]    = CELULA_SEM_CONTENCAO;
    }
}

// Leitura e validação da entrada
/* Confere se o programa foi chamado com exatamente 1 argumento (o arquivo de entrada) */
LEITURA_STATUS validar_argc(int argc, const char *argv0) {
    if (argc != 2) {
        fprintf(stderr, "[Erro] Uso correto: mpirun --hostfile <arq_hosts> -np <NPrc> %s <arquivo_de_entrada>\n", argv0);
        return LEITURA_ERRO_SISTEMA;
    }

    return LEITURA_OK;
}

/* Abre o arquivo de entrada em modo leitura */
FILE *abrir_arquivo(const char *caminho) {
    FILE *f = fopen(caminho, "r");
    if (f == NULL)
        fprintf(stderr, "[Erro] Não foi possível abrir o arquivo de entrada '%s'.\n", caminho);

    return f;
}

/* Lê e valida a primeira linha do arquivo (L, C, P, T, seed, LIMIAR) */
LEITURA_STATUS ler_config_geral(FILE *input, int *L, int *C, int *P, int *T, unsigned int *seed, int *LIMIAR) {
    if (fscanf(input, "%d %d %d %d %u %d", L, C, P, T, seed, LIMIAR) != 6) {
        fprintf(stderr, "[Erro] Não foi possível ler a primeira linha do arquivo.\n");
        return LEITURA_ERRO_SISTEMA;
    }

    if (*L <= 0 || *C <= 0 || *P < 0 || *T <= 0 || *LIMIAR <= 0) {
        fprintf(stderr, "[Erro] Parâmetros gerais inválidos (L>0, C>0, P>=0, T>0, LIMIAR>0).\n");
        return LEITURA_ERRO_SISTEMA;
    }

    return LEITURA_OK;
}

/* Lê e valida a configuração do vento (direção e intensidade) */
LEITURA_STATUS ler_config_vento(FILE *input, int *vento_linha, int *vento_coluna, int *vento_intensidade) {
    if (fscanf(input, "%d %d %d", vento_linha, vento_coluna, vento_intensidade) != 3) {
        fprintf(stderr, "[Erro] Não foi possível ler a configuração do vento.\n");
        return LEITURA_ERRO_SISTEMA;
    }

    if (*vento_linha < -1 || *vento_linha > 1 || *vento_coluna < -1 || *vento_coluna > 1 || (*vento_linha == 0 && *vento_coluna == 0) ||
        *vento_intensidade < INTENSIDADE_MIN || *vento_intensidade > INTENSIDADE_MAX) {
        fprintf(stderr, "[Erro] Os valores inseridos para configuração do vento são inválidos.\n");
        return LEITURA_ERRO_ENTRADA;
    }

    return LEITURA_OK;
}

/* Lê e valida quantos focos iniciais (F) e zonas de contenção (Z) tem */
LEITURA_STATUS ler_contagem_focos_zonas(FILE *input, int *F, int *num_zonas) {
    if (fscanf(input, "%d %d", F, num_zonas) != 2) {
        fprintf(stderr, "[Erro] Não foi possível ler quantidades de focos e zonas de contenção.\n");
        return LEITURA_ERRO_SISTEMA;
    }

    if (*F < 0 || *num_zonas < 0) {
        fprintf(stderr, "[Erro] Quantidade de focos (F) ou zonas (Z) inválida.\n");
        return LEITURA_ERRO_SISTEMA;
    }

    return LEITURA_OK;
}

/* Lê os F focos iniciais e inicia o fogo neles, validando posição dentro da matriz, célula combustível e sem foco repetido */
LEITURA_STATUS ler_focos(FILE *input, int F, int L, int C, int *cobertura, int *estado_atual, int *tempo_atual) {
    for (int k = 0; k < F; k++) {
        int linha, coluna;

        if (fscanf(input, "%d %d", &linha, &coluna) != 2) {
            fprintf(stderr, "[Erro] Não foi possível ler o foco inicial %d.\n", k + 1);
            return LEITURA_ERRO_SISTEMA;
        }

        if (linha < 0 || linha >= L || coluna < 0 || coluna >= C) {
            fprintf(stderr, "[Erro] Os valores inseridos para os limites do foco inicial são inválidos.\n");
            return LEITURA_ERRO_ENTRADA;
        }

        long long idx = (long long)linha * C + coluna;

        if (estado_atual[idx] == ESTADO_EM_CHAMAS) {
            fprintf(stderr, "[Erro] Os valores inseridos do foco inicial são inválidos (foco repetido).\n");
            return LEITURA_ERRO_ENTRADA;
        }

        if (cobertura[idx] == COBERTURA_CODIGO_AGUA || cobertura[idx] == COBERTURA_CODIGO_SOLO) {
            fprintf(stderr, "[Erro] Os valores inseridos do foco inicial são inválidos.\n");
            return LEITURA_ERRO_ENTRADA;
        }

        estado_atual[idx] = ESTADO_EM_CHAMAS;
        tempo_atual[idx]  = tempo_queima_inicial(cobertura[idx]);
    }

    return LEITURA_OK;
}

/* Lê as zonas de contenção e monta o vetor ativacao[], guardando em cada célula o menor passo_ativacao entre todas as zonas que a cobrem */
LEITURA_STATUS ler_zonas_contencao(FILE *input, int num_zonas, int L, int C, int P, int *ativacao) {
    for (int k = 0; k < num_zonas; k++) {
        int passo_ativacao, linha_inicial, coluna_inicial, linha_final, coluna_final;

        if (fscanf(input, "%d %d %d %d %d", &passo_ativacao, &linha_inicial, &coluna_inicial, &linha_final, &coluna_final) != 5) {
            fprintf(stderr, "[Erro] Não foi possível ler a zona de contenção %d.\n", k + 1);
            return LEITURA_ERRO_SISTEMA;
        }

        if (passo_ativacao < 0 || passo_ativacao >= P || linha_inicial < 0 || linha_inicial >= L || coluna_inicial < 0 ||
            coluna_inicial >= C || linha_final < 0 || linha_final >= L || coluna_final < 0 || coluna_final >= C ||
            linha_inicial > linha_final || coluna_inicial > coluna_final) {
            fprintf(stderr, "[Erro] Os valores inseridos para contenção são inválidos.\n");
            return LEITURA_ERRO_ENTRADA;
        }

        for (int r = linha_inicial; r <= linha_final; r++) {
            for (int c = coluna_inicial; c <= coluna_final; c++) {
                long long idx = (long long)r * C + c;

                if (ativacao[idx] == CELULA_SEM_CONTENCAO || passo_ativacao < ativacao[idx])
                    ativacao[idx] = passo_ativacao;
            }
        }
    }

    return LEITURA_OK;
}

// Preparação na raiz
/* Lê e valida as três primeiras linhas da entrada, gravando os parâmetros direto no vetor de configuração */
LEITURA_STATUS ler_cabecalho(FILE *input, int *config, unsigned int *seed, int *F, int *num_zonas) {
    LEITURA_STATUS status =
        ler_config_geral(input, &config[CFG_L], &config[CFG_C], &config[CFG_P], &config[CFG_T], seed, &config[CFG_LIMIAR]);
    if (status != LEITURA_OK)
        return status;

    status = ler_config_vento(input, &config[CFG_VENTO_LINHA], &config[CFG_VENTO_COLUNA], &config[CFG_VENTO_INTENSIDADE]);
    if (status != LEITURA_OK)
        return status;

    return ler_contagem_focos_zonas(input, F, num_zonas);
}

/* Monta a grade global na raiz: aloca os vetores, sorteia o terreno, aplica os focos e as zonas de contenção */
LEITURA_STATUS carregar_grade(FILE *input, CELULAS *globais, const int *config, unsigned int seed, int F, int num_zonas) {
    int L = config[CFG_L];
    int C = config[CFG_C];
    int P = config[CFG_P];

    long long total_celulas = (long long)L * C;
    if (!alocar_cels_globais(globais, total_celulas))
        return LEITURA_ERRO_SISTEMA;

    gerar_terreno(globais, total_celulas, seed);

    LEITURA_STATUS status = ler_focos(input, F, L, C, globais->cobertura, globais->estado_atual, globais->tempo_atual);
    if (status != LEITURA_OK)
        return status;

    return ler_zonas_contencao(input, num_zonas, L, C, P, globais->ativacao);
}

/* Conta os combustíveis e os estados iniciais da matriz e grava os totais no vetor de configuração */
void contar_estados_iniciais(const CELULAS *globais, int *config) {
    int T                   = config[CFG_T];
    long long total_celulas = (long long)config[CFG_L] * config[CFG_C];
    const int *cobertura    = globais->cobertura;
    const int *estado       = globais->estado_atual;

    int init_combustiveis = 0, init_nao_combustiveis = 0, init_intactas = 0;
    int init_em_chamas = 0, init_queimadas = 0, init_contencao = 0;

    /* Varredura em paralelo, fora do trecho cronometrado */
    #pragma omp parallel for num_threads(T) schedule(SCHED) default(none) shared(total_celulas, cobertura, estado) \
    reduction(+ : init_combustiveis, init_nao_combustiveis, init_intactas, init_em_chamas, init_queimadas, init_contencao)
    for (long long i = 0; i < total_celulas; i++) {
        COBERTURA_CODIGO cob = (COBERTURA_CODIGO)cobertura[i];
        if (cob == COBERTURA_CODIGO_VEGETACAO || cob == COBERTURA_CODIGO_FLORESTA) {
            init_combustiveis++;
        }

        ESTADO_CODIGO est = (ESTADO_CODIGO)estado[i];
        if (est == ESTADO_NAO_COMBUSTIVEL)
            init_nao_combustiveis++;
        else if (est == ESTADO_INTACTA)
            init_intactas++;
        else if (est == ESTADO_EM_CHAMAS)
            init_em_chamas++;
        else if (est == ESTADO_QUEIMADA)
            init_queimadas++;
        else if (est == ESTADO_CONTENCAO)
            init_contencao++;
    }

    config[CFG_COMBUSTIVEIS_INICIAIS] = init_combustiveis;
    config[CFG_NAO_COMBUSTIVEIS]      = init_nao_combustiveis;
    config[CFG_INTACTAS]              = init_intactas;
    config[CFG_EM_CHAMAS]             = init_em_chamas;
    config[CFG_QUEIMADAS]             = init_queimadas;
    config[CFG_CONTENCAO]             = init_contencao;
}

/* Faz a preparação que só a raiz consegue fazer: lê e valida a entrada, monta a grade global e conta os estados iniciais
Em caso de erro, quem chamou continua responsável por liberar a grade global */
LEITURA_STATUS preparar_simulacao(int argc, char *argv[], CELULAS *globais, int *config) {
    if (validar_argc(argc, argv[0]) != LEITURA_OK)
        return LEITURA_ERRO_SISTEMA;

    FILE *input = abrir_arquivo(argv[argc - 1]);
    if (input == NULL)
        return LEITURA_ERRO_SISTEMA;

    unsigned int seed;
    int F, num_zonas;

    LEITURA_STATUS status = ler_cabecalho(input, config, &seed, &F, &num_zonas);
    if (status == LEITURA_OK)
        status = carregar_grade(input, globais, config, seed, F, num_zonas);

    fclose(input);

    if (status != LEITURA_OK)
        return status;

    contar_estados_iniciais(globais, config);

    return LEITURA_OK;
}

/* Prepara a simulação na raiz e envia a configuração pra todos os processos em um único broadcast
Devolve 0 se a raiz encontrou algum erro, e nesse caso todos os processos encerram juntos */
int compartilhar_configuracao(int argc, char *argv[], int suporte_threads, int rank, CELULAS *globais, int *config) {
    if (rank == PROC_RAIZ) {
        if (suporte_threads < MPI_THREAD_FUNNELED) {
            fprintf(stderr, "[Erro] A implementação MPI não oferece o nível de suporte a threads MPI_THREAD_FUNNELED.\n");
            config[CFG_STATUS] = LEITURA_ERRO_SISTEMA;
        } else {
            config[CFG_STATUS] = preparar_simulacao(argc, argv, globais, config);
        }
    }

    MPI_Bcast(config, CFG_TAMANHO, MPI_INT, PROC_RAIZ, MPI_COMM_WORLD);

    return config[CFG_STATUS] == LEITURA_OK;
}

// Decomposição de domínio
/* Divide as L linhas da matriz em blocos contíguos, um por processo, em ordem de rank
Quando a divisão não é exata, os primeiros processos ficam com uma linha a mais */
void dividir_linhas(int L, int num_processos, int *linhas_por_processo, int *primeira_linha) {
    int linhas_base  = L / num_processos;
    int linhas_resto = L % num_processos;

    int proxima_linha = 0;
    for (int p = 0; p < num_processos; p++) {
        linhas_por_processo[p] = linhas_base + (p < linhas_resto ? 1 : 0);
        primeira_linha[p]      = proxima_linha;
        proxima_linha += linhas_por_processo[p];
    }
}

/* Divide as linhas e cria os comunicadores, os vizinhos e o tipo de linha do processo
Devolve 0 se o processo ficou sem nenhuma linha (só acontece com mais processos do que linhas) */
int decompor_dominio(DECOMPOSICAO *dec, int L, int C) {
    dec->comm_ativos = MPI_COMM_NULL;
    dec->comm_grade  = MPI_COMM_NULL;
    dec->tipo_linha  = MPI_DATATYPE_NULL;

    MPI_Comm_rank(MPI_COMM_WORLD, &dec->rank);
    MPI_Comm_size(MPI_COMM_WORLD, &dec->num_processos);

    dec->linhas_por_processo = malloc(dec->num_processos * sizeof(int));
    dec->primeira_linha      = malloc(dec->num_processos * sizeof(int));
    if (!dec->linhas_por_processo || !dec->primeira_linha) {
        fprintf(stderr, "[Erro] Não foi possível alocar memoria para a divisão das linhas.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    dividir_linhas(L, dec->num_processos, dec->linhas_por_processo, dec->primeira_linha);
    dec->linhas_locais = dec->linhas_por_processo[dec->rank];

    /* Separa os processos que receberam pelo menos uma linha (quem ficou sem recebe MPI_COMM_NULL) */
    MPI_Comm_split(MPI_COMM_WORLD, dec->linhas_locais > 0 ? 1 : MPI_UNDEFINED, dec->rank, &dec->comm_ativos);
    if (dec->comm_ativos == MPI_COMM_NULL)
        return 0;

    /* Organiza os processos ativos em fila, na ordem dos blocos, pra cada um saber quem é o vizinho de cima e o de baixo
    Nas pontas da fila, o vizinho que não existe vem como MPI_PROC_NULL */
    int num_ativos = dec->num_processos < L ? dec->num_processos : L;
    int periodico  = 0;
    MPI_Cart_create(dec->comm_ativos, 1, &num_ativos, &periodico, 0, &dec->comm_grade);
    MPI_Cart_shift(dec->comm_grade, 0, 1, &dec->vizinho_cima, &dec->vizinho_baixo);

    /* Tipo que representa uma linha inteira da matriz, pra enviar e receber os dados em unidades de linha */
    MPI_Type_contiguous(C, MPI_INT, &dec->tipo_linha);
    MPI_Type_commit(&dec->tipo_linha);

    return 1;
}

/* Libera o que decompor_dominio() criou (um processo sem linhas não chega a criar a fila nem o tipo de linha) */
void liberar_decomposicao(DECOMPOSICAO *dec) {
    free(dec->linhas_por_processo);
    free(dec->primeira_linha);

    if (dec->tipo_linha != MPI_DATATYPE_NULL)
        MPI_Type_free(&dec->tipo_linha);
    if (dec->comm_grade != MPI_COMM_NULL)
        MPI_Comm_free(&dec->comm_grade);
    if (dec->comm_ativos != MPI_COMM_NULL)
        MPI_Comm_free(&dec->comm_ativos);
}

// Comunicação entre processos
/* Envia a cada processo o seu bloco de linhas de um vetor da grade global */
void espalhar_vetor(const int *global, int *local, const DECOMPOSICAO *dec, int C) {
    MPI_Scatterv(global, dec->linhas_por_processo, dec->primeira_linha, dec->tipo_linha, local + C, dec->linhas_locais, dec->tipo_linha,
                 PROC_RAIZ, dec->comm_grade);
}

/* Junta os blocos de um vetor de volta na grade global da raiz, na ordem das linhas */
void juntar_vetor(const int *local, int *global, const DECOMPOSICAO *dec, int C) {
    MPI_Gatherv(local + C, dec->linhas_locais, dec->tipo_linha, global, dec->linhas_por_processo, dec->primeira_linha, dec->tipo_linha,
                PROC_RAIZ, dec->comm_grade);
}

/* Distribui os 5 vetores do estado inicial (os de próximo passo não precisam, são escritos antes de serem lidos) */
void distribuir_grade(const CELULAS *globais, CELULAS *celulas, const DECOMPOSICAO *dec, int C) {
    espalhar_vetor(globais->cobertura, celulas->cobertura, dec, C);
    espalhar_vetor(globais->umidade, celulas->umidade, dec, C);
    espalhar_vetor(globais->estado_atual, celulas->estado_atual, dec, C);
    espalhar_vetor(globais->tempo_atual, celulas->tempo_atual, dec, C);
    espalhar_vetor(globais->ativacao, celulas->ativacao, dec, C);
}

/* Junta na raiz o estado e o tempo de queima finais, que são os vetores usados no checksum */
void juntar_grade(const CELULAS *celulas, CELULAS *globais, const DECOMPOSICAO *dec, int C) {
    juntar_vetor(celulas->estado_atual, globais->estado_atual, dec, C);
    juntar_vetor(celulas->tempo_atual, globais->tempo_atual, dec, C);
}

/* Envia a primeira e a última linha do bloco pros processos vizinhos e recebe deles as duas linhas fantasmas
Só o estado é trocado, porque o potencial de ignição só precisa saber se o vizinho está em chamas */
void trocar_fronteiras(int *estado, const DECOMPOSICAO *dec, int C) {
    int linhas_locais = dec->linhas_locais;

#if MPI_VERSION >= 3
    /* Posições em linhas: envia a primeira e a última linha real, recebe na fantasma de cima e na de baixo */
    const int quantidades[2]     = {1, 1};
    const int desloc_envio[2]    = {0, linhas_locais - 1};
    const int desloc_recebido[2] = {0, linhas_locais + 1};

    MPI_Neighbor_alltoallv(estado + C, quantidades, desloc_envio, dec->tipo_linha, estado, quantidades, desloc_recebido, dec->tipo_linha,
                           dec->comm_grade);
#else
    /* Alternativa pra MPI anterior à versão 3, que não tem as primitivas coletivas de vizinhança */
    int *primeira_real     = estado + C;
    int *ultima_real       = estado + (long long)linhas_locais * C;
    int *fantasma_superior = estado;
    int *fantasma_inferior = estado + (long long)(linhas_locais + 1) * C;

    MPI_Sendrecv(primeira_real, 1, dec->tipo_linha, dec->vizinho_cima, 0, fantasma_inferior, 1, dec->tipo_linha, dec->vizinho_baixo, 0,
                 dec->comm_grade, MPI_STATUS_IGNORE);
    MPI_Sendrecv(ultima_real, 1, dec->tipo_linha, dec->vizinho_baixo, 1, fantasma_superior, 1, dec->tipo_linha, dec->vizinho_cima, 1,
                 dec->comm_grade, MPI_STATUS_IGNORE);
#endif
}

/* Soma as estatísticas do passo de todos os processos
Todos recebem os mesmos totais, então a condição de parada é igual em todos eles */
void somar_estatisticas(int *estatisticas_locais, int *estatisticas_globais, MPI_Comm comm_grade) {
    MPI_Allreduce(estatisticas_locais, estatisticas_globais, EST_TAMANHO, MPI_INT, MPI_SUM, comm_grade);
}

// Atualização das células
/* Pré-calcula os deslocamentos de memória em 1D e os pesos direcionais com o vento pros 8 vizinhos de Moore
A direção é invertida no peso porque o fogo vai do vizinho pra célula */
VIZINHANCA calcular_vizinhanca(const int *config) {
    VIZINHANCA vizinhanca;

    for (int k = 0; k < 8; k++) {
        int variacao_linha  = VIZINHOS[k].vertical;
        int variacao_coluna = VIZINHOS[k].horizontal;

        vizinhanca.deslocamento_offset[k] = (long long)variacao_linha * config[CFG_C] + variacao_coluna;
        vizinhanca.pesos_direcao[k]       = peso_vizinho(-variacao_linha, -variacao_coluna, config[CFG_VENTO_LINHA],
                                                         config[CFG_VENTO_COLUNA], config[CFG_VENTO_INTENSIDADE]);
    }

    return vizinhanca;
}

/* Soma os pesos dos vizinhos em chamas de uma célula (o valor S)
As linhas de cima e de baixo sempre existem na grade local (reais ou fantasmas), então só os limites de coluna são checados */
static inline int somar_pesos_vizinhos(const int *estado, long long i, int c, int C, const long long *deslocamento_offset,
                                       const int *pesos_direcao) {
    int S = 0;

    if (c > 0 && c < C - 1) {
        /* Célula interna: soma os pesos dos vizinhos usando os offsets pré-calculados e vetorização SIMD */
        #pragma omp simd reduction(+ : S)
        for (int k = 0; k < 8; k++) {
            int atual_em_chamas = (estado[i + deslocamento_offset[k]] == ESTADO_EM_CHAMAS);
            S += pesos_direcao[k] * atual_em_chamas;
        }
    } else {
        /* Célula de borda: soma o peso dos vizinhos checando os limites de coluna da matriz */
        for (int k = 0; k < 8; k++) {
            int coluna_vizinho = c + VIZINHOS[k].horizontal;

            if (coluna_vizinho >= 0 && coluna_vizinho < C && estado[i + deslocamento_offset[k]] == ESTADO_EM_CHAMAS) {
                S += pesos_direcao[k];
            }
        }
    }

    return S;
}

/* Calcula o próximo estado e o próximo tempo das células de uma linha e devolve as contagens da linha
Lê só o passo atual e escreve só na própria linha, então linhas diferentes podem ser calculadas ao mesmo tempo */
static inline ESTATISTICAS atualizar_linha(const CELULAS *celulas, int l, int C, int LIMIAR, const long long *deslocamento_offset,
                                           const int *pesos_direcao) {
    ESTATISTICAS est = {0};

    for (int c = 0; c < C; c++) {
        long long i                 = (long long)l * C + c;
        ESTADO_CODIGO estado_celula = (ESTADO_CODIGO)celulas->estado_atual[i];

        if (estado_celula == ESTADO_INTACTA) {
            int S = somar_pesos_vizinhos(celulas->estado_atual, i, c, C, deslocamento_offset, pesos_direcao);

            /* Calcula o potencial de ignição e verifica se atinge o limiar pra pegar fogo */
            if (S > 0 && potencial_ignicao(S, fator_cobertura((COBERTURA_CODIGO)celulas->cobertura[i]), celulas->umidade[i]) >= LIMIAR) {
                celulas->proximo_estado[i] = ESTADO_EM_CHAMAS;
                celulas->proximo_tempo[i]  = tempo_queima_inicial((COBERTURA_CODIGO)celulas->cobertura[i]);
                est.em_chamas++;
                est.ignicoes++;
            } else {
                celulas->proximo_estado[i] = ESTADO_INTACTA;
                celulas->proximo_tempo[i]  = 0;
                est.intactas++;
            }

        } else if (estado_celula == ESTADO_EM_CHAMAS) {
            /* Decrementa o tempo de queima restante da célula */
            int tempo_ignicao_restante = celulas->tempo_atual[i] - 1;

            if (tempo_ignicao_restante == 0) {
                celulas->proximo_estado[i] = ESTADO_QUEIMADA;
                celulas->proximo_tempo[i]  = 0;
                est.queimadas++;
            } else {
                celulas->proximo_estado[i] = ESTADO_EM_CHAMAS;
                celulas->proximo_tempo[i]  = tempo_ignicao_restante;
                est.em_chamas++;
            }

        } else {
            /* Não combustível, queimada e contenção não mudam mais */
            celulas->proximo_estado[i] = estado_celula;
            celulas->proximo_tempo[i]  = 0;

            if (estado_celula == ESTADO_NAO_COMBUSTIVEL) {
                est.nao_combustiveis++;
            } else if (estado_celula == ESTADO_QUEIMADA) {
                est.queimadas++;
            } else if (estado_celula == ESTADO_CONTENCAO) {
                est.contencao++;
            }
        }
    }

    return est;
}

// Fechamento de um passo
/* Soma as contagens do miolo do bloco e das duas linhas de fronteira no vetor que vai pra redução global */
void empacotar_estatisticas(int *estatisticas_locais, ESTATISTICAS miolo, ESTATISTICAS borda_superior, ESTATISTICAS borda_inferior) {
    estatisticas_locais[EST_EM_CHAMAS]        = miolo.em_chamas + borda_superior.em_chamas + borda_inferior.em_chamas;
    estatisticas_locais[EST_IGNICOES]         = miolo.ignicoes + borda_superior.ignicoes + borda_inferior.ignicoes;
    estatisticas_locais[EST_NAO_COMBUSTIVEIS] = miolo.nao_combustiveis + borda_superior.nao_combustiveis + borda_inferior.nao_combustiveis;
    estatisticas_locais[EST_INTACTAS]         = miolo.intactas + borda_superior.intactas + borda_inferior.intactas;
    estatisticas_locais[EST_QUEIMADAS]        = miolo.queimadas + borda_superior.queimadas + borda_inferior.queimadas;
    estatisticas_locais[EST_CONTENCAO]        = miolo.contencao + borda_superior.contencao + borda_inferior.contencao;
}

/* Troca os vetores atuais pelos de próximo passo (só os ponteiros, sem copiar células) */
void trocar_buffers(CELULAS *celulas) {
    int *estado_temporario  = celulas->estado_atual;
    celulas->estado_atual   = celulas->proximo_estado;
    celulas->proximo_estado = estado_temporario;

    int *tempo_temporario  = celulas->tempo_atual;
    celulas->tempo_atual   = celulas->proximo_tempo;
    celulas->proximo_tempo = tempo_temporario;
}

/* Atualiza os contadores, o total de ignições e o pico com os totais globais do passo */
void atualizar_contadores(COUNTERS *cnt, PICO *pico, const int *estatisticas_globais, int passo_atual) {
    cnt->em_chamas        = estatisticas_globais[EST_EM_CHAMAS];
    cnt->nao_combustiveis = estatisticas_globais[EST_NAO_COMBUSTIVEIS];
    cnt->intactas         = estatisticas_globais[EST_INTACTAS];
    cnt->queimadas        = estatisticas_globais[EST_QUEIMADAS];
    cnt->contencao        = estatisticas_globais[EST_CONTENCAO];

    cnt->total_ignicoes += estatisticas_globais[EST_IGNICOES];
    if (estatisticas_globais[EST_IGNICOES] > pico->quantidade) {
        pico->quantidade = estatisticas_globais[EST_IGNICOES];
        pico->passo      = passo_atual;
    }
}

// Laço de simulação
/* Executa os passos de simulação no bloco do processo e devolve quantos passos foram executados */
int simular(CELULAS *celulas, const DECOMPOSICAO *dec, const VIZINHANCA *vizinhanca, const int *config, COUNTERS *cnt, PICO *pico) {
    /* Sem const de propósito, porque o tratamento de variável const em default(none) muda entre versões do GCC */
    int C             = config[CFG_C];
    int P             = config[CFG_P];
    int T             = config[CFG_T];
    int LIMIAR        = config[CFG_LIMIAR];
    int linhas_locais = dec->linhas_locais;

    const long long *deslocamento_offset = vizinhanca->deslocamento_offset;
    const int *pesos_direcao             = vizinhanca->pesos_direcao;

    /* Intervalo de índices das células reais do bloco local (pula a linha fantasma superior e para antes da inferior) */
    long long primeira_celula_real = C;
    long long fim_celulas_reais    = (long long)(linhas_locais + 1) * C;

    int passo_atual = 0;

    // Acumuladores de reduction do miolo do bloco; zerados após entrarem na redução global a cada passo.
    int proximo_celulas_em_chamas = 0;
    int ignicoes_no_passo         = 0;
    int proximo_nao_combustiveis  = 0;
    int proximo_intactas          = 0;
    int proximo_queimadas         = 0;
    int proximo_contencao         = 0;

    // Estatísticas das duas linhas de fronteira do bloco, cada uma preenchida por uma única tarefa a cada passo.
    ESTATISTICAS borda_superior = {0};
    ESTATISTICAS borda_inferior = {0};

    // Região Paralela Persistente
    /* Região paralela persistente mantida ativa durante toda a simulação pra evitar overhead de criação de threads
    As chamadas MPI de dentro dela ficam sempre em omp master */
    #pragma omp parallel num_threads(T) default(none) \
    shared(C, P, LIMIAR, linhas_locais, primeira_celula_real, fim_celulas_reais, celulas, dec, cnt, pico, passo_atual, \
               deslocamento_offset, pesos_direcao, proximo_celulas_em_chamas, ignicoes_no_passo, proximo_nao_combustiveis, \
               proximo_intactas, proximo_queimadas, proximo_contencao, borda_superior, borda_inferior)
    {
        while (passo_atual < P && cnt->em_chamas > 0) {
            /* Ponteiros do passo, lidos depois da troca de buffers do passo anterior */
            int *estado_atual   = celulas->estado_atual;
            const int *ativacao = celulas->ativacao;

            // Ativação das contenções
            /* Ativa as zonas de contenção agendadas para o passo_atual em paralelo, só nas células reais do bloco
            O corpo do loop não tem if e usa `&` no lugar de `&&` pra permitir a vetorização: sempre escreve, escolhendo
            entre o novo estado e o estado atual */
            #pragma omp for simd schedule(SCHED)
            for (long long i = primeira_celula_real; i < fim_celulas_reais; i++) {
                int deve_ativar = (ativacao[i] == passo_atual) & (estado_atual[i] == ESTADO_INTACTA);
                estado_atual[i] = deve_ativar ? ESTADO_CONTENCAO : estado_atual[i];
            }

            // Troca de fronteiras e tarefas de borda
            /* A thread principal troca as fronteiras enquanto as outras já calculam o miolo, que não depende das linhas fantasmas
            Quando as linhas fantasmas chegam, cada linha de fronteira vira uma tarefa, executada por qualquer thread livre */
            #pragma omp master
            {
                trocar_fronteiras(celulas->estado_atual, dec, C);

                #pragma omp task default(none) shared(borda_superior, celulas, C, LIMIAR, deslocamento_offset, pesos_direcao)
                borda_superior = atualizar_linha(celulas, 1, C, LIMIAR, deslocamento_offset, pesos_direcao);

                /* Com uma linha só no bloco, a primeira tarefa já resolve */
                if (linhas_locais > 1) {
                    #pragma omp task default(none) shared(borda_inferior, celulas, C, LIMIAR, linhas_locais, deslocamento_offset, pesos_direcao)
                    borda_inferior = atualizar_linha(celulas, linhas_locais, C, LIMIAR, deslocamento_offset, pesos_direcao);
                }
            }

            // Próximo estado e estatísticas
            /* Varre as linhas do miolo do bloco em paralelo para atualizar o estado das células e contabilizar estatísticas do próximo passo
            A barreira no fim do laço também espera as duas tarefas de fronteira terminarem */
            #pragma omp for schedule(SCHED) reduction(+ : proximo_celulas_em_chamas, ignicoes_no_passo, proximo_nao_combustiveis,         \
                                                           proximo_intactas, proximo_queimadas, proximo_contencao)
            for (int l = 2; l < linhas_locais; l++) {
                ESTATISTICAS linha = atualizar_linha(celulas, l, C, LIMIAR, deslocamento_offset, pesos_direcao);

                proximo_celulas_em_chamas += linha.em_chamas;
                ignicoes_no_passo += linha.ignicoes;
                proximo_nao_combustiveis += linha.nao_combustiveis;
                proximo_intactas += linha.intactas;
                proximo_queimadas += linha.queimadas;
                proximo_contencao += linha.contencao;
            }

            // Redução global, troca de buffers e atualização de pico
            /* Trecho executado só pela thread principal: soma as estatísticas com as dos outros processos, realiza a troca de matrizes
            (double buffering), atualiza os contadores e o pico de ignições */
            #pragma omp master
            {
                ESTATISTICAS miolo = {
                    .em_chamas        = proximo_celulas_em_chamas,
                    .ignicoes         = ignicoes_no_passo,
                    .nao_combustiveis = proximo_nao_combustiveis,
                    .intactas         = proximo_intactas,
                    .queimadas        = proximo_queimadas,
                    .contencao        = proximo_contencao
                };

                int estatisticas_locais[EST_TAMANHO];
                int estatisticas_globais[EST_TAMANHO];

                empacotar_estatisticas(estatisticas_locais, miolo, borda_superior, borda_inferior);
                somar_estatisticas(estatisticas_locais, estatisticas_globais, dec->comm_grade);

                trocar_buffers(celulas);
                atualizar_contadores(cnt, pico, estatisticas_globais, passo_atual);

                proximo_celulas_em_chamas = 0;
                ignicoes_no_passo         = 0;
                proximo_nao_combustiveis  = 0;
                proximo_intactas          = 0;
                proximo_queimadas         = 0;
                proximo_contencao         = 0;
                borda_superior            = (ESTATISTICAS){0};
                borda_inferior            = (ESTATISTICAS){0};

                passo_atual++;
            }

            /* O omp master não tem barreira implícita, então esta segura as outras threads até o passo estar fechado */
            #pragma omp barrier
        }
    }

    return passo_atual;
}

// Saída
/* Calcula o checksum na ordem das células, igual à versão sequencial */
unsigned long long calcular_checksum(const CELULAS *globais, long long total_celulas) {
    unsigned long long checksum = 0;

    for (long long i = 0; i < total_celulas; i++) {
        checksum = checksum * 31ULL + (unsigned long long)globais->estado_atual[i];
        checksum = checksum * 31ULL + (unsigned long long)globais->tempo_atual[i];
    }

    return checksum;
}

/* Imprime os 12 campos do resultado final, no formato pedido */
void print_data(COUNTERS cnt, int passo_atual, PICO pico, double pct_queimado, double pct_protegido, unsigned long long checksum,
                double tempo) {
    printf("passos: %d\n", passo_atual);
    printf("nao_combustiveis: %d\n", cnt.nao_combustiveis);
    printf("intactas: %d\n", cnt.intactas);
    printf("em_chamas: %d\n", cnt.em_chamas);
    printf("queimadas: %d\n", cnt.queimadas);
    printf("contencao: %d\n", cnt.contencao);
    printf("total_ignicoes: %d\n", cnt.total_ignicoes);
    printf("pico_ignicoes: %d %d\n", pico.passo, pico.quantidade);
    printf("percentual_queimado: %.2f\n", pct_queimado);
    printf("percentual_protegido: %.2f\n", pct_protegido);
    printf("checksum: %llu\n", checksum);
    printf("tempo: %.6f\n", tempo);
}

/* Calcula o checksum e os percentuais e imprime o resultado final (só a raiz chama) */
void imprimir_resultado(const CELULAS *globais, long long total_celulas, COUNTERS cnt, int passos, PICO pico, double tempo) {
    unsigned long long checksum = calcular_checksum(globais, total_celulas);

    double pct_queimado  = percentual_queimado(cnt.queimadas, cnt.em_chamas, cnt.combustiveis_iniciais);
    double pct_protegido = percentual_protegido(cnt.contencao, cnt.combustiveis_iniciais);

    print_data(cnt, passos, pico, pct_queimado, pct_protegido, checksum, tempo);
}

// Programa principal
int main(int argc, char *argv[]) {
    /* Só a thread principal de cada processo faz chamadas MPI (sempre dentro de omp master), então o nível FUNNELED é suficiente */
    int suporte_threads;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &suporte_threads);

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int config[CFG_TAMANHO] = {0};
    CELULAS globais         = {0};

    // Preparação na raiz e configuração
    /* Se a raiz encontrou algum erro na entrada, todos os processos encerram aqui */
    if (!compartilhar_configuracao(argc, argv, suporte_threads, rank, &globais, config)) {
        liberar_cels(&globais);
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    int L = config[CFG_L];
    int C = config[CFG_C];

    // Decomposição de domínio
    /* Um processo que ficou sem linhas (mais processos do que linhas) encerra aqui */
    DECOMPOSICAO dec;
    if (!decompor_dominio(&dec, L, C)) {
        liberar_decomposicao(&dec);
        MPI_Finalize();
        return EXIT_SUCCESS;
    }

    // Distribuição da matriz
    CELULAS celulas;
    if (!alocar_cels_locais(&celulas, dec.linhas_locais, C)) {
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    distribuir_grade(&globais, &celulas, &dec, C);

    /* As contagens iniciais são globais e chegaram pelo broadcast, então todos os processos começam com os mesmos contadores */
    COUNTERS cnt = {
        .combustiveis_iniciais = config[CFG_COMBUSTIVEIS_INICIAIS],
        .nao_combustiveis      = config[CFG_NAO_COMBUSTIVEIS],
        .intactas              = config[CFG_INTACTAS],
        .em_chamas             = config[CFG_EM_CHAMAS],
        .queimadas             = config[CFG_QUEIMADAS],
        .contencao             = config[CFG_CONTENCAO],
        .total_ignicoes        = 0
    };
    PICO pico = {-1, 0};

    VIZINHANCA vizinhanca = calcular_vizinhanca(config);

    // Simulação
    /* Garante que todos os processos terminaram a preparação antes de marcar o tempo inicial da simulação
    O tempo cobre só os passos de simulação, sem a distribuição e a junção da matriz */
    MPI_Barrier(dec.comm_grade);
    double t_inicio = omp_get_wtime();

    int passos = simular(&celulas, &dec, &vizinhanca, config, &cnt, &pico);

    /* Calcula o tempo total gasto na simulação */
    double tempo_execucao = omp_get_wtime() - t_inicio;

    // Junção do resultado e saída
    juntar_grade(&celulas, &globais, &dec, C);

    if (rank == PROC_RAIZ) {
        imprimir_resultado(&globais, (long long)L * C, cnt, passos, pico, tempo_execucao);
    }

    liberar_cels(&celulas);
    liberar_cels(&globais);
    liberar_decomposicao(&dec);
    MPI_Finalize();

    return EXIT_SUCCESS;
}