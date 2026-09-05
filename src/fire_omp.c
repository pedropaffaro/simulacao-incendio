#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

// DEBUG!!!!! Função determinística: produz exatamente o mesmo terreno no Mac, Linux e Windows
int rand_r_portavel(unsigned int *seed) {
    *seed = *seed * 1103515245 + 12345;
    return (unsigned int)(*seed / 65536) % 32768;
}

int main(int argc, char *argv[]) {
    // 1. Validação da presença de um único argumento de linha de comando (Seção 3 e 5)
    if (argc != 2) {
        fprintf(stderr, "[Erro] Uso correto: %s <arquivo_de_entrada>\n", argv[0]);
        return 1;
    }

    // 2. Abertura do arquivo de entrada (Seção 5)
    FILE *arq = fopen(argv[1], "r");
    if (arq == NULL) {
        fprintf(stderr, "[Erro] Nao foi possivel abrir o arquivo de entrada '%s'.\n", argv[1]);
        return 1;
    }

    // 3. Leitura e validação da 1ª linha: Configuração Geral (Seção 4.1 e 5)
    int L, C, P, T, LIMIAR;
    unsigned int seed;

    if (fscanf(arq, "%d %d %d %d %u %d", &L, &C, &P, &T, &seed, &LIMIAR) != 6) {
        fprintf(stderr, "[Erro] Não foi pssível ler a primeira linha do arquivo.\n");
        fclose(arq);
        return 1;
    }

    if (L <= 0 || C <= 0 || P < 0 || T <= 0 || LIMIAR <= 0) {
        fprintf(stderr, "[Erro] Parâmetros gerais inválidos (L>0, C>0, P>=0, T>0, LIMIAR>0).\n");
        fclose(arq);
        return 1;
    }

    // 4. Leitura e validação da 2ª linha: Configuração do Vento (Seção 4.2 e 5)
    int vento_linha, vento_coluna, vento_intensidade;

    if (fscanf(arq, "%d %d %d", &vento_linha, &vento_coluna, &vento_intensidade) != 3) {
        fprintf(stderr, "[Erro] Não foi pssível ler a configuração do vento.\n");
        fclose(arq);
        return 1;
    }

    if (vento_linha < -1 || vento_linha > 1 || 
        vento_coluna < -1 || vento_coluna > 1 || 
        (vento_linha == 0 && vento_coluna == 0) || 
        vento_intensidade < 0 || vento_intensidade > 5) {
        printf("[Erro] Os valores inseridos para configuração do vento são inválidos.\n");
        fclose(arq);
        return 0;
    }

    // 5. Leitura e validação da 3ª linha: Focos e Zonas (Seção 4.3 e 5)
    int F, num_zonas_contencao;

    if (fscanf(arq, "%d %d", &F, &num_zonas_contencao) != 2) {
        fprintf(stderr, "[Erro] Não foi possível ler quantidades de focos e zonas de contenção.\n");
        fclose(arq);
        return 1;
    }

    if (F < 0 || num_zonas_contencao < 0) {
        fprintf(stderr, "[Erro] Quantidade de focos (F) ou zonas (Z) inválida.\n");
        fclose(arq);
        return 1;
    }

    // 6. Alocação dinâmica de memória no Heap (Matriz 1D, Seção 6.1)
    long long total_celulas = (long long)L * C;
    int *cobertura = (int *) malloc(total_celulas * sizeof(int));
    int *umidade = (int *) malloc(total_celulas * sizeof(int));
    int *estado_atual = (int *) malloc(total_celulas * sizeof(int));
    int *tempo_atual = (int *) malloc(total_celulas * sizeof(int));
    int *proximo_estado = (int *) malloc(total_celulas * sizeof(int));
    int *proximo_tempo  = (int *) malloc(total_celulas * sizeof(int));
    int *ativacao = (int *) malloc(total_celulas * sizeof(int));

    if (!cobertura || !umidade || !estado_atual || !tempo_atual || !ativacao) {
        fprintf(stderr, "[Erro] Não foi possível alocar memoria para as estruturas da matriz.\n");
        fclose(arq);
        free(cobertura); free(umidade); free(estado_atual); free(tempo_atual); free(proximo_estado); free(proximo_tempo); free(ativacao);
        return 1;
    }

    // 7. Geração de Cobertura e Umidade da Floresta (Seções 6.2, 6.3 e 6.4)
    unsigned int current_seed = seed;

    // Passo 1: Gerar toda a Cobertura
    for (long long i = 0; i < total_celulas; i++) {
        int val = rand_r_portavel(&current_seed) % 100;
        if (val <= 9) {
            cobertura[i] = 0;    // Água
            estado_atual[i] = 0;
        } else if (val <= 19) {
            cobertura[i] = 1;    // Solo exposto
            estado_atual[i] = 0;
        } else if (val <= 54) {
            cobertura[i] = 2;    // Vegetação rasteira
            estado_atual[i] = 1;
        } else {
            cobertura[i] = 3;    // Floresta
            estado_atual[i] = 1;
        }
    }

    // Passo 2: Gerar toda a Umidade (e inicializar o restante)
    for (long long i = 0; i < total_celulas; i++) {
        umidade[i] = rand_r_portavel(&current_seed) % 101;
        tempo_atual[i] = 0;
        ativacao[i] = -1;
    }

    // 8. Leitura e validação dos Focos Iniciais de Incêndio (Seções 5 e 6.5)
    for (int i = 0; i < F; i++) {
        int linha, coluna;
        if (fscanf(arq, "%d %d", &linha, &coluna) != 2) {
            fprintf(stderr, "[Erro] Não foi possível ler o foco inicial %d.\n", i + 1);
            fclose(arq);
            free(cobertura); free(umidade); free(estado_atual); free(tempo_atual); free(proximo_estado); free(proximo_tempo); free(ativacao);
            return 1;
        }

        // Validação de limites da matriz
        if (linha < 0 || linha >= L || coluna < 0 || coluna >= C) {
            printf("[Erro] Os valores inseridos para os limites do foco inicial são inválidos.\n");
            fclose(arq);
            free(cobertura); free(umidade); free(estado_atual); free(tempo_atual); free(proximo_estado); free(proximo_tempo); free(ativacao);
            return 0;
        }

        long long i = (long long)linha * C + coluna;

        // Validação de foco repetido
        if (estado_atual[i] == 2) {
            printf("[Erro] Os valores inseridos do foco inicial são inválidos (foco repetido).\n");
            fclose(arq);
            free(cobertura); free(umidade); free(estado_atual); free(tempo_atual); free(proximo_estado); free(proximo_tempo); free(ativacao);
            return 0;
        }

        // Validação de célula combustível (foco sobre água ou solo exposto é inválido)
        if (cobertura[i] == 0 || cobertura[i] == 1) {
            printf("[Erro] Os valores inseridos do foco inicial são inválidos.\n");
            fclose(arq);
            free(cobertura); free(umidade); free(estado_atual); free(tempo_atual); free(proximo_estado); free(proximo_tempo); free(ativacao);
            return 0;
        }

        // Aplicação do foco
        estado_atual[i] = 2; // Em chamas
        tempo_atual[i] = (cobertura[i] == 2) ? 2 : 4; // 2 passos para rasteira, 4 para floresta
    }

    // 9. Leitura, validação e construção do Mapa de Contenção (Seções 5 e 6.6)
    for (int i = 0; i < num_zonas_contencao; i++) {
        int passo_ativacao, linha_inicial, coluna_inicial, linha_final, coluna_final;
        if (fscanf(arq, "%d %d %d %d %d", &passo_ativacao, &linha_inicial, &coluna_inicial, &linha_final, &coluna_final) != 5) {
            fprintf(stderr, "[Erro] Não foi possível ler a zona de contenção %d.\n", i + 1);
            fclose(arq);
            free(cobertura); free(umidade); free(estado_atual); free(tempo_atual); free(proximo_estado); free(proximo_tempo); free(ativacao);
            return 1;
        }

        // Validações da contenção:
        // - 0 <= passo_ativacao < P
        // - limites dentro da matriz
        // - limites iniciais <= limites finais
        if (passo_ativacao < 0 || passo_ativacao >= P || 
            linha_inicial < 0 || linha_inicial >= L || 
            coluna_inicial < 0 || coluna_inicial >= C || 
            linha_final < 0 || linha_final >= L || 
            coluna_final < 0 || coluna_final >= C ||
            linha_inicial > linha_final ||
            coluna_inicial > coluna_final) {
            printf("[Erro] Os valores inseridos para contenção são inválidos.\n");
            fclose(arq);
            free(cobertura); free(umidade); free(estado_atual); free(tempo_atual); free(proximo_estado); free(proximo_tempo); free(ativacao);
            return 0;
        }

        // Armazena no mapa de ativação (mantém o menor passo de ativação se houver sobreposição)
        for (int r = linha_inicial; r <= linha_final; r++) {
            for (int c = coluna_inicial; c <= coluna_final; c++) {
                long long idx = (long long)r * C + c;
                if (ativacao[idx] == -1 || passo_ativacao < ativacao[idx]) {
                    ativacao[idx] = passo_ativacao;
                }
            }
        }
    }

    fclose(arq);

    // --- A PARTIR DAQUI SEGUE A EXECUÇÃO CRONOMETRADA DA SIMULAÇÃO ---

    // Faz a contagem de células em chamas logo após a inicialização
    int celulas_em_chamas = 0;
    for (long long i = 0; i < total_celulas; i++) {
        if (estado_atual[i] == 2) {
            celulas_em_chamas++;
        }
    }

    // Variável que representa o passo atual
    int passo_atual = 0;

    #pragma omp parallel num_threads(T) default(none) shared(L, C, P, total_celulas, ativacao, estado_atual, proximo_estado, tempo_atual, proximo_tempo, LIMIAR, vento_linha, vento_coluna, vento_intensidade, cobertura, umidade, passo_atual, celulas_em_chamas)
    {
        while(passo_atual < P && celulas_em_chamas > 0){ // NÃO PODE SER PARALELIZADO -> CADA PASSO DEPENDE DO ANTERIOR
            // Ativação das zonas programadas para p
            #pragma omp for schedule(static) 
            for(long long i = 0; i < total_celulas; i++)
                if(ativacao[i] == passo_atual && estado_atual[i] == 1)
                    estado_atual[i] = 4;

            // Cálculo do próximo estado das células


            #pragma omp master // Pode ser a single também, precisamos decidir qual é melhor
            {
                passo_atual++;
            }
        }
    }

    // Liberação final de memória após a simulação
    free(cobertura);
    free(umidade);
    free(estado_atual);
    free(tempo_atual);
    free(proximo_estado);
    free(proximo_tempo);
    free(ativacao);

    return 0;
}