// #define _POSIX_C_SOURCE 200112L
// #include <stdio.h>
// #include <stdlib.h>
// #include <omp.h>
// #include "../include/funcs.h"

// int main(int argc, char *argv[]) {
//     if (validar_argc(argc, argv[0]) != LEITURA_OK) return 1;

//     FILE *input = abrir_arquivo(argv[1]);
//     if (input == NULL) return 1;

//     int L, C, P, T, LIMIAR;
//     unsigned int seed;
//     LEITURA_STATUS stats;

//     stats = ler_config_geral(input, &L, &C, &P, &T, &seed, &LIMIAR);
//     if (stats != LEITURA_OK) { fclose(input); return stats == LEITURA_ERRO_SISTEMA ? 1 : 0; }

//     int vento_linha, vento_coluna, vento_intensidade;
//     stats = ler_config_vento(input, &vento_linha, &vento_coluna, &vento_intensidade);
//     if (stats != LEITURA_OK) { fclose(input); return stats == LEITURA_ERRO_SISTEMA ? 1 : 0; }

//     int F, num_zonas_contencao;
//     stats = ler_contagem_focos_zonas(input, &F, &num_zonas_contencao);
//     if (stats != LEITURA_OK) { fclose(input); return stats == LEITURA_ERRO_SISTEMA ? 1 : 0; }

//     long long total_celulas = (long long)L * C;
//     GRADE grade;
//     if (!alocar_grade(&grade, total_celulas)) {
//         fclose(input);
//         liberar_grade(&grade);
//         return 1;
//     }

//     gerar_terreno(&grade, total_celulas, seed);

//     stats = ler_focos(input, F, L, C, grade.cobertura, grade.estado_atual, grade.tempo_atual);
//     if (stats != LEITURA_OK) {
//         fclose(input);
//         liberar_grade(&grade);
//         return stats == LEITURA_ERRO_SISTEMA ? 1 : 0;
//     }

//     stats = ler_zonas_contencao(input, num_zonas_contencao, L, C, P, grade.ativacao);
//     if (stats != LEITURA_OK) {
//         fclose(input);
//         liberar_grade(&grade);
//         return stats == LEITURA_ERRO_SISTEMA ? 1 : 0;
//     }

//     fclose(input);

//     // --- A PARTIR DAQUI SEGUE A EXECUÇÃO CRONOMETRADA DA SIMULAÇÃO ---

//     int total_combustiveis = 0;
//     int celulas_em_chamas = 0;
//     #pragma omp parallel for num_threads(T) schedule(static) \
//     reduction(+:total_combustiveis, celulas_em_chamas)
//     for (long long i = 0; i < total_celulas; i++) {
//         COBERTURA_CODIGO cob = (COBERTURA_CODIGO)grade.cobertura[i];
//         if (cob == COBERTURA_CODIGO_VEGETACAO || cob == COBERTURA_CODIGO_FLORESTA) {
//             total_combustiveis++;
//         }
//         if (grade.estado_atual[i] == ESTADO_EM_CHAMAS) {
//             celulas_em_chamas++;
//         }
//     }

//     // Pré-cálculo dos pesos e deslocamentos 
//     long long deslocamento_offset[8];
//     int pesos_direcao[8];

//     for (int k = 0; k < 8; k++) {
//         int variacao_linha = VIZINHOS[k].vertical;
//         int variacao_coluna = VIZINHOS[k].horizontal;

//         deslocamento_offset[k] = (long long) variacao_linha * C + variacao_coluna;
//         pesos_direcao[k] = peso_vizinho(-variacao_linha, -variacao_coluna, vento_linha, vento_coluna, vento_intensidade);
//     }

//     int passo_atual = 0;
//     int total_queimadas = 0;
//     int total_contencoes = 0;
//     int proximo_celulas_em_chamas = 0;

//     #pragma omp parallel num_threads(T) default(none) \
//         shared(L, C, P, total_celulas, grade, LIMIAR, passo_atual, \
//             celulas_em_chamas, proximo_celulas_em_chamas, \
//             deslocamento_offset, pesos_direcao, VIZINHOS, \
//             total_queimadas, total_contencoes)
//     {
//         while (passo_atual < P && celulas_em_chamas > 0) {

//             // 1. Ativação das contenções
//             #pragma omp for schedule(static) reduction(+:total_contencoes)
//             for (long long i = 0; i < total_celulas; i++) {
//                 if (grade.ativacao[i] == passo_atual && grade.estado_atual[i] == ESTADO_INTACTA) {
//                     grade.estado_atual[i] = ESTADO_CONTENCAO;
//                     total_contencoes++;
//                 }
//             }

//             // 2. Simulação e redução direta em proximo_celulas_em_chamas
//             #pragma omp for collapse(2) schedule(static) reduction(+:proximo_celulas_em_chamas, total_queimadas)
//             for (int l = 0; l < L; l++) {
//                 for (int c = 0; c < C; c++) {
//                     long long i = (long long)l * C + c;
//                     ESTADO_CODIGO estado_celula = (ESTADO_CODIGO)grade.estado_atual[i];

//                     if (estado_celula == ESTADO_INTACTA) {
//                         int S = 0;

//                         if (l > 0 && l < L - 1 && c > 0 && c < C - 1) {
//                             for (int k = 0; k < 8; k++) {
//                                 if (grade.estado_atual[i + deslocamento_offset[k]] == ESTADO_EM_CHAMAS) {
//                                     S += pesos_direcao[k];
//                                 }
//                             }
//                         } else {
//                             for (int k = 0; k < 8; k++) {
//                                 int linha_vizinho  = l + VIZINHOS[k].vertical;
//                                 int coluna_vizinho = c + VIZINHOS[k].horizontal;

//                                 if (linha_vizinho >= 0 && linha_vizinho < L && coluna_vizinho >= 0 && coluna_vizinho < C) {
//                                     long long vizinho = (long long)linha_vizinho * C + coluna_vizinho;
//                                     if (grade.estado_atual[vizinho] == ESTADO_EM_CHAMAS) {
//                                         S += pesos_direcao[k];
//                                     }
//                                 }
//                             }
//                         }

//                         if (S > 0 && potencial_ignicao(S, fator_cobertura((COBERTURA_CODIGO)grade.cobertura[i]), grade.umidade[i]) >= LIMIAR) {
//                             grade.proximo_estado[i] = ESTADO_EM_CHAMAS;
//                             grade.proximo_tempo[i]  = tempo_queima_inicial((COBERTURA_CODIGO)grade.cobertura[i]);
//                             proximo_celulas_em_chamas++;
//                         } else {
//                             grade.proximo_estado[i] = ESTADO_INTACTA;
//                             grade.proximo_tempo[i]  = 0;
//                         }

//                     } else if (estado_celula == ESTADO_EM_CHAMAS) {
//                         int tempo_ignicao_restante = grade.tempo_atual[i] - 1;

//                         if (tempo_ignicao_restante == 0) {
//                             grade.proximo_estado[i] = ESTADO_QUEIMADA;
//                             grade.proximo_tempo[i]  = 0;
//                             total_queimadas++;
//                         } else {
//                             grade.proximo_estado[i] = ESTADO_EM_CHAMAS;
//                             grade.proximo_tempo[i]  = tempo_ignicao_restante;
//                             proximo_celulas_em_chamas++;
//                         }

//                     } else {
//                         grade.proximo_estado[i] = estado_celula;
//                         grade.proximo_tempo[i]  = grade.tempo_atual[i];
//                     }
//                 }
//             }

//             // 3. Troca de ponteiros, atualização da contagem e zeramento para o próximo passo
//             #pragma omp single
//             {
//                 int *estado_temporario = grade.estado_atual;
//                 grade.estado_atual = grade.proximo_estado;
//                 grade.proximo_estado = estado_temporario;

//                 int *tempo_temporario = grade.tempo_atual;
//                 grade.tempo_atual = grade.proximo_tempo;
//                 grade.proximo_tempo = tempo_temporario;

//                 // Atualiza o contador do while e reseta a acumuladora do próximo passo
//                 celulas_em_chamas = proximo_celulas_em_chamas;
//                 proximo_celulas_em_chamas = 0;

//                 passo_atual++;
//             }
//         }
//     }

//     // O que sobrou ativo na variável ao sair do loop é o total final
//     int total_em_chamas = celulas_em_chamas; 

//     float pct_queimado  = percentual_queimado(total_queimadas, total_em_chamas, total_combustiveis);
//     float pct_protegido = percentual_protegido(total_contencoes, total_combustiveis);

//     unsigned long long checksum = 0;
//     for (long long i = 0; i < L * C; i++) {
//         checksum = checksum * 31ULL + (unsigned long long)estado_atual[i];
//         checksum = checksum * 31ULL + (unsigned long long)tempo_atual[i];
//     }

//     printf("p: %d/%d\n", passo_atual);
//     printf("nao_combustiveis: %d\n", total_combustiveis);
//     printf("Celulas queimadas: %d\n", total_queimadas);
//     printf("Celulas em chamas: %d\n", total_em_chamas);
//     printf("Celulas em contencao: %d\n", total_contencoes);
//     printf("Percentual queimado: %.2f%%\n", pct_queimado);
//     printf("Percentual protegido: %.2f%%\n", pct_protegido);

//     liberar_grade(&grade);
//     return 0;
// }
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