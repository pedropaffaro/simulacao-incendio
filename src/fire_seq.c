#define _POSIX_C_SOURCE 200112L
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

// Representa uma direção(vertical, horizontal)
/* Usada pra descrever o deslocamento até os vizinhos de uma célula */
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

/* Tabela com os 8 vizinhos de Moore, pra não ficar repetindo as 8 direções toda vez que algum trecho do código precisar varrer a vizinhança */
static const DIRECAO VIZINHOS[8] = {
    DIRECAO_NORTE,    DIRECAO_NORDESTE,
    DIRECAO_LESTE,    DIRECAO_SUDESTE,
    DIRECAO_SUL,      DIRECAO_SUDOESTE,
    DIRECAO_OESTE,    DIRECAO_NOROESTE
};
// clang-format on

/* Tipo de cobertura de terreno de cada célula, sorteado na geração do mapa (gerar_terreno()) */
typedef enum { COBERTURA_CODIGO_AGUA = 0, COBERTURA_CODIGO_SOLO, COBERTURA_CODIGO_VEGETACAO, COBERTURA_CODIGO_FLORESTA } COBERTURA_CODIGO;

/* Geração da cobertura (rand % 100) */
// clang-format off
#define COBERTURA_MAX_AGUA     9  // 0–9   (10%)
#define COBERTURA_MAX_SOLO     19 // 10–19 (10%)
#define COBERTURA_MAX_RASTEIRA 54 // 20–54 (35%)
#define COBERTURA_MAX_FLORESTA 99 // 55–99 (45%)
// clang-format on

/* Multiplicador de combustível usado no cálculo do potencial de ignição (potencial_ignicao())
Água e solo não queimam, então ficam em 0 */
#define FATOR_AGUA 0
#define FATOR_SOLO 0
#define FATOR_RASTEIRA 8
#define FATOR_FLORESTA 12

/* Estado de queima de cada célula durante a simulação, guardado em estado_atual/proximo_estado */
typedef enum { ESTADO_NAO_COMBUSTIVEL = 0, ESTADO_INTACTA, ESTADO_EM_CHAMAS, ESTADO_QUEIMADA, ESTADO_CONTENCAO } ESTADO_CODIGO;

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

/* Estrutura que guarda os ponteiros para os vetores que representam a grade de células */
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
typedef enum { LEITURA_OK = 0, LEITURA_ERRO_SISTEMA, LEITURA_ERRO_ENTRADA } LEITURA_STATUS;

// Representa o pico de ignições da simulação(passo, quantidade)
/* Guarda em que passo ocorreu a maior quantidade de novas ignições, pra imprimir no resultado final */
typedef struct {
    int passo;
    int quantidade;
} PICO;

/* Contadores de células por estado, mais o total de combustíveis iniciais e o total de ignições
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
    // Não tem porque trabalhar com ponto flutuante para <=
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

/* Aloca os 7 vetores da grade
Devolve 0 se algum malloc falhar (com os outros já alocados, por isso sempre precisa chamar liberar_cels() depois) */
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

/* Libera os 7 vetores alocados em alocar_cels() */
void liberar_cels(CELULAS *g) {
    free(g->cobertura);
    free(g->umidade);
    free(g->estado_atual);
    free(g->tempo_atual);
    free(g->proximo_estado);
    free(g->proximo_tempo);
    free(g->ativacao);
}

/* Sorteia a cobertura e a umidade de cada célula com rand_r, na ordem pedida, cobertura primeiro, depois umidade, célula por célula */
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

/* Confere se o programa foi chamado com exatamente 1 argumento (o arquivo de entrada) */
LEITURA_STATUS validar_argc(int argc, const char *argv0) {
    if (argc != 2) {
        fprintf(stderr, "[Erro] Uso correto: %s <arquivo_de_entrada>\n", argv0);
        return LEITURA_ERRO_SISTEMA;
    }
    
    return LEITURA_OK;
}

/* Abre o arquivo de entrada em modo leitura */
FILE *abrir_arquivo(const char *caminho) {
    FILE *f = fopen(caminho, "r");
    if (f == NULL)
        fprintf(stderr, "[Erro] Nao foi possivel abrir o arquivo de entrada '%s'.\n", caminho);

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

/* Lê os F focos iniciais e ateia fogo neles, validando posição dentro da matriz, célula combustível e sem foco repetido */
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

/* Imprime os 12 campos do resultado final, no formato pedido */
void print_data(COUNTERS cnt, int passo_atual, PICO pico, double pct_queimado, double pct_protegido,
                unsigned long long checksum, double tempo) {
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
    /* Valida a quantidade de argumentos da linha de comando */
    if (validar_argc(argc, argv[0]) != LEITURA_OK)
        return EXIT_FAILURE;

    /* Abre o arquivo de entrada passado por argumento */
    FILE *input = abrir_arquivo(argv[argc - 1]);
    if (input == NULL)
        return EXIT_FAILURE;

    int L, C, P, T, LIMIAR;
    unsigned int seed;
    LEITURA_STATUS status;

    /* Lê as configurações gerais da simulação (L, C, P, T, seed, LIMIAR) */
    status = ler_config_geral(input, &L, &C, &P, &T, &seed, &LIMIAR);
    if (status != LEITURA_OK) {
        fclose(input);
        return EXIT_FAILURE;
    }

    /* Lê a configuração da direção e intensidade do vento */
    int vento_linha, vento_coluna, vento_intensidade;
    status = ler_config_vento(input, &vento_linha, &vento_coluna, &vento_intensidade);
    if (status != LEITURA_OK) {
        fclose(input);
        return EXIT_FAILURE;
    }

    /* Lê a quantidade de focos iniciais e de zonas de contenção */
    int F, num_zonas;
    status = ler_contagem_focos_zonas(input, &F, &num_zonas);
    if (status != LEITURA_OK) {
        fclose(input);
        return EXIT_FAILURE;
    }

    /* Aloca a memória das estruturas da grade */
    long long total_celulas = (long long)L * C;
    CELULAS celulas;
    if (!alocar_cels(&celulas, total_celulas)) {
        fclose(input);
        liberar_cels(&celulas);
        return EXIT_FAILURE;
    }

    /* Gera a cobertura e a umidade inicial de cada célula */
    gerar_terreno(&celulas, total_celulas, seed);

    /* Lê e aplica os focos iniciais de incêndio */
    status = ler_focos(input, F, L, C, celulas.cobertura, celulas.estado_atual, celulas.tempo_atual);
    if (status != LEITURA_OK) {
        fclose(input);
        liberar_cels(&celulas);
        return EXIT_FAILURE;
    }

    /* Lê as zonas de contenção e define o passo de ativação das células */
    status = ler_zonas_contencao(input, num_zonas, L, C, P, celulas.ativacao);
    if (status != LEITURA_OK) {
        fclose(input);
        liberar_cels(&celulas);
        return EXIT_FAILURE;
    }

    /* Fecha o arquivo de entrada após finalizar as leituras */
    fclose(input);

    COUNTERS cnt = {0};

    /* Varredura inicial pra contar combustíveis e a distribuição de estados antes de começar a simular
    Fica fora do trecho cronometrado, então não conta como custo da simulação em si */
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

    long long deslocamento_offset[8];
    int pesos_direcao[8];

    /* Pré-calcula os deslocamentos de memória em 1D e os pesos direcionais com o vento
    para os 8 vizinhos de Moore, já que o vento é constante durante toda a simulação
    (mesma otimização da versão paralela, Seção 4.4) */
    for (int k = 0; k < 8; k++) {
        int variacao_linha  = VIZINHOS[k].vertical;
        int variacao_coluna = VIZINHOS[k].horizontal;

        deslocamento_offset[k] = (long long)variacao_linha * C + variacao_coluna;
        pesos_direcao[k]       = peso_vizinho(-variacao_linha, -variacao_coluna, vento_linha, vento_coluna, vento_intensidade);
    }

    /* Marca o tempo inicial da simulação */
    double t_inicio = omp_get_wtime();

    int passo_atual = 0;
    PICO pico       = {-1, 0};

    // Pico já começa em {-1, 0} pra sair certo (-1 0) se a simulação não tiver nenhuma ignição
    while (passo_atual < P && cnt.em_chamas > 0) {
        /* Ativa as zonas de contenção agendadas para o passo_atual
        Corpo do laço escrito em forma branchless (sem if): em vez de pular a escrita quando a condição é falsa,
        sempre escreve, selecionando entre o novo estado e o estado atual (seguro porque reescrever o mesmo valor
        não tem efeito). Mesma forma usada em fire_omp.c para viabilizar #pragma omp simd (Seção 4.5 do relatório);
        aqui não há nenhuma diretiva omp, só a reescrita aritmética, para isolar o efeito da forma branchless em si
        do efeito do pragma. O & no lugar de && é proposital, pra evitar o curto-circuito que reintroduziria
        controle de fluxo e poderia impedir a autovetorização do gcc mesmo sem a diretiva. */
        for (long long i = 0; i < total_celulas; i++) {
            int deve_ativar = (celulas.ativacao[i] == passo_atual) & (celulas.estado_atual[i] == ESTADO_INTACTA);
            celulas.estado_atual[i] = deve_ativar ? ESTADO_CONTENCAO : celulas.estado_atual[i];
        }

        // Próximo estado e estatísticas
        int novos_incendios = 0;
        int next_nao_comb = 0, next_intactas = 0, next_em_chamas = 0;
        int next_queimadas = 0, next_contencao = 0;

        for (int l = 0; l < L; l++) {
            for (int c = 0; c < C; c++) {
                /* Índice construído diretamente de l e c, sem divisão/módulo (Seção 3.2/4.4) */
                long long i = (long long)l * C + c;

                switch (celulas.estado_atual[i]) {
                    case ESTADO_NAO_COMBUSTIVEL: {
                        celulas.proximo_estado[i] = ESTADO_NAO_COMBUSTIVEL;
                        celulas.proximo_tempo[i]  = 0;
                        next_nao_comb++;
                        break;
                    }

                    case ESTADO_INTACTA: {
                        int S = 0;

                        /* Célula interna: todos os 8 vizinhos existem, dispensa checar limites da matriz (Seção 4.4).
                        Soma reescrita em forma branchless (multiplica o peso por 0/1 em vez de somar condicionalmente),
                        mesma forma usada em fire_omp.c para viabilizar #pragma omp simd reduction(+:S) (Seção 4.5);
                        aqui, de novo, sem nenhuma diretiva omp. */
                        if (l > 0 && l < L - 1 && c > 0 && c < C - 1) {
                            for (int viz = 0; viz < 8; viz++) {
                                int atual_em_chamas = (celulas.estado_atual[i + deslocamento_offset[viz]] == ESTADO_EM_CHAMAS);
                                S += pesos_direcao[viz] * atual_em_chamas;
                            }
                        } else {
                            /* Célula de borda: soma o peso dos vizinhos existentes, checando os limites da matriz */
                            for (int viz = 0; viz < 8; viz++) {
                                int linha_vizinho  = l + VIZINHOS[viz].vertical;
                                int coluna_vizinho = c + VIZINHOS[viz].horizontal;

                                if (linha_vizinho < 0 || linha_vizinho >= L || coluna_vizinho < 0 || coluna_vizinho >= C)
                                    continue;

                                long long viz_idx = (long long)linha_vizinho * C + coluna_vizinho;

                                if (celulas.estado_atual[viz_idx] == ESTADO_EM_CHAMAS)
                                    S += pesos_direcao[viz];
                            }
                        }

                        /* Curto-circuito: sem vizinho em chamas, o potencial é 0 e nunca atinge LIMIAR > 0 (Seção 4.4) */
                        if (S == 0) {
                            celulas.proximo_estado[i] = ESTADO_INTACTA;
                            celulas.proximo_tempo[i]  = 0;
                            next_intactas++;
                            break;
                        }

                        /* Calcula o potencial de ignição e verifica se atinge o limiar pra pegar fogo */
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
                        /* Decrementa o tempo de queima restante da célula */
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
        }

        // Comparação estrita: em empate fica o primeiro passo que atingiu aquela quantidade
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

        // Troca as matrizes
        int *tmp;
        tmp                    = celulas.estado_atual;
        celulas.estado_atual   = celulas.proximo_estado;
        celulas.proximo_estado = tmp;
        tmp                    = celulas.tempo_atual;
        celulas.tempo_atual    = celulas.proximo_tempo;
        celulas.proximo_tempo  = tmp;

        passo_atual++;
    }

    /* Calcula o tempo total gasto na simulação */
    double tempo_execucao = omp_get_wtime() - t_inicio;

    // Checksum
    // Calculado depois de parar o cronômetro, sempre na mesma ordem sequencial, pra dar exatamente o mesmo valor na versão paralela
    unsigned long long checksum = 0;
    for (long long i = 0; i < total_celulas; i++) {
        checksum = checksum * 31ULL + (unsigned long long)celulas.estado_atual[i];
        checksum = checksum * 31ULL + (unsigned long long)celulas.tempo_atual[i];
    }

    // Percentuais
    /* Calcula o percentual de área queimada e de área protegida em relação ao combustível inicial */
    double pct_queimado  = percentual_queimado(cnt.queimadas, cnt.em_chamas, cnt.combustiveis_iniciais);
    double pct_protegido = percentual_protegido(cnt.contencao, cnt.combustiveis_iniciais);

    /* Libera os vetores alocados pra simulação */
    liberar_cels(&celulas);

    /* Imprime a saída final */
    print_data(cnt, passo_atual, pico, pct_queimado, pct_protegido, checksum, tempo_execucao);

    return EXIT_SUCCESS;
}