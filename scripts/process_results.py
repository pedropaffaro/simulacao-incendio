#!/usr/bin/env python3
"""
scripts/process_results.py: TB1 SSC0903 (fire_seq / fire_omp)

Lê results/raw/runs.csv (gerado por scripts/run_benchmarks.sh), aplica a
mediana como filtro estatístico sobre as repetições de cada configuração, e
gera em results/plot/:

  - tab_tempos.tex             (Tabela 7  -- tempo seq/par)
  - tab_speedup.tex            (Tabela 8  -- S(T), E(T))
  - tab_vazao.tex              (Tabela 9  -- vazão em M atualizações/s)
  - tab_threads_det.tex        (Tabela 4  -- independência de T, carga média)
  - tab_varredura.tex          (Tabela 12 -- tempo/S/E x T, carga grande)
  - tab_schedules.tex          (Tabela 13 -- tempo x schedule x T, carga grande)
  - tab_monothread.tex         (Tabela 10 -- isolamento monothread)
  - tab_speedup_corrigido.tex  (Tabela 11 -- speedup decomposto)
  - fig_tempos.dat, fig_vazao.dat, fig_speedup.dat (dados para pgfplots)

Cada tab_*.tex contém SOMENTE as linhas de corpo da tabela (entre \midrule e
\bottomrule), para dar \input{} de dentro do tabular já existente no
main.tex -- assim o texto e a formatação da tabela ficam no relatório, e só
os números vêm do processamento.

Uso:
    python3 scripts/process_results.py [caminho/para/runs.csv]
"""
import csv
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CSV_PATH = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "results" / "raw" / "runs.csv"
PLOT_DIR = ROOT / "results" / "plot"

CARGA_ORDEM = ["pequena", "media", "grande"]
CARGA_LABEL = {"pequena": "Pequena", "media": "Média", "grande": "Grande"}

# Numero de nucleos fisicos do processador usado nos experimentos (Tabela 4 /
# Ambiente experimental do relatorio: Intel Core i7-4790, 4 fisicos / 8 logicos
# via hyperthreading). Usado para normalizar a eficiencia "real" na Tabela 9
# (tab_speedup_corrigido.tex), em contraste com a normalizacao pelo T nominal
# de cada entrada, que para as cargas media/grande e 8 (inclui as threads
# logicas de hyperthreading, nao so os nucleos fisicos).
NUM_NUCLEOS_FISICOS = 4

NUM_FIELDS = {
    "T_arquivo", "rep", "passos", "nao_combustiveis", "intactas", "em_chamas",
    "queimadas", "contencao", "total_ignicoes", "pico_passo", "pico_qtd",
    "percentual_queimado", "percentual_protegido", "tempo",
}


# ---------------------------------------------------------------------------
# Leitura e utilitários
def load_runs(path):
    if not path.exists():
        sys.exit(f"[erro] arquivo não encontrado: {path}\n"
                  f"       rode scripts/run_benchmarks.sh antes de processar.")
    with open(path, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in NUM_FIELDS:
            r[k] = float(r[k]) if "." in r[k] or k in ("percentual_queimado", "percentual_protegido") else int(float(r[k]))
        r["checksum"] = int(r["checksum"])
    return rows


def where(rows, **filters):
    out = rows
    for k, v in filters.items():
        out = [r for r in out if r[k] == v]
    return out


def median_tempo(rows):
    if not rows:
        return None
    return statistics.median(r["tempo"] for r in rows)


def check_determinism(rows, chave_grupo):
    """Avisa (não falha) se checksum/total_ignicoes/pico variarem dentro do
    mesmo grupo de configuração onde deveriam ser constantes."""
    campos = ("checksum", "total_ignicoes", "pico_passo", "pico_qtd")
    valores = {c: {r[c] for r in rows} for c in campos}
    for c, vs in valores.items():
        if len(vs) > 1:
            print(f"[aviso] {chave_grupo}: campo '{c}' variou entre execuções ({vs}) "
                  f"-- verifique determinismo!", file=sys.stderr)


def fmt_int(n):
    return f"{int(round(n)):,}".replace(",", "\\,")


def fmt_dec(x, casas=2):
    return f"{x:.{casas}f}".replace(".", "{,}")


def fmt_pico(passo, qtd):
    return f"{int(passo)}\\ \\ {fmt_int(qtd)}"


_lc_cache = {}


def get_lc(entrada_rel_path):
    """Lê L e C da primeira linha do arquivo de entrada referenciado no CSV."""
    if entrada_rel_path in _lc_cache:
        return _lc_cache[entrada_rel_path]
    path = ROOT / entrada_rel_path
    if not path.exists():
        # tenta relativo ao cwd também, caso o CSV tenha sido gerado alhures
        path = Path(entrada_rel_path)
    with open(path) as f:
        primeira = f.readline().split()
    L, C = int(primeira[0]), int(primeira[1])
    _lc_cache[entrada_rel_path] = (L, C)
    return L, C


def write_tex(nome, linhas):
    PLOT_DIR.mkdir(parents=True, exist_ok=True)
    destino = PLOT_DIR / nome
    with open(destino, "w", encoding="utf-8") as f:
        f.write("\n".join(linhas) + "\n")
    print(f"[ok] {destino.relative_to(ROOT)}")


def write_dat(nome, cabecalho, linhas):
    PLOT_DIR.mkdir(parents=True, exist_ok=True)
    destino = PLOT_DIR / nome
    with open(destino, "w", encoding="utf-8") as f:
        f.write(cabecalho + "\n")
        f.write("\n".join(linhas) + "\n")
    print(f"[ok] {destino.relative_to(ROOT)}")


# ---------------------------------------------------------------------------
# Tabela 7 -- tempos (grupo "tempos") + dados da Figura 1
def tabela_tempos(rows):
    linhas_tex, linhas_dat_seq, linhas_dat_par = [], [], []
    for carga in CARGA_ORDEM:
        seq = where(rows, grupo="tempos", carga=carga, binario="fire_seq")
        par = where(rows, grupo="tempos", carga=carga, binario="fire_omp_static")
        if not seq or not par:
            print(f"[aviso] tempos: faltam dados para carga={carga}", file=sys.stderr)
            continue
        t_seq, t_par = median_tempo(seq), median_tempo(par)
        T = par[0]["T_arquivo"]
        L, C = get_lc(par[0]["entrada"])
        passos = par[0]["passos"]
        atualizacoes_m = round(L * C * passos / 1e6)
        linhas_tex.append(
            f"{CARGA_LABEL[carga]} & {atualizacoes_m}~M & {T} & "
            f"{fmt_dec(t_seq, 6)} & {fmt_dec(t_par, 6)} \\\\"
        )
        linhas_dat_seq.append(f"{CARGA_LABEL[carga]}\t{t_seq:.6f}")
        linhas_dat_par.append(f"{CARGA_LABEL[carga]}\t{t_par:.6f}")
    write_tex("tab_tempos.tex", linhas_tex)
    write_dat("fig_tempos_seq.dat", "carga\ttempo", linhas_dat_seq)
    write_dat("fig_tempos_par.dat", "carga\ttempo", linhas_dat_par)


# ---------------------------------------------------------------------------
# Tabela 8 -- speedup e eficiência (deriva do mesmo grupo "tempos")
def tabela_speedup(rows):
    linhas = []
    for carga in CARGA_ORDEM:
        seq = where(rows, grupo="tempos", carga=carga, binario="fire_seq")
        par = where(rows, grupo="tempos", carga=carga, binario="fire_omp_static")
        if not seq or not par:
            continue
        t_seq, t_par = median_tempo(seq), median_tempo(par)
        T = par[0]["T_arquivo"]
        S = t_seq / t_par
        E = S / T
        linhas.append(f"{CARGA_LABEL[carga]} & {T} & {fmt_dec(S, 3)} & {fmt_dec(E, 3)} \\\\")
    write_tex("tab_speedup.tex", linhas)


# ---------------------------------------------------------------------------
# Tabela 9 -- vazão (M atualizações/s) + dados da Figura 2
def tabela_vazao(rows):
    linhas_tex, linhas_dat = [], []
    for carga in CARGA_ORDEM:
        seq = where(rows, grupo="tempos", carga=carga, binario="fire_seq")
        par = where(rows, grupo="tempos", carga=carga, binario="fire_omp_static")
        if not seq or not par:
            continue
        t_seq, t_par = median_tempo(seq), median_tempo(par)
        T = par[0]["T_arquivo"]
        L, C = get_lc(par[0]["entrada"])
        passos = par[0]["passos"]
        celulas_m = L * C * passos / 1e6
        vaz_seq = celulas_m / t_seq
        vaz_par = celulas_m / t_par
        vaz_par_thread = vaz_par / T
        linhas_tex.append(
            f"{CARGA_LABEL[carga]} & {T} & {fmt_dec(vaz_seq, 1)} & "
            f"{fmt_dec(vaz_par, 1)} & {fmt_dec(vaz_par_thread, 1)} \\\\"
        )
        linhas_dat.append(f"{CARGA_LABEL[carga]}\t{vaz_seq:.2f}\t{vaz_par_thread:.2f}")
    write_tex("tab_vazao.tex", linhas_tex)
    write_dat("fig_vazao.dat", "carga\tsequencial\tparalela_por_thread", linhas_dat)


# ---------------------------------------------------------------------------
# Tabela 4 -- independência do número de threads (carga média)
def tabela_threads_det(rows):
    linhas = []
    for T in (1, 2, 4, 8, 16):
        grupo = where(rows, grupo="threads_det", carga="media", T_arquivo=T)
        if not grupo:
            linhas.append(f"{T}  & \\TODO{{}} & \\TODO{{}} & \\TODO{{}} \\\\")
            continue
        check_determinism(grupo, f"threads_det T={T}")
        r = grupo[0]
        linhas.append(
            f"{T}{' ' if T >= 10 else '  '}& {fmt_int(r['total_ignicoes'])} & "
            f"{fmt_pico(r['pico_passo'], r['pico_qtd'])} & {r['checksum']} \\\\"
        )
    write_tex("tab_threads_det.tex", linhas)

    # aviso extra: checksum deve ser IGUAL para todo T, não só dentro de cada T
    checksums = {r["T_arquivo"]: r["checksum"] for r in where(rows, grupo="threads_det", carga="media")}
    if len(set(checksums.values())) > 1:
        print(f"[aviso] threads_det: checksum difere entre valores de T: {checksums}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Tabela 12 -- varredura de T (carga grande)
def tabela_varredura(rows):
    t_seq_rows = where(rows, grupo="tempos", carga="grande", binario="fire_seq")
    if not t_seq_rows:
        print("[erro] varredura_T: falta tempo sequencial da carga grande (grupo 'tempos')", file=sys.stderr)
        return
    t_seq = median_tempo(t_seq_rows)

    linhas, linhas_dat = [], []
    for T in (1, 2, 4, 8, 16):
        grupo = where(rows, grupo="varredura_T", carga="grande", T_arquivo=T)
        if not grupo:
            linhas.append(f"{T}  & \\TODO{{}} & \\TODO{{}} & \\TODO{{}} \\\\")
            continue
        t_par = median_tempo(grupo)
        S = t_seq / t_par
        E = S / T
        linhas.append(f"{T}{' ' if T >= 10 else '  '}& {fmt_dec(t_par, 6)} & {fmt_dec(S, 2)} & {fmt_dec(E, 2)} \\\\")
        linhas_dat.append(f"{T}\t{t_par:.6f}\t{S:.4f}\t{E:.4f}")
    write_tex("tab_varredura.tex", linhas)
    write_dat("fig_varredura.dat", "T\ttempo\tspeedup\teficiencia", linhas_dat)


# ---------------------------------------------------------------------------
# Tabela 13 -- comparação de schedules (carga grande)
def tabela_schedules(rows):
    schedules = ["static", "static_1024", "dynamic_1024", "guided"]
    schedule_label = {
        "static": "static", "static_1024": "static, 1024",
        "dynamic_1024": "dynamic, 1024", "guided": "guided",
    }
    linhas = []
    dat_por_T = {T: [] for T in (2, 4, 8, 16)}
    for sc in schedules:
        celulas = []
        for T in (2, 4, 8, 16):
            grupo = where(rows, grupo="schedules", carga="grande", schedule=sc, T_arquivo=T)
            if grupo:
                t = median_tempo(grupo)
                celulas.append(fmt_dec(t, 6))
                dat_por_T[T].append(f"{t:.6f}")
            else:
                celulas.append("\\TODO{}")
                dat_por_T[T].append("NaN")
        linhas.append(f"\\texttt{{{schedule_label[sc]}}} & " + " & ".join(celulas) + " \\\\")
    write_tex("tab_schedules.tex", linhas)

    linhas_dat = [f"{T}\t" + "\t".join(dat_por_T[T]) for T in (2, 4, 8, 16)]
    write_dat("fig_schedules.dat", "T\t" + "\t".join(schedules), linhas_dat)


# ---------------------------------------------------------------------------
# Tabela 10 -- isolamento monothread (taskset -c 0)
def tabela_monothread(rows):
    linhas = []
    fatores = {}
    for carga in ("pequena", "media", "grande"):
        seq = where(rows, grupo="monothread", carga=carga, binario="fire_seq")
        par = where(rows, grupo="monothread", carga=carga, binario="fire_omp_static", T_arquivo=1)
        if not seq or not par:
            continue
        t_seq, t_par = median_tempo(seq), median_tempo(par)
        razao = t_seq / t_par
        fatores[carga] = razao
        linhas.append(f"{CARGA_LABEL[carga]} & {fmt_dec(t_seq, 6)} & {fmt_dec(t_par, 6)} & {fmt_dec(razao, 3)} \\\\")
    write_tex("tab_monothread.tex", linhas)
    return fatores


# ---------------------------------------------------------------------------
# Tabela 9 -- speedup medido decomposto em fator monothread x fator paralelo
#
# Reporta DUAS eficiencias para S_par, porque sao respostas a perguntas
# diferentes:
#   - E_par_T:       S_par / T_arquivo (T nominal da entrada: 4 ou 8).
#                     Cai bastante nas cargas media/grande porque ali T=8
#                     conta as 4 threads logicas de hyperthreading como se
#                     fossem nucleos completos.
#   - E_par_fisico:  S_par / NUM_NUCLEOS_FISICOS (sempre 4, os nucleos fisicos
#                     reais do processador, Tabela 4). E esta a metrica usada
#                     no texto do relatorio (abstract e Secao 8) para afirmar
#                     eficiencia de 93-98%; antes desta correcao, a coluna
#                     unica "E paralela (/T)" usava T_arquivo tambem para
#                     media/grande, o que produzia 0,48/0,49 em vez disso e
#                     ficava inconsistente com o texto.
def tabela_speedup_corrigido(rows, fatores_monothread):
    linhas_tex, linhas_dat = [], []
    for carga in CARGA_ORDEM:
        seq = where(rows, grupo="tempos", carga=carga, binario="fire_seq")
        par = where(rows, grupo="tempos", carga=carga, binario="fire_omp_static")
        if not seq or not par:
            continue
        t_seq, t_par = median_tempo(seq), median_tempo(par)
        T = par[0]["T_arquivo"]
        S_medido = t_seq / t_par

        if carga in fatores_monothread:
            fator = fatores_monothread[carga]
            aprox = ""
        elif "media" in fatores_monothread:
            # sem medição direta para esta carga: extrapola do fator da carga média
            fator = fatores_monothread["media"]
            aprox = "\\approx "
        else:
            linhas_tex.append(f"{CARGA_LABEL[carga]} & {T} & {fmt_dec(S_medido, 2)} & "
                               f"\\TODO{{}} & \\TODO{{}} & \\TODO{{}} & \\TODO{{}} \\\\")
            continue

        S_par = S_medido / fator
        E_par_T = S_par / T
        E_par_fisico = S_par / NUM_NUCLEOS_FISICOS
        fator_str = f"${aprox}{fmt_dec(fator, 3 if not aprox else 2)}$" if aprox else fmt_dec(fator, 3)
        S_par_str = f"$\\approx {fmt_dec(S_par, 2)}$" if aprox else fmt_dec(S_par, 2)
        E_par_T_str = f"$\\approx {fmt_dec(E_par_T, 2)}$" if aprox else fmt_dec(E_par_T, 2)
        E_par_fisico_str = f"$\\approx {fmt_dec(E_par_fisico, 2)}$" if aprox else fmt_dec(E_par_fisico, 2)

        linhas_tex.append(
            f"{CARGA_LABEL[carga]} & {T} & {fmt_dec(S_medido, 2)} & {fator_str} & "
            f"{S_par_str} & {E_par_T_str} & {E_par_fisico_str} \\\\"
        )
        linhas_dat.append(f"{CARGA_LABEL[carga]}\t{S_medido:.2f}\t{S_par:.2f}\t{T}")
    write_tex("tab_speedup_corrigido.tex", linhas_tex)
    write_dat("fig_speedup.dat", "carga\tS_medido\tS_paralelo\tT_ideal", linhas_dat)


# ---------------------------------------------------------------------------
def main():
    rows = load_runs(CSV_PATH)
    print(f"[info] {len(rows)} execuções carregadas de {CSV_PATH.relative_to(ROOT) if CSV_PATH.is_relative_to(ROOT) else CSV_PATH}")

    tabela_tempos(rows)
    tabela_speedup(rows)
    tabela_vazao(rows)
    tabela_threads_det(rows)
    tabela_varredura(rows)
    tabela_schedules(rows)
    fatores = tabela_monothread(rows)
    tabela_speedup_corrigido(rows, fatores)

    print("\nFeito. Os arquivos results/plot/tab_*.tex são pensados para \\input{} dentro dos")
    print("tabulars já existentes em main.tex (entre \\midrule e \\bottomrule),")
    print("e os fig_*.dat para \\addplot table {...} nas figuras 1-3.")


if __name__ == "__main__":
    main()