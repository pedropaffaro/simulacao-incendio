#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

typedef struct {
    int vertical;
    int horizontal;
} DIRECAO;

// clang-format off
#define DIRECAO_NORTE    (DIRECAO){-1,  0}
#define DIRECAO_NORDESTE (DIRECAO){-1,  1}
#define DIRECAO_LESTE    (DIRECAO){ 0,  1}
#define DIRECAO_SUDESTE  (DIRECAO){ 1,  1}
#define DIRECAO_SUL      (DIRECAO){ 1,  0}
#define DIRECAO_SUDOESTE (DIRECAO){ 1, -1}
#define DIRECAO_OESTE    (DIRECAO){ 0, -1}
#define DIRECAO_NOROESTE (DIRECAO){-1, -1}

static const DIRECAO VIZINHOS[8] = {
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
} GRADE;

typedef enum {
    LEITURA_OK = 0,
    LEITURA_ERRO_SISTEMA,
    LEITURA_ERRO_ENTRADA
} LEITURA_STATUS;

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

int alocar_grade(GRADE *g, long long total_celulas) {
    *g = (GRADE){0};
    g->cobertura      = malloc(total_celulas * sizeof(int));
    g->umidade        = malloc(total_celulas * sizeof(int));
    g->estado_atual   = malloc(total_celulas * sizeof(int));
    g->tempo_atual    = malloc(total_celulas * sizeof(int));
    g->proximo_estado = malloc(total_celulas * sizeof(int));
    g->proximo_tempo  = malloc(total_celulas * sizeof(int));
    g->ativacao       = malloc(total_celulas * sizeof(int));
    if (!g->cobertura || !g->umidade || !g->estado_atual || !g->tempo_atual ||
        !g->proximo_estado || !g->proximo_tempo || !g->ativacao) {
        fprintf(stderr, "[Erro] Não foi possível alocar memoria para as estruturas da matriz.\n");
        return 0;
    }
    return 1;
}

void liberar_grade(GRADE *g) {
    free(g->cobertura);
    free(g->umidade);
    free(g->estado_atual);
    free(g->tempo_atual);
    free(g->proximo_estado);
    free(g->proximo_tempo);
    free(g->ativacao);
}

void gerar_terreno(GRADE *g, long long total_celulas, unsigned int seed) {
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
    if (*vento_linha < -1 || *vento_linha > 1 ||
        *vento_coluna < -1 || *vento_coluna > 1 ||
        (*vento_linha == 0 && *vento_coluna == 0) ||
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
        tempo_atual[idx] = tempo_queima_inicial(cobertura[idx]);
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
        if (passo_ativacao < 0 || passo_ativacao >= P ||
            linha_inicial < 0 || linha_inicial >= L ||
            coluna_inicial < 0 || coluna_inicial >= C ||
            linha_final < 0 || linha_final >= L ||
            coluna_final < 0 || coluna_final >= C ||
            linha_inicial > linha_final ||
            coluna_inicial > coluna_final) {
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

int main(int argc, char *argv[]) {
    if (validar_argc(argc, argv[0]) != LEITURA_OK) return 1;

    FILE *input = abrir_arquivo(argv[1]);
    if (input == NULL) return 1;

    int L, C, P, T, LIMIAR;
    unsigned int seed;
    LEITURA_STATUS stats;

    stats = ler_config_geral(input, &L, &C, &P, &T, &seed, &LIMIAR);
    if (stats != LEITURA_OK) { fclose(input); return stats == LEITURA_ERRO_SISTEMA ? 1 : 0; }

    int vento_linha, vento_coluna, vento_intensidade;
    stats = ler_config_vento(input, &vento_linha, &vento_coluna, &vento_intensidade);
    if (stats != LEITURA_OK) { fclose(input); return stats == LEITURA_ERRO_SISTEMA ? 1 : 0; }

    int F, num_zonas_contencao;
    stats = ler_contagem_focos_zonas(input, &F, &num_zonas_contencao);
    if (stats != LEITURA_OK) { fclose(input); return stats == LEITURA_ERRO_SISTEMA ? 1 : 0; }

    long long total_celulas = (long long)L * C;
    GRADE grade;
    if (!alocar_grade(&grade, total_celulas)) {
        fclose(input);
        liberar_grade(&grade);
        return 1;
    }

    gerar_terreno(&grade, total_celulas, seed);

    stats = ler_focos(input, F, L, C, grade.cobertura, grade.estado_atual, grade.tempo_atual);
    if (stats != LEITURA_OK) {
        fclose(input);
        liberar_grade(&grade);
        return stats == LEITURA_ERRO_SISTEMA ? 1 : 0;
    }

    stats = ler_zonas_contencao(input, num_zonas_contencao, L, C, P, grade.ativacao);
    if (stats != LEITURA_OK) {
        fclose(input);
        liberar_grade(&grade);
        return stats == LEITURA_ERRO_SISTEMA ? 1 : 0;
    }

    fclose(input);

    // --- INÍCIO DA MEDIÇÃO DE TEMPO DA SIMULAÇÃO ---
    double t_inicio = omp_get_wtime();

    int total_combustiveis = 0;
    int celulas_em_chamas = 0;
    
    #pragma omp parallel for num_threads(T) schedule(static) reduction(+:total_combustiveis, celulas_em_chamas)
    for (long long i = 0; i < total_celulas; i++) {
        COBERTURA_CODIGO cob = (COBERTURA_CODIGO)grade.cobertura[i];
        if (cob == COBERTURA_CODIGO_VEGETACAO || cob == COBERTURA_CODIGO_FLORESTA) {
            total_combustiveis++;
        }
        if (grade.estado_atual[i] == ESTADO_EM_CHAMAS) {
            celulas_em_chamas++;
        }
    }

    long long deslocamento_offset[8];
    int pesos_direcao[8];

    for (int k = 0; k < 8; k++) {
        int variacao_linha = VIZINHOS[k].vertical;
        int variacao_coluna = VIZINHOS[k].horizontal;

        deslocamento_offset[k] = (long long)variacao_linha * C + variacao_coluna;
        pesos_direcao[k] = peso_vizinho(-variacao_linha, -variacao_coluna, vento_linha, vento_coluna, vento_intensidade);
    }

    int passo_atual = 0;
    int proximo_celulas_em_chamas = 0;
    int ignicoes_no_passo = 0;
    int total_ignicoes = 0;
    int pico_passo = 0;
    int pico_qtd = 0;

    // Região Paralela Persistente
    #pragma omp parallel num_threads(T) default(none) \
        shared(L, C, P, total_celulas, grade, LIMIAR, passo_atual, \
               celulas_em_chamas, proximo_celulas_em_chamas, \
               deslocamento_offset, pesos_direcao, VIZINHOS, \
               ignicoes_no_passo, total_ignicoes, pico_passo, pico_qtd)
    {
        while (passo_atual < P && celulas_em_chamas > 0) {

            // 1. Ativação das contenções
            #pragma omp for schedule(static)
            for (long long i = 0; i < total_celulas; i++) {
                if (grade.ativacao[i] == passo_atual && grade.estado_atual[i] == ESTADO_INTACTA) {
                    grade.estado_atual[i] = ESTADO_CONTENCAO;
                }
            }

            // 2. Simulação, contagem de chamas e novas ignições
            #pragma omp for collapse(2) schedule(static) reduction(+:proximo_celulas_em_chamas, ignicoes_no_passo)
            for (int l = 0; l < L; l++) {
                for (int c = 0; c < C; c++) {
                    long long i = (long long)l * C + c;
                    ESTADO_CODIGO estado_celula = (ESTADO_CODIGO)grade.estado_atual[i];

                    if (estado_celula == ESTADO_INTACTA) {
                        int S = 0;

                        if (l > 0 && l < L - 1 && c > 0 && c < C - 1) {
                            for (int k = 0; k < 8; k++) {
                                if (grade.estado_atual[i + deslocamento_offset[k]] == ESTADO_EM_CHAMAS) {
                                    S += pesos_direcao[k];
                                }
                            }
                        } else {
                            for (int k = 0; k < 8; k++) {
                                int linha_vizinho  = l + VIZINHOS[k].vertical;
                                int coluna_vizinho = c + VIZINHOS[k].horizontal;

                                if (linha_vizinho >= 0 && linha_vizinho < L && coluna_vizinho >= 0 && coluna_vizinho < C) {
                                    long long vizinho = (long long)linha_vizinho * C + coluna_vizinho;
                                    if (grade.estado_atual[vizinho] == ESTADO_EM_CHAMAS) {
                                        S += pesos_direcao[k];
                                    }
                                }
                            }
                        }

                        if (S > 0 && potencial_ignicao(S, fator_cobertura((COBERTURA_CODIGO)grade.cobertura[i]), grade.umidade[i]) >= LIMIAR) {
                            grade.proximo_estado[i] = ESTADO_EM_CHAMAS;
                            grade.proximo_tempo[i]  = tempo_queima_inicial((COBERTURA_CODIGO)grade.cobertura[i]);
                            proximo_celulas_em_chamas++;
                            ignicoes_no_passo++;
                        } else {
                            grade.proximo_estado[i] = ESTADO_INTACTA;
                            grade.proximo_tempo[i]  = 0;
                        }

                    } else if (estado_celula == ESTADO_EM_CHAMAS) {
                        int tempo_ignicao_restante = grade.tempo_atual[i] - 1;

                        if (tempo_ignicao_restante == 0) {
                            grade.proximo_estado[i] = ESTADO_QUEIMADA;
                            grade.proximo_tempo[i]  = 0;
                        } else {
                            grade.proximo_estado[i] = ESTADO_EM_CHAMAS;
                            grade.proximo_tempo[i]  = tempo_ignicao_restante;
                            proximo_celulas_em_chamas++;
                        }

                    } else {
                        grade.proximo_estado[i] = estado_celula;
                        grade.proximo_tempo[i]  = grade.tempo_atual[i];
                    }
                }
            }

            // 3. Troca de ponteiros e atualização do pico de ignições
            #pragma omp single
            {
                int *estado_temporario = grade.estado_atual;
                grade.estado_atual = grade.proximo_estado;
                grade.proximo_estado = estado_temporario;

                int *tempo_temporario = grade.tempo_atual;
                grade.tempo_atual = grade.proximo_tempo;
                grade.proximo_tempo = tempo_temporario;

                celulas_em_chamas = proximo_celulas_em_chamas;
                proximo_celulas_em_chamas = 0;

                total_ignicoes += ignicoes_no_passo;
                if (ignicoes_no_passo > pico_qtd) {
                    pico_qtd = ignicoes_no_passo;
                    pico_passo = passo_atual;
                }
                ignicoes_no_passo = 0;

                passo_atual++;
            }
        }
    }

    double t_fim = omp_get_wtime();
    double tempo_execucao = t_fim - t_inicio;

    // --- CONTAGEM DOS ESTADOS FINAIS E CHECKSUM ---
    int nao_combustiveis = 0;
    int intactas = 0;
    int em_chamas = 0;
    int queimadas = 0;
    int contencao = 0;

    #pragma omp parallel for num_threads(T) schedule(static) \
        reduction(+:nao_combustiveis, intactas, em_chamas, queimadas, contencao)
    for (long long i = 0; i < total_celulas; i++) {
        ESTADO_CODIGO est = (ESTADO_CODIGO)grade.estado_atual[i];
        if (est == ESTADO_NAO_COMBUSTIVEL) nao_combustiveis++;
        else if (est == ESTADO_INTACTA) intactas++;
        else if (est == ESTADO_EM_CHAMAS) em_chamas++;
        else if (est == ESTADO_QUEIMADA) queimadas++;
        else if (est == ESTADO_CONTENCAO) contencao++;
    }

    float pct_queimado  = percentual_queimado(queimadas, em_chamas, total_combustiveis);
    float pct_protegido = percentual_protegido(contencao, total_combustiveis);
    unsigned long long checksum = 0;
    for (long long i = 0; i < L * C; i++) {
        checksum = checksum * 31ULL + (unsigned long long)grade.estado_atual[i];
        checksum = checksum * 31ULL + (unsigned long long)grade.tempo_atual[i];
    }

    // --- Impressão
    printf("passos: %d\n", passo_atual);
    printf("nao_combustiveis: %d\n", nao_combustiveis);
    printf("intactas: %d\n", intactas);
    printf("em_chamas: %d\n", em_chamas);
    printf("queimadas: %d\n", queimadas);
    printf("contencao: %d\n", contencao);
    printf("total_ignicoes: %d\n", total_ignicoes);
    printf("pico_ignicoes: %d %d\n", pico_passo, pico_qtd);
    printf("percentual_queimado: %.2f\n", pct_queimado);
    printf("percentual_protegido: %.2f\n", pct_protegido);
    printf("checksum: %llu\n", checksum);
    printf("tempo: %.6f\n", tempo_execucao);

    liberar_grade(&grade);
    return 0;
}