#define _POSIX_C_SOURCE 200112L
#include "../include/funcs.h"
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

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
