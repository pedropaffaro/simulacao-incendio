#ifndef FUNCS_H
#define FUNCS_H

#include "constants.h"

/**
 * @brief Retorna o estado da célula após a ativação de uma zona de contenção.
 *
 * Apenas células intactas são convertidas
 * para contenção; os demais estados permanecem inalterados.
 *
 * @param estado Estado atual da célula.
 * @return Novo estado após a ativação.
 */
ESTADO_CODIGO estado_apos_ativacao(ESTADO_CODIGO estado);

/**
 * @brief Retorna o fator de combustível associado ao tipo de cobertura.
 *
 * água=0, solo=0, vegetação=8, floresta=12.
 *
 * @param cobertura Tipo de cobertura da célula.
 * @return Fator de combustível
 */
int fator_cobertura(COBERTURA_CODIGO cobertura);

/**
 * @brief Retorna o tempo inicial de queima conforme o tipo de cobertura.
 *
 * Vegetação rasteira: 2 passos. Floresta: 4 passos. Água e solo não queimam (retorna ERRO).
 *
 * @param cobertura Tipo de cobertura da célula.
 * @return Tempo de queima em passos.
 */
int tempo_queima_inicial(COBERTURA_CODIGO cobertura);

/**
 * @brief Calcula o peso de um vizinho em chamas para o potencial de ignição.
 *
 * Pv = max(1, P_basico + intensidade * A), onde
 * A = prop_linha * vento_linha + prop_coluna * vento_coluna.
 * P_basico = 10 (ortogonal) ou 7 (diagonal).
 *
 * @param prop_linha   Componente vertical do sentido de propagação (célula - vizinho).
 * @param prop_coluna  Componente horizontal do sentido de propagação (célula - vizinho).
 * @param vento_linha  Componente vertical da direção do vento.
 * @param vento_coluna Componente horizontal da direção do vento.
 * @param intensidade  Intensidade do vento (0 a 5).
 * @return Peso do vizinho (mínimo 1).
 */
int peso_vizinho(int prop_linha, int prop_coluna, int vento_linha, int vento_coluna, int intensidade);

/**
 * @brief Calcula o potencial de ignição com aritmética inteira.
 *
 * I = floor(S * fator_combustivel * (100 - umidade) / 100).
 * A divisão inteira já aplica o floor implicitamente.
 *
 * @param S                Soma dos pesos de todos os vizinhos de Moore em chamas.
 * @param fator_combustivel Fator de combustível da célula avaliada.
 * @param umidade          Umidade da célula avaliada (0 a 100).
 * @return Potencial de ignição I.
 */
int potencial_ignicao(int S, int fator_combustivel, int umidade);

/**
 * @brief Calcula o percentual da área combustível que foi queimada.
 *
 * 100 * (queimadas + em_chamas) / combustiveis_iniciais.
 * Retorna 0.0 se não houver células combustíveis iniciais.
 *
 * @param queimadas Quantidade de células no estado queimada.
 * @param em_chamas Quantidade de células ainda em chamas.
 * @param combustiveis_iniciais Quantidade inicial de células combustíveis.
 * @return Percentual queimado (0.0 a 100.0).
 */
float percentual_queimado(int queimadas, int em_chamas, int combustiveis_iniciais);

/**
 * @brief Calcula o percentual da área combustível que foi protegida por contenção.
 *
 * 100 * contencoes / combustiveis_iniciais.
 * Retorna 0.0 se não houver células combustíveis iniciais.
 *
 * @param contencoes Quantidade de células no estado contenção.
 * @param combustiveis_iniciais Quantidade inicial de células combustíveis.
 * @return Percentual protegido (0.0 a 100.0).
 */
float percentual_protegido(int contencoes, int combustiveis_iniciais);

#endif
