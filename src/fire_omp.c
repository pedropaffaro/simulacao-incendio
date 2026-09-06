#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include "../include/funcs.h"

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

    // --- A PARTIR DAQUI SEGUE A EXECUÇÃO CRONOMETRADA DA SIMULAÇÃO ---

    int celulas_em_chamas = 0;
    for (long long i = 0; i < total_celulas; i++) {
        if (grade.estado_atual[i] == ESTADO_EM_CHAMAS)
            celulas_em_chamas++;
    }

    int passo_atual = 0;

    #pragma omp parallel num_threads(T) default(none) shared(L, C, P, total_celulas, grade, LIMIAR, vento_linha, vento_coluna, vento_intensidade, passo_atual, celulas_em_chamas)
    {
        while (passo_atual < P && celulas_em_chamas > 0) { // NÃO PODE SER PARALELIZADO -> CADA PASSO DEPENDE DO ANTERIOR
            #pragma omp for schedule(static)
            for (long long i = 0; i < total_celulas; i++)
                if (grade.ativacao[i] == passo_atual && grade.estado_atual[i] == ESTADO_INTACTA)
                    grade.estado_atual[i] = ESTADO_CONTENCAO;

            // Cálculo do próximo estado das células

            #pragma omp master // Pode ser a single também, precisamos decidir qual é melhor
            {
                passo_atual++;
            }
        }
    }

    liberar_grade(&grade);
    return 0;
}
