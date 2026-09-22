#!/usr/bin/env python3
"""
scripts/process_results.py: TB1 SSC0903 (fire_seq / fire_omp)

Lê results/raw/runs.csv (gerado por scripts/run_benchmarks.sh), aplica a
mediana como filtro estatístico sobre as repetições de cada configuração, e
gera em results/plot/:

  Tabelas (.tex — só as linhas de corpo, para \\input{} no main.tex):
    tab_tempos.tex             tempo seq / par T-nativo / par T=1
    tab_speedup.tex            S_abs, E_abs, S_rel, E_rel por carga
    tab_vazao.tex              vazão em M atualizações/s
    tab_threads_det.tex        independência do número de threads, carga média
    tab_varredura.tex          tempo/S_abs/E_abs/S_rel/E_rel/e(T) vs T, carga grande
    tab_schedules.tex          tempo x schedule x T, carga grande
    tab_monothread.tex         isolamento monothread (taskset -c 0)
    tab_speedup_corrigido.tex  decomposição S_abs = fator_monothread × S_rel

  Dados para pgfplots (.dat — tab-separated):
    fig_tempos_seq.dat / fig_tempos_par.dat / fig_tempos_par_T1.dat
    fig_vazao.dat
    fig_varredura.dat          T, tempo, S_abs, E_abs, S_rel, E_rel
    fig_schedules.dat
    fig_speedup.dat            S_abs e S_rel por carga

Métricas:
  Speedup absoluto  S_abs = Tseq / Tpar_p   (melhor algoritmo seq vs par com p threads)
  Speedup relativo  S_rel = Tpar_1 / Tpar_p  (par com 1 thread vs par com p threads)
  Eficiência        E = Sp / p
  Karp-Flatt        e(T) = (T/S_abs - 1) / (T - 1),  T >= 2

Uso:
    python3 scripts/process_results.py [caminho/para/runs.csv]
"""
import csv
import math
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CSV_PATH = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "results" / "raw" / "runs.csv"
PLOT_DIR = ROOT / "results" / "plot"

CARGA_ORDEM = ["pequena", "media", "grande"]
CARGA_LABEL = {"pequena": "Pequena", "media": "Média", "grande": "Grande"}

# T nativo de cada arquivo de carga (campo T da primeira linha do arquivo de entrada)
CARGA_T_NATIVE = {"pequena": 4, "media": 8, "grande": 8}

# Núcleos físicos do processador usado nos experimentos (Intel Core i7-4790)
NUM_NUCLEOS_FISICOS = 4

NUM_FIELDS = {
    "T_arquivo", "rep", "passos", "nao_combustiveis", "intactas", "em_chamas",
    "queimadas", "contencao", "total_ignicoes", "pico_passo", "pico_qtd",
    "percentual_queimado", "percentual_protegido", "tempo",
}

T_VALORES_PADRAO   = (1, 2, 4, 8, 16)
T_VALORES_SCHEDULES = (2, 4, 8, 16)


# ---------------------------------------------------------------------------
# Leitura e utilitários
def load_runs(path):
    if not path.exists():
        sys.exit(f"[erro] arquivo não encontrado: {path}\n"
                  "       rode scripts/run_benchmarks.sh antes de processar.")
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
    campos = ("checksum", "total_ignicoes", "pico_passo", "pico_qtd")
    valores = {c: {r[c] for r in rows} for c in campos}
    for c, vs in valores.items():
        if len(vs) > 1:
            print(f"[aviso] {chave_grupo}: campo '{c}' variou ({vs}) -- não-determinismo!",
                  file=sys.stderr)


def checar_seq_par(rows):
    """Confirma que fire_seq e fire_omp_static produzem saída idêntica (exceto tempo)."""
    for carga in CARGA_ORDEM:
        seq = where(rows, grupo="tempos", carga=carga, binario="fire_seq")
        par = where(rows, grupo="tempos", carga=carga, binario="fire_omp_static")
        if not seq or not par:
            continue
        if {r["checksum"] for r in seq} != {r["checksum"] for r in par}:
            print(f"[aviso] tempos/{carga}: checksum seq != par -- versões divergem!", file=sys.stderr)
        for campo in ("total_ignicoes", "pico_passo", "pico_qtd"):
            if {r[campo] for r in seq} != {r[campo] for r in par}:
                print(f"[aviso] tempos/{carga}: campo '{campo}' difere entre seq e par", file=sys.stderr)


def carga_stats(rows, carga):
    """Agrega tempos medianos de seq, par nativo e par T=1 para uma carga.

    Retorna dict com chaves: t_seq, t_par, t_par_T1 (None se não houver), T, L, C, passos.
    t_par_T1 é o baseline do speedup relativo (Tpar_1/Tpar_p dos slides).
    """
    T_native = CARGA_T_NATIVE[carga]
    seq = where(rows, grupo="tempos", carga=carga, binario="fire_seq")
    all_par = where(rows, grupo="tempos", carga=carga, binario="fire_omp_static")
    par = where(all_par, T_arquivo=T_native)
    par_T1 = where(all_par, T_arquivo=1)

    if not seq or not par:
        return None

    t_seq    = median_tempo(seq)
    t_par    = median_tempo(par)
    t_par_T1 = median_tempo(par_T1) if par_T1 else None
    L, C     = get_lc(par[0]["entrada"])
    passos   = par[0]["passos"]
    return {"t_seq": t_seq, "t_par": t_par, "t_par_T1": t_par_T1,
            "T": T_native, "L": L, "C": C, "passos": passos}


def fmt_int(n):
    return f"{int(round(n)):,}".replace(",", "\\,")


def fmt_dec(x, casas=2):
    return f"{x:.{casas}f}".replace(".", "{,}")


def fmt_pico(passo, qtd):
    return f"{int(passo)}\\ \\ {fmt_int(qtd)}"


_lc_cache = {}

def get_lc(entrada_rel_path):
    if entrada_rel_path in _lc_cache:
        return _lc_cache[entrada_rel_path]
    path = ROOT / entrada_rel_path
    if not path.exists():
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
# Tabela: tempos base + dados para figura
def tabela_tempos(rows):
    linhas_tex, linhas_dat_seq, linhas_dat_par, linhas_dat_par_T1 = [], [], [], []
    for carga in CARGA_ORDEM:
        stats = carga_stats(rows, carga)
        if stats is None:
            print(f"[aviso] tempos: faltam dados para carga={carga}", file=sys.stderr)
            continue
        t_seq, t_par, t_par_T1, T, L, C, passos = (
            stats["t_seq"], stats["t_par"], stats["t_par_T1"],
            stats["T"], stats["L"], stats["C"], stats["passos"]
        )
        atualizacoes_m = round(L * C * passos / 1e6)
        t1_str = fmt_dec(t_par_T1, 6) if t_par_T1 is not None else "--"
        linhas_tex.append(
            f"{CARGA_LABEL[carga]} & {atualizacoes_m}~M & {T} & "
            f"{fmt_dec(t_seq, 6)} & {fmt_dec(t_par, 6)} & {t1_str} \\\\"
        )
        linhas_dat_seq.append(f"{CARGA_LABEL[carga]}\t{t_seq:.6f}")
        linhas_dat_par.append(f"{CARGA_LABEL[carga]}\t{t_par:.6f}")
        linhas_dat_par_T1.append(f"{CARGA_LABEL[carga]}\t{t_par_T1:.6f}" if t_par_T1 else f"{CARGA_LABEL[carga]}\tNaN")
    write_tex("tab_tempos.tex", linhas_tex)
    write_dat("fig_tempos_seq.dat",    "carga\ttempo", linhas_dat_seq)
    write_dat("fig_tempos_par.dat",    "carga\ttempo", linhas_dat_par)
    write_dat("fig_tempos_par_T1.dat", "carga\ttempo", linhas_dat_par_T1)


# ---------------------------------------------------------------------------
# Tabela: speedup absoluto e relativo + eficiências
#
# Sp_abs = Tseq  / Tpar_p  (melhor algoritmo sequencial — slides: "speedup absoluto")
# Sp_rel = Tpar1 / Tpar_p  (versão paralela em 1 thread — slides: "speedup relativo")
# Ep     = Sp / p
def tabela_speedup(rows):
    linhas = []
    for carga in CARGA_ORDEM:
        stats = carga_stats(rows, carga)
        if stats is None:
            continue
        t_seq, t_par, t_par_T1, T = (
            stats["t_seq"], stats["t_par"], stats["t_par_T1"], stats["T"]
        )
        S_abs = t_seq / t_par
        E_abs = S_abs / T
        if t_par_T1 is not None:
            S_rel = t_par_T1 / t_par
            E_rel = S_rel / T
            rel_str = f"{fmt_dec(S_rel, 3)} & {fmt_dec(E_rel, 3)}"
        else:
            rel_str = "-- & --"
        linhas.append(
            f"{CARGA_LABEL[carga]} & {T} "
            f"& {fmt_dec(S_abs, 3)} & {fmt_dec(E_abs, 3)} & {rel_str} \\\\"
        )
    write_tex("tab_speedup.tex", linhas)


# ---------------------------------------------------------------------------
# Tabela: vazão (M atualizações/s) + dados para figura
def tabela_vazao(rows):
    linhas_tex, linhas_dat = [], []
    for carga in CARGA_ORDEM:
        stats = carga_stats(rows, carga)
        if stats is None:
            continue
        t_seq, t_par, T, L, C, passos = (
            stats["t_seq"], stats["t_par"], stats["T"], stats["L"], stats["C"], stats["passos"]
        )
        celulas_m     = L * C * passos / 1e6
        vaz_seq       = celulas_m / t_seq
        vaz_par       = celulas_m / t_par
        vaz_par_thread = vaz_par / T
        linhas_tex.append(
            f"{CARGA_LABEL[carga]} & {T} & {fmt_dec(vaz_seq, 1)} & "
            f"{fmt_dec(vaz_par, 1)} & {fmt_dec(vaz_par_thread, 1)} \\\\"
        )
        linhas_dat.append(f"{CARGA_LABEL[carga]}\t{vaz_seq:.2f}\t{vaz_par_thread:.2f}")
    write_tex("tab_vazao.tex", linhas_tex)
    write_dat("fig_vazao.dat", "carga\tsequencial\tparalela_por_thread", linhas_dat)


# ---------------------------------------------------------------------------
# Tabela: independência do número de threads (carga média)
def tabela_threads_det(rows):
    linhas = []
    for T in T_VALORES_PADRAO:
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

    checksums = {r["T_arquivo"]: r["checksum"] for r in where(rows, grupo="threads_det", carga="media")}
    if len(set(checksums.values())) > 1:
        print(f"[aviso] threads_det: checksum difere entre T: {checksums}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Tabela: varredura de T (carga grande)
# Colunas: T | Tempo | S_abs | E_abs | S_rel | E_rel | e(T)
def tabela_varredura(rows):
    stats_grande = carga_stats(rows, "grande")
    if stats_grande is None:
        print("[erro] varredura_T: falta tempo seq da carga grande (grupo 'tempos')", file=sys.stderr)
        return
    t_seq = stats_grande["t_seq"]

    # T=1 do grupo varredura: baseline do speedup relativo na varredura
    grupo_T1 = where(rows, grupo="varredura_T", carga="grande", T_arquivo=1)
    t_par_T1 = median_tempo(grupo_T1) if grupo_T1 else None

    linhas, linhas_dat = [], []
    for T in T_VALORES_PADRAO:
        grupo = where(rows, grupo="varredura_T", carga="grande", T_arquivo=T)
        if not grupo:
            linhas.append(f"{T}  & \\TODO{{}} & \\TODO{{}} & \\TODO{{}} & \\TODO{{}} & \\TODO{{}} & \\TODO{{}} \\\\")
            continue
        check_determinism(grupo, f"varredura_T T={T}")
        t_par = median_tempo(grupo)

        S_abs = t_seq / t_par
        E_abs = S_abs / T
        e_kf  = fmt_dec((T / S_abs - 1) / (T - 1), 2) if T > 1 else "--"

        if t_par_T1 is not None:
            S_rel = t_par_T1 / t_par
            E_rel = S_rel / T
            s_rel_str = fmt_dec(S_rel, 2)
            e_rel_str = fmt_dec(E_rel, 2)
            s_rel_dat = f"{S_rel:.4f}"
            e_rel_dat = f"{E_rel:.4f}"
        else:
            s_rel_str = "--"
            e_rel_str = "--"
            s_rel_dat = "NaN"
            e_rel_dat = "NaN"

        linhas.append(
            f"{T}{' ' if T >= 10 else '  '}"
            f"& {fmt_dec(t_par, 6)} "
            f"& {fmt_dec(S_abs, 2)} & {fmt_dec(E_abs, 2)} "
            f"& {s_rel_str} & {e_rel_str} "
            f"& {e_kf} \\\\"
        )
        linhas_dat.append(f"{T}\t{t_par:.6f}\t{S_abs:.4f}\t{E_abs:.4f}\t{s_rel_dat}\t{e_rel_dat}")

    write_tex("tab_varredura.tex", linhas)
    write_dat("fig_varredura.dat",
              "T\ttempo\tspeedup_abs\teficiencia_abs\tspeedup_rel\teficiencia_rel",
              linhas_dat)

    checksums = {r["T_arquivo"]: r["checksum"] for r in where(rows, grupo="varredura_T", carga="grande")}
    if len(set(checksums.values())) > 1:
        print(f"[aviso] varredura_T: checksum difere entre T: {checksums}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Tabela: comparação de schedules (carga grande)
def tabela_schedules(rows):
    schedules = ["static", "static_1024", "dynamic_1024", "guided"]
    schedule_label = {
        "static": "static",
        "static_1024": "static, 1024",
        "dynamic_1024": "dynamic, 1024",
        "guided": "guided",
    }
    linhas = []
    dat_por_T = {T: [] for T in T_VALORES_SCHEDULES}
    checksums_todos = {}
    for sc in schedules:
        celulas = []
        for T in T_VALORES_SCHEDULES:
            grupo = where(rows, grupo="schedules", carga="grande", schedule=sc, T_arquivo=T)
            if grupo:
                check_determinism(grupo, f"schedules {sc} T={T}")
                checksums_todos[(sc, T)] = grupo[0]["checksum"]
                t = median_tempo(grupo)
                celulas.append(fmt_dec(t, 6))
                dat_por_T[T].append(f"{t:.6f}")
            else:
                celulas.append("\\TODO{}")
                dat_por_T[T].append("NaN")
        linhas.append(f"\\texttt{{{schedule_label[sc]}}} & " + " & ".join(celulas) + " \\\\")
    write_tex("tab_schedules.tex", linhas)

    linhas_dat = [f"{T}\t" + "\t".join(dat_por_T[T]) for T in T_VALORES_SCHEDULES]
    write_dat("fig_schedules.dat", "T\t" + "\t".join(schedules), linhas_dat)

    if len(set(checksums_todos.values())) > 1:
        print(f"[aviso] schedules: checksum difere entre schedule/T: {checksums_todos}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Tabela: isolamento monothread (taskset -c 0, sem paralelismo real)
# Mede o overhead puro do framework OpenMP com T=1.
def tabela_monothread(rows):
    linhas = []
    fatores = {}
    for carga in ("pequena", "media", "grande"):
        seq = where(rows, grupo="monothread", carga=carga, binario="fire_seq")
        par = where(rows, grupo="monothread", carga=carga, binario="fire_omp_static", T_arquivo=1)
        if not seq or not par:
            continue
        t_seq_pin, t_par_pin = median_tempo(seq), median_tempo(par)
        razao = t_seq_pin / t_par_pin
        fatores[carga] = razao
        linhas.append(
            f"{CARGA_LABEL[carga]} & {fmt_dec(t_seq_pin, 6)} & "
            f"{fmt_dec(t_par_pin, 6)} & {fmt_dec(razao, 3)} \\\\"
        )
    write_tex("tab_monothread.tex", linhas)
    return fatores


# ---------------------------------------------------------------------------
# Tabela: decomposição S_abs = fator_monothread × S_rel
#
# fator_monothread = Tseq / Tpar_1  (quanto a versão paralela "ganha" serialmente)
# S_rel            = Tpar_1 / Tpar_p (speedup relativo dos slides)
# S_abs            = fator_monothread × S_rel
#
# Prioridade para fator_monothread:
#   1) medido diretamente via t_par_T1 do grupo "tempos" (unpinned)
#   2) fallback: fatores_monothread do grupo "monothread" (pinned, taskset -c 0)
#   3) extrapolação da carga media (marcado com ≈)
def tabela_speedup_corrigido(rows, fatores_monothread):
    linhas_tex, linhas_dat = [], []
    for carga in CARGA_ORDEM:
        stats = carga_stats(rows, carga)
        if stats is None:
            continue
        t_seq, t_par, t_par_T1, T = (
            stats["t_seq"], stats["t_par"], stats["t_par_T1"], stats["T"]
        )
        S_abs = t_seq / t_par

        if t_par_T1 is not None:
            # Fonte primária: unpinned T=1 do grupo tempos
            fator_mon = t_seq / t_par_T1
            S_rel     = t_par_T1 / t_par
            aprox     = ""
        elif carga in fatores_monothread:
            # Fallback: pinned (taskset -c 0) do grupo monothread
            fator_mon = fatores_monothread[carga]
            S_rel     = S_abs / fator_mon
            aprox     = ""
        elif "media" in fatores_monothread:
            # Extrapolação da carga média
            fator_mon = fatores_monothread["media"]
            S_rel     = S_abs / fator_mon
            aprox     = "\\approx "
        else:
            linhas_tex.append(
                f"{CARGA_LABEL[carga]} & {T} & {fmt_dec(S_abs, 2)} & -- & -- & -- & -- \\\\"
            )
            continue

        E_rel_T      = S_rel / T
        E_rel_fisico = S_rel / NUM_NUCLEOS_FISICOS

        fator_str   = fmt_dec(fator_mon, 3)
        s_rel_str   = f"$\\approx {fmt_dec(S_rel, 2)}$" if aprox else fmt_dec(S_rel, 2)
        e_T_str     = f"$\\approx {fmt_dec(E_rel_T, 2)}$" if aprox else fmt_dec(E_rel_T, 2)
        e_fis_str   = f"$\\approx {fmt_dec(E_rel_fisico, 2)}$" if aprox else fmt_dec(E_rel_fisico, 2)

        linhas_tex.append(
            f"{CARGA_LABEL[carga]} & {T} & {fmt_dec(S_abs, 2)} & {fator_str} & "
            f"{s_rel_str} & {e_T_str} & {e_fis_str} \\\\"
        )
        linhas_dat.append(f"{CARGA_LABEL[carga]}\t{S_abs:.2f}\t{S_rel:.2f}\t{T}")

    write_tex("tab_speedup_corrigido.tex", linhas_tex)
    write_dat("fig_speedup.dat", "carga\tS_abs\tS_rel\tT_ideal", linhas_dat)


# ---------------------------------------------------------------------------
def main():
    rows = load_runs(CSV_PATH)
    print(f"[info] {len(rows)} execuções carregadas de "
          f"{CSV_PATH.relative_to(ROOT) if CSV_PATH.is_relative_to(ROOT) else CSV_PATH}")

    checar_seq_par(rows)

    tabela_tempos(rows)
    tabela_speedup(rows)
    tabela_vazao(rows)
    tabela_threads_det(rows)
    tabela_varredura(rows)
    tabela_schedules(rows)
    fatores = tabela_monothread(rows)
    tabela_speedup_corrigido(rows, fatores)

    print("\nFeito. Arquivos em results/plot/:")
    print("  tab_*.tex  → \\input{} entre \\midrule e \\bottomrule no main.tex")
    print("  fig_*.dat  → \\addplot table {...} nas figuras pgfplots")
    print("\nNota: tab_speedup e tab_varredura agora têm colunas extras (S_rel, E_rel).")
    print("Atualize os cabeçalhos das tabelas correspondentes no main.tex.")


if __name__ == "__main__":
    main()
