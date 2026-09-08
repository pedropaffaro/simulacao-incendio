// #define _POSIX_C_SOURCE 200112L
typedef struct {
    int vertical;
    int horizontal;
} DIRECTION;

// clang-format off
#define DIRECAO_NORTE    (DIRECTION){-1,  0}
#define DIRECAO_NORDESTE (DIRECTION){-1,  1}
#define DIRECAO_LESTE    (DIRECTION){ 0,  1}
#define DIRECAO_SUDESTE  (DIRECTION){ 1,  1}
#define DIRECAO_SUL      (DIRECTION){ 1,  0}
#define DIRECAO_SUDOESTE (DIRECTION){ 1, -1}
#define DIRECAO_OESTE    (DIRECTION){ 0, -1}
#define DIRECAO_NOROESTE (DIRECTION){-1, -1}

static const DIRECTION VIZINHOS[8] = {
    DIRECAO_NORTE,    DIRECAO_NORDESTE,
    DIRECAO_LESTE,    DIRECAO_SUDESTE,
    DIRECAO_SUL,      DIRECAO_SUDOESTE,
    DIRECAO_OESTE,    DIRECAO_NOROESTE
};
// clang-format on

typedef enum {
    COBERTURA_CODIGO_AGUA = 0,
    COBERTURA_CODIGO_SOLO,
    COBERTURA_CODIGO_VEGETACAO,
    COBERTURA_CODIGO_FLORESTA
} COBERTURA_CODIGO;

/* Geração da cobertura (rand % 100) */
// clang-format off
#define COBERTURA_MAX_AGUA     9  // 0–9   (10%)
#define COBERTURA_MAX_SOLO     19 // 10–19 (10%)
#define COBERTURA_MAX_RASTEIRA 54 // 20–54 (35%)
#define COBERTURA_MAX_FLORESTA 99 // 55–99 (45%)
// clang-format on

/* Fatores de combustível por cobertura */
#define FATOR_AGUA 0
#define FATOR_SOLO 0
#define FATOR_RASTEIRA 8
#define FATOR_FLORESTA 12

typedef enum {
    ESTADO_NAO_COMBUSTIVEL = 0,
    ESTADO_INTACTA,
    ESTADO_EM_CHAMAS,
    ESTADO_QUEIMADA,
    ESTADO_CONTENCAO
} ESTADO_CODIGO;

/* Tempos iniciais de queima (em passos) */
#define TEMPO_QUEIMA_RASTEIRA 2
#define TEMPO_QUEIMA_FLORESTA 4

/* Marcador de célula sem zona de contenção */
#define CELULA_SEM_CONTENCAO -1

/* Pesos base dos vizinhos de Moore */
#define PESO_ORTOGONAL 10
#define PESO_DIAGONAL 7

/* Limites de intensidade do vento */
#define INTENSIDADE_MIN 0
#define INTENSIDADE_MAX 5

typedef enum {
    ALINHAMENTO_DIAGONAL_CONTRARIO = -2,
    ALINHAMENTO_DESFAVORIDO,
    ALINHAMENTO_NEUTRO,
    ALINHAMENTO_FAVORAVEL,
    ALINHAMENTO_DIAGONAL_FAVORAVEL
} ALINHAMENTO_VENTO;

#define ERRO -1

typedef struct {
    int *cobertura;
    int *umidade;
    int *estado_atual;
    int *tempo_atual;
    int *proximo_estado;
    int *proximo_tempo;
    int *ativacao;
} CELULAS;

typedef enum {
    LEITURA_OK = 0,
    LEITURA_ERRO_SISTEMA,
    LEITURA_ERRO_ENTRADA
} LEITURA_STATUS;

typedef struct {
    int passo;
    int quantidade;
} PICO;

typedef struct {
    int combustiveis_iniciais;
    int nao_combustiveis;
    int intactas;
    int em_chamas;
    int queimadas;
    int contencao;
    int total_ignicoes;
} COUNTERS;

typedef struct {
    int linha;
    int coluna;
} COORDENADA;

#include <omp.h>
#include <stdio.h>
#include <stdlib.h>


COORDENADA get_coordenada(long long idx, int C) {
    COORDENADA coord;
    coord.linha  = (int)(idx / C);
    coord.coluna = (int)(idx % C);
    return coord;
}

long long int get_idx(COORDENADA coord, int C) {
    return (long long)coord.linha * C + coord.coluna;
}

ESTADO_CODIGO estado_apos_ativacao(ESTADO_CODIGO estado) {
    switch (estado) {
        case ESTADO_INTACTA:
            return ESTADO_CONTENCAO;
        default:
            return estado;
    }
}

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

int peso_vizinho(int prop_linha, int prop_coluna, int vento_linha, int vento_coluna, int intensidade) {

    int p_basico = (abs(prop_linha) + abs(prop_coluna) == 1) ? PESO_ORTOGONAL : PESO_DIAGONAL;

    int A = prop_linha * vento_linha + prop_coluna * vento_coluna;

    int peso_vizinho = p_basico + intensidade * A;

    return peso_vizinho < 1 ? 1 : peso_vizinho;
}

int potencial_ignicao(int S, int fator_combustivel, int umidade) {
    // Não tem porque trabalhar com ponto flutuante para <=
    return (S * fator_combustivel * (100 - umidade)) / 100;
}

float percentual_queimado(int queimadas, int em_chamas, int combustiveis_iniciais) {
    if (combustiveis_iniciais == 0)
        return 0.0;
    return (100.0 * (queimadas + em_chamas)) / combustiveis_iniciais;
}

float percentual_protegido(int contencoes, int combustiveis_iniciais) {
    if (combustiveis_iniciais == 0)
        return 0.0;
    return (100.0 * contencoes) / combustiveis_iniciais;
}

int alocar_cels(CELULAS *g, long long total_celulas) {
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
    return 1;
}

void liberar_cels(CELULAS *g) {
    free(g->cobertura);
    free(g->umidade);
    free(g->estado_atual);
    free(g->tempo_atual);
    free(g->proximo_estado);
    free(g->proximo_tempo);
    free(g->ativacao);
}

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

LEITURA_STATUS validar_argc(int argc, const char *argv0) {
    if (argc != 2) {
        fprintf(stderr, "[Erro] Uso correto: %s <arquivo_de_entrada>\n", argv0);
        return LEITURA_ERRO_SISTEMA;
    }
    return LEITURA_OK;
}

FILE *abrir_arquivo(const char *caminho) {
    FILE *f = fopen(caminho, "r");
    if (f == NULL)
        fprintf(stderr, "[Erro] Nao foi possivel abrir o arquivo de entrada '%s'.\n", caminho);
    return f;
}

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

LEITURA_STATUS ler_config_vento(FILE *input, int *vento_linha, int *vento_coluna, int *vento_intensidade) {
    if (fscanf(input, "%d %d %d", vento_linha, vento_coluna, vento_intensidade) != 3) {
        fprintf(stderr, "[Erro] Não foi possível ler a configuração do vento.\n");
        return LEITURA_ERRO_SISTEMA;
    }
    if (*vento_linha < -1 || *vento_linha > 1 || *vento_coluna < -1 || *vento_coluna > 1 || (*vento_linha == 0 && *vento_coluna == 0) ||
        *vento_intensidade < INTENSIDADE_MIN || *vento_intensidade > INTENSIDADE_MAX) {
        printf("[Erro] Os valores inseridos para configuração do vento são inválidos.\n");
        return LEITURA_ERRO_ENTRADA;
    }
    return LEITURA_OK;
}

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

LEITURA_STATUS ler_focos(FILE *input, int F, int L, int C, int *cobertura, int *estado_atual, int *tempo_atual) {

    for (int k = 0; k < F; k++) {
        int linha, coluna;
        if (fscanf(input, "%d %d", &linha, &coluna) != 2) {
            fprintf(stderr, "[Erro] Não foi possível ler o foco inicial %d.\n", k + 1);
            return LEITURA_ERRO_SISTEMA;
        }
        if (linha < 0 || linha >= L || coluna < 0 || coluna >= C) {
            printf("[Erro] Os valores inseridos para os limites do foco inicial são inválidos.\n");
            return LEITURA_ERRO_ENTRADA;
        }
        long long idx = (long long)linha * C + coluna;
        if (estado_atual[idx] == ESTADO_EM_CHAMAS) {
            printf("[Erro] Os valores inseridos do foco inicial são inválidos (foco repetido).\n");
            return LEITURA_ERRO_ENTRADA;
        }
        if (cobertura[idx] == COBERTURA_CODIGO_AGUA || cobertura[idx] == COBERTURA_CODIGO_SOLO) {
            printf("[Erro] Os valores inseridos do foco inicial são inválidos.\n");
            return LEITURA_ERRO_ENTRADA;
        }
        estado_atual[idx] = ESTADO_EM_CHAMAS;
        tempo_atual[idx]  = tempo_queima_inicial(cobertura[idx]);
    }
    return LEITURA_OK;
}

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
            printf("[Erro] Os valores inseridos para contenção são inválidos.\n");
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

void print_data(COUNTERS cnt, int passo_atual, PICO pico, float pct_queimado, float pct_protegido, unsigned long long checksum,
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

int main(int argc, char *argv[]) {
    if (validar_argc(argc, argv[0]) != LEITURA_OK)
        return EXIT_FAILURE;

    FILE *input = abrir_arquivo(argv[argc - 1]);
    if (input == NULL)
        return EXIT_FAILURE;

    int L, C, P, T, LIMIAR;
    unsigned int seed;
    LEITURA_STATUS status;

    status = ler_config_geral(input, &L, &C, &P, &T, &seed, &LIMIAR);
    if (status != LEITURA_OK) {
        fclose(input);
        return status == LEITURA_ERRO_SISTEMA ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    int vento_linha, vento_coluna, vento_intensidade;
    status = ler_config_vento(input, &vento_linha, &vento_coluna, &vento_intensidade);
    if (status != LEITURA_OK) {
        fclose(input);
        return status == LEITURA_ERRO_SISTEMA ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    int F, num_zonas;
    status = ler_contagem_focos_zonas(input, &F, &num_zonas);
    if (status != LEITURA_OK) {
        fclose(input);
        return status == LEITURA_ERRO_SISTEMA ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    long long total_celulas = (long long)L * C;
    CELULAS celulas;
    if (!alocar_cels(&celulas, total_celulas)) {
        fclose(input);
        liberar_cels(&celulas);
        return EXIT_FAILURE;
    }

    gerar_terreno(&celulas, total_celulas, seed);

    status = ler_focos(input, F, L, C, celulas.cobertura, celulas.estado_atual, celulas.tempo_atual);
    if (status != LEITURA_OK) {
        fclose(input);
        liberar_cels(&celulas);
        return status == LEITURA_ERRO_SISTEMA ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    status = ler_zonas_contencao(input, num_zonas, L, C, P, celulas.ativacao);
    if (status != LEITURA_OK) {
        fclose(input);
        liberar_cels(&celulas);
        return status == LEITURA_ERRO_SISTEMA ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    fclose(input);

    COUNTERS cnt = {0};

    for (long long i = 0; i < total_celulas; i++) {
        if (celulas.cobertura[i] == COBERTURA_CODIGO_VEGETACAO || celulas.cobertura[i] == COBERTURA_CODIGO_FLORESTA)
            cnt.combustiveis_iniciais++;
        switch (celulas.estado_atual[i]) {
            case ESTADO_NAO_COMBUSTIVEL:
                cnt.nao_combustiveis++;
                break;
            case ESTADO_INTACTA:
                cnt.intactas++;
                break;
            case ESTADO_EM_CHAMAS:
                cnt.em_chamas++;
                break;
            case ESTADO_QUEIMADA:
                cnt.queimadas++;
                break;
            case ESTADO_CONTENCAO:
                cnt.contencao++;
                break;
        }
    }

    double t_inicio = omp_get_wtime();

    int passo_atual  = 0;
    PICO pico        = {-1, 0};
    COORDENADA coord = {0, 0};

    while (passo_atual < P && cnt.em_chamas > 0) {

        // Ativar zonas programadas para passo_atual
        for (long long i = 0; i < total_celulas; i++) {
            if (celulas.ativacao[i] == passo_atual)
                celulas.estado_atual[i] = estado_apos_ativacao(celulas.estado_atual[i]);
        }

        // Calcular próximo estado de todas as células
        // Calcular estatísticas do próximo estado
        int novos_incendios = 0;
        int next_nao_comb = 0, next_intactas = 0, next_em_chamas = 0;
        int next_queimadas = 0, next_contencao = 0;

        for (long long i = 0; i < total_celulas; i++) {
            coord = get_coordenada(i, C);

            switch (celulas.estado_atual[i]) {

                case ESTADO_NAO_COMBUSTIVEL: {
                    celulas.proximo_estado[i] = ESTADO_NAO_COMBUSTIVEL;
                    celulas.proximo_tempo[i]  = 0;
                    next_nao_comb++;
                    break;
                }

                case ESTADO_INTACTA: {

                    int S = 0;
                    COORDENADA vizinho_coord;

                    for (int viz = 0; viz < 8; viz++) {
                        vizinho_coord = (COORDENADA){coord.linha + VIZINHOS[viz].vertical, coord.coluna + VIZINHOS[viz].horizontal};
                        if (vizinho_coord.linha < 0 || vizinho_coord.linha >= L || vizinho_coord.coluna < 0 || vizinho_coord.coluna >= C)
                            continue;

                        long long viz_idx = get_idx(vizinho_coord, C);

                        if (celulas.estado_atual[viz_idx] != ESTADO_EM_CHAMAS)
                            continue;

                        int prop_linha  = coord.linha - vizinho_coord.linha;
                        int prop_coluna = coord.coluna - vizinho_coord.coluna;
                        S += peso_vizinho(prop_linha, prop_coluna, vento_linha, vento_coluna, vento_intensidade);
                    }

                    int I = potencial_ignicao(S, fator_cobertura(celulas.cobertura[i]), celulas.umidade[i]);

                    if (I >= LIMIAR) {
                        celulas.proximo_estado[i] = ESTADO_EM_CHAMAS;
                        celulas.proximo_tempo[i]  = tempo_queima_inicial(celulas.cobertura[i]);
                        novos_incendios++;
                        next_em_chamas++;
                    } else {
                        celulas.proximo_estado[i] = ESTADO_INTACTA;
                        celulas.proximo_tempo[i]  = 0;
                        next_intactas++;
                    }
                    break;
                }

                case ESTADO_EM_CHAMAS: {
                    int novo_tempo = celulas.tempo_atual[i] - 1;
                    if (novo_tempo == 0) {
                        celulas.proximo_estado[i] = ESTADO_QUEIMADA;
                        celulas.proximo_tempo[i]  = 0;
                        next_queimadas++;
                    } else {
                        celulas.proximo_estado[i] = ESTADO_EM_CHAMAS;
                        celulas.proximo_tempo[i]  = novo_tempo;
                        next_em_chamas++;
                    }
                    break;
                }

                case ESTADO_QUEIMADA:
                    celulas.proximo_estado[i] = ESTADO_QUEIMADA;
                    celulas.proximo_tempo[i]  = 0;
                    next_queimadas++;
                    break;

                case ESTADO_CONTENCAO:
                    celulas.proximo_estado[i] = ESTADO_CONTENCAO;
                    celulas.proximo_tempo[i]  = 0;
                    next_contencao++;
                    break;
            }
        }

        if (novos_incendios > pico.quantidade) {
            pico.quantidade = novos_incendios;
            pico.passo      = passo_atual;
        }

        cnt.total_ignicoes += novos_incendios;

        cnt.nao_combustiveis = next_nao_comb;
        cnt.intactas         = next_intactas;
        cnt.em_chamas        = next_em_chamas;
        cnt.queimadas        = next_queimadas;
        cnt.contencao        = next_contencao;

        // Trocar matrizes
        int *tmp;
        tmp                    = celulas.estado_atual;
        celulas.estado_atual   = celulas.proximo_estado;
        celulas.proximo_estado = tmp;
        tmp                    = celulas.tempo_atual;
        celulas.tempo_atual    = celulas.proximo_tempo;
        celulas.proximo_tempo  = tmp;

        // Atualizar condição de parada
        cnt.em_chamas = next_em_chamas;
        passo_atual++;
    }

    double tempo = omp_get_wtime() - t_inicio;

    // Checksum
    unsigned long long checksum = 0;
    for (long long i = 0; i < total_celulas; i++) {
        checksum = checksum * 31ULL + (unsigned long long)celulas.estado_atual[i];
        checksum = checksum * 31ULL + (unsigned long long)celulas.tempo_atual[i];
    }

    // Percentuais
    float pct_queimado  = percentual_queimado(cnt.queimadas, cnt.em_chamas, cnt.combustiveis_iniciais);
    float pct_protegido = percentual_protegido(cnt.contencao, cnt.combustiveis_iniciais);

    liberar_cels(&celulas);

    print_data(cnt, passo_atual, pico, pct_queimado, pct_protegido, checksum, tempo);

    return EXIT_SUCCESS;
}
