#include "../include/funcs.h"
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