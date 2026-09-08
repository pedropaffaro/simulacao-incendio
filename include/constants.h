#ifndef CONSTANTS_H
#define CONSTANTS_H

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

#endif
