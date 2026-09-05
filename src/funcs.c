#include "../include/funcs.h"
#include <stdlib.h>

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

    return peso_vizinho < 1 ? 1 : (peso_vizinho + intensidade * A);
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
