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

    // Pré-cálculo dos pesos e deslocamentos 
    long long deslocamento_offset[8];
    int pesos_direcao[8];

    for (int k = 0; k < 8; k++) {
        int variacao_linha = VIZINHOS[k].vertical;
        int variacao_coluna = VIZINHOS[k].horizontal;

        deslocamento_offset[k] = (long long) variacao_linha * C + variacao_coluna;
        pesos_direcao[k] = peso_vizinho(-variacao_linha, -variacao_coluna, vento_linha, vento_coluna, vento_intensidade);
    }

    int passo_atual = 0;

    // Região Paralela
    #pragma omp parallel num_threads(T) default(none) \
        shared(L, C, P, total_celulas, grade, LIMIAR, vento_linha, vento_coluna, vento_intensidade, \
            passo_atual, celulas_em_chamas, deslocamento_offset, pesos_direcao, VIZINHOS)
    {
        while (passo_atual < P && celulas_em_chamas > 0) {

            // Ativação das zonas de contenção
            #pragma omp for schedule(static)
            for (long long i = 0; i < total_celulas; i++) {
                if (grade.ativacao[i] == passo_atual && grade.estado_atual[i] == ESTADO_INTACTA) {
                    grade.estado_atual[i] = ESTADO_CONTENCAO;
                }
            }

            // Zera o contador de chamas na thread única antes da redução
            #pragma omp single
            {
                celulas_em_chamas = 0;
            } 

            // Cálculo do próximo estado e contagem de celulas_em_chamas por reduction
            #pragma omp for schedule(static) reduction(+:celulas_em_chamas)
            for (long long i = 0; i < total_celulas; i++) {
                ESTADO_CODIGO estado_celula = (ESTADO_CODIGO)grade.estado_atual[i];

                if (estado_celula == ESTADO_INTACTA) { // A célula só pega fogo se estiver intacta
                    int linha_celula = i / C;
                    int coluna_celula = i % C;
                    int S = 0;

                    // Vizinhos de Moore
                    if (linha_celula > 0 && linha_celula < L - 1 && coluna_celula > 0 && coluna_celula < C - 1) { // Se a célula está no miolo, não verifica bordas
                        for (int k = 0; k < 8; k++) {
                            if (grade.estado_atual[i + deslocamento_offset[k]] == ESTADO_EM_CHAMAS) { // Só avalia os pesos se o vizinho está em chamas
                                S += pesos_direcao[k];
                            }
                        }
                    } else { // Se a célula está nos limites, verifica as bordas
                        for (int k = 0; k < 8; k++) {
                            int linha_vizinho  = linha_celula  + VIZINHOS[k].vertical;
                            int coluna_vizinho = coluna_celula + VIZINHOS[k].horizontal;

                            if (linha_vizinho >= 0 && linha_vizinho < L && coluna_vizinho >= 0 && coluna_vizinho < C) {
                                long long vizinho = (long long)linha_vizinho * C + coluna_vizinho;
                                if (grade.estado_atual[vizinho] == ESTADO_EM_CHAMAS) {
                                    S += pesos_direcao[k];
                                }
                            }
                        }
                    }

                    if (S > 0) {
                        if (potencial_ignicao(S, fator_cobertura((COBERTURA_CODIGO)grade.cobertura[i]), grade.umidade[i]) >= LIMIAR) {
                            grade.proximo_estado[i] = ESTADO_EM_CHAMAS;
                            grade.proximo_tempo[i] = tempo_queima_inicial((COBERTURA_CODIGO)grade.cobertura[i]);
                            celulas_em_chamas++;
                        } else {
                            grade.proximo_estado[i] = ESTADO_INTACTA;
                            grade.proximo_tempo[i] = 0;
                        }
                    } else {
                        grade.proximo_estado[i] = ESTADO_INTACTA;
                        grade.proximo_tempo[i] = 0;
                    }

                } else if (estado_celula == ESTADO_EM_CHAMAS) {
                    int tempo_ignicao_restante = grade.tempo_atual[i] - 1;

                    if (tempo_ignicao_restante == 0) {
                        grade.proximo_estado[i] = ESTADO_QUEIMADA;
                        grade.proximo_tempo[i] = 0;
                    } else {
                        grade.proximo_estado[i] = ESTADO_EM_CHAMAS;
                        grade.proximo_tempo[i] = tempo_ignicao_restante;
                        celulas_em_chamas++; // Permanece queimando no próximo passo
                    }

                } else {
                    grade.proximo_estado[i] = estado_celula;
                    grade.proximo_tempo[i]  = grade.tempo_atual[i];
                }
            }

            // Troca de ponteiros e avanço do passo com barreira de sincronização
            #pragma omp single
            {
                int *estado_temporario = grade.estado_atual;
                grade.estado_atual = grade.proximo_estado;
                grade.proximo_estado = estado_temporario;

                int *tempo_temporario = grade.tempo_atual;
                grade.tempo_atual = grade.proximo_tempo;
                grade.proximo_tempo = tempo_temporario;

                passo_atual++;
            }
        }
    }

    liberar_grade(&grade);
    return 0;
}
