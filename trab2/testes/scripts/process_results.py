#!/usr/bin/env python3
"""
testes/scripts/process_results.py: TB2 SSC0903 (fire_seq / fire_mpi_omp)

Lê testes/resultados/runs.csv e testes/resultados/ambiente.txt (gerados por
testes/scripts/run_benchmarks.sh), aplica a mediana como filtro estatístico sobre
as repetições de cada configuração, e gera em relatorio/plot/ tudo que o main.tex
carrega com \\input{}:

  Tabelas completas (.tex, com o ambiente tabular inteiro):
    tab_validacao.tex            saída do sequencial por carga e conferência com a paralela
    tab_independencia.tex        independência de NPrc e T (carga intermediária)
    tab_ambiente.tex             ambiente experimental
    tab_resumo.tex               tempos e speedups de cada carga
    tab_tempos_<carga>.tex       tempo mediano, NPrc x T
    tab_speedup_<carga>.tex      speedup absoluto, NPrc x T
    tab_eficiencia_<carga>.tex   eficiência, NPrc x T
    tab_escalabilidade.tex       melhor configuração por total de núcleos (carga de referência)
    tab_hibrido.tex              configurações com o mesmo total de núcleos (carga de referência)

  Figuras completas (.tex, com o tikzpicture inteiro):
    fig_tempos.tex               tempo sequencial x paralelo por carga
    fig_speedup.tex              speedup x total de núcleos, uma curva por T
    fig_eficiencia.tex           eficiência x total de núcleos, uma curva por T
    fig_hibrido.tex              tempo das configurações com o mesmo total de núcleos

  Valores citados no texto:
    valores.tex                  um \\DefValor{chave}{valor} por número usado na análise

Métricas:
  p                 = NPrc x T (total de núcleos lógicos pedidos pela configuração)
  Speedup absoluto  S_abs = Tseq / Tpar(NPrc, T)
  Speedup relativo  S_rel = Tpar(1, 1) / Tpar(NPrc, T)
  Eficiência        E = S / p
  Karp-Flatt        e(p) = (1/S_abs - 1/p) / (1 - 1/p),  p >= 2

Uso:
    python3 testes/scripts/process_results.py [caminho/para/runs.csv]
"""
import csv
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
RESULTADOS_DIR = ROOT / "testes" / "resultados"
CSV_PATH = Path(sys.argv[1]) if len(sys.argv) > 1 else RESULTADOS_DIR / "runs.csv"
AMBIENTE_PATH = CSV_PATH.parent / "ambiente.txt"
PLOT_DIR = ROOT / "relatorio" / "plot"

# Nome de exibição das cargas conhecidas, as demais aparecem com a primeira letra maiúscula
CARGA_LABEL = {"pequena": "Pequena", "media": "Média", "grande": "Grande"}

# Campos que precisam ser idênticos entre a versão sequencial e a paralela (todos menos o tempo)
CAMPOS_RESULTADO = (
    "passos", "nao_combustiveis", "intactas", "em_chamas", "queimadas", "contencao",
    "total_ignicoes", "pico_passo", "pico_qtd", "percentual_queimado", "percentual_protegido", "checksum",
)

CAMPOS_INTEIROS = (
    "np", "t", "rep", "passos", "nao_combustiveis", "intactas", "em_chamas", "queimadas", "contencao",
    "total_ignicoes", "pico_passo", "pico_qtd", "checksum",
)


# ---------------------------------------------------------------------------
# Leitura e utilitários
def load_runs(path: Path) -> list[dict]:
    """
    Carrega o CSV de execuções e converte os campos numéricos.
    """
    if not path.exists():
        sys.exit(f"[erro] arquivo não encontrado: {path}\n"
                 "       rode testes/scripts/run_benchmarks.sh antes de processar.")
    with open(path, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for campo in CAMPOS_INTEIROS:
            r[campo] = int(r[campo])
        r["tempo"] = float(r["tempo"])
    if not rows:
        sys.exit(f"[erro] {path} não tem nenhuma execução.")
    return rows


def load_ambiente(path: Path) -> dict[str, str]:
    """
    Carrega o arquivo chave=valor com a descrição do ambiente de execução.
    """
    ambiente = {}
    if not path.exists():
        print(f"[aviso] {path} não encontrado, a tabela de ambiente fica incompleta", file=sys.stderr)
        return ambiente
    with open(path, encoding="utf-8") as f:
        for linha in f:
            if "=" in linha:
                chave, valor = linha.rstrip("\n").split("=", 1)
                ambiente[chave] = valor.strip()
    return ambiente


def where(rows: list[dict], **filters) -> list[dict]:
    """
    Filtra as execuções pelos pares campo=valor informados.
    """
    out = rows
    for k, v in filters.items():
        out = [r for r in out if r[k] == v]
    return out


def median_tempo(rows: list[dict]) -> float | None:
    """
    Devolve a mediana do tempo das execuções, ou None se a lista estiver vazia.
    """
    if not rows:
        return None
    return statistics.median(r["tempo"] for r in rows)


def carga_label(carga: str) -> str:
    """
    Devolve o nome de exibição de uma carga.
    """
    return CARGA_LABEL.get(carga, carga.replace("_", " ").capitalize())


def fmt_int(n: float) -> str:
    """
    Formata um inteiro com separador de milhar no padrão do relatório.
    """
    return f"{int(round(n)):,}".replace(",", "\\,")


def fmt_dec(x: float, casas: int = 2) -> str:
    """
    Formata um decimal com vírgula, no padrão do relatório.
    """
    return f"{x:.{casas}f}".replace(".", "{,}")


def tex_escape(texto: str) -> str:
    """
    Escapa os caracteres especiais do LaTeX em um texto vindo do ambiente.
    """
    trocas = {"\\": "\\textbackslash{}", "&": "\\&", "%": "\\%", "$": "\\$", "#": "\\#", "_": "\\_",
              "{": "\\{", "}": "\\}", "~": "\\textasciitilde{}", "^": "\\textasciicircum{}"}
    return "".join(trocas.get(ch, ch) for ch in texto)


def write_file(nome: str, linhas: list[str]) -> None:
    """
    Grava um arquivo em relatorio/plot/.
    """
    PLOT_DIR.mkdir(parents=True, exist_ok=True)
    destino = PLOT_DIR / nome
    with open(destino, "w", encoding="utf-8") as f:
        f.write("\n".join(linhas) + "\n")
    print(f"[ok] {destino.relative_to(ROOT)}")


# ---------------------------------------------------------------------------
# Agregação: uma estrutura por carga, com os tempos medianos de cada configuração
def carga_stats(rows: list[dict], carga: str) -> dict:
    """
    Agrega as execuções de uma carga.

    Returns:
        Dicionário com t_seq, tempos[(np, t)], t_par_11, dimensões, número de passos,
        atualizações de célula, a melhor configuração e as listas de NPrc e T medidos.
    """
    seq = where(rows, versao="seq", carga=carga)
    par = where(rows, versao="mpi_omp", carga=carga)

    configs = sorted({(r["np"], r["t"]) for r in par})
    tempos = {cfg: median_tempo(where(par, np=cfg[0], t=cfg[1])) for cfg in configs}

    with open(ROOT / (seq or par)[0]["entrada"], encoding="utf-8") as f:
        primeira = f.readline().split()
    L, C = int(primeira[0]), int(primeira[1])
    passos = (seq or par)[0]["passos"]

    melhor = min(tempos, key=tempos.get) if tempos else None
    return {
        "carga": carga,
        "seq": seq,
        "par": par,
        "t_seq": median_tempo(seq),
        "tempos": tempos,
        "t_par_11": tempos.get((1, 1)),
        "L": L,
        "C": C,
        "passos": passos,
        "atualizacoes": L * C * passos,
        "melhor": melhor,
        "nps": sorted({cfg[0] for cfg in configs}),
        "ts": sorted({cfg[1] for cfg in configs}),
    }


def speedup_abs(stats: dict, cfg: tuple[int, int]) -> float | None:
    """
    Speedup absoluto de uma configuração, em relação ao sequencial do trabalho 1.
    """
    if stats["t_seq"] is None or cfg not in stats["tempos"]:
        return None
    return stats["t_seq"] / stats["tempos"][cfg]


def speedup_rel(stats: dict, cfg: tuple[int, int]) -> float | None:
    """
    Speedup relativo de uma configuração, em relação à própria versão paralela com 1 processo e 1 thread.
    """
    if stats["t_par_11"] is None or cfg not in stats["tempos"]:
        return None
    return stats["t_par_11"] / stats["tempos"][cfg]


def karp_flatt(speedup: float, p: int) -> float | None:
    """
    Fração serial determinada experimentalmente (métrica de Karp-Flatt).
    """
    if p <= 1:
        return None
    return (1 / speedup - 1 / p) / (1 - 1 / p)


def resultado_identico(stats: dict) -> bool:
    """
    Confere se todas as execuções paralelas da carga têm os mesmos campos de resultado do sequencial.
    """
    if not stats["seq"] or not stats["par"]:
        return False
    referencia = tuple(str(stats["seq"][0][c]) for c in CAMPOS_RESULTADO)
    todas = stats["seq"] + stats["par"]
    return all(tuple(str(r[c]) for c in CAMPOS_RESULTADO) == referencia for r in todas)


# ---------------------------------------------------------------------------
# Tabela: saída por carga e conferência sequencial x paralela
def tabela_validacao(todas: list[dict]) -> None:
    linhas = ["\\begin{tabular}{@{}l" + "r" * len(todas) + "@{}}", "\\toprule",
              "Campo & " + " & ".join(carga_label(s["carga"]) for s in todas) + " \\\\", "\\midrule"]

    def linha(rotulo: str, formatador) -> str:
        return f"\\texttt{{{rotulo}}} & " + " & ".join(formatador(s["seq"][0]) for s in todas) + " \\\\"

    for campo in ("passos", "nao_combustiveis", "intactas", "em_chamas", "queimadas", "contencao", "total_ignicoes"):
        linhas.append(linha(campo.replace("_", "\\_"), lambda r, c=campo: fmt_int(r[c])))
    linhas.append(linha("pico\\_ignicoes", lambda r: f"{r['pico_passo']}\\ \\ {fmt_int(r['pico_qtd'])}"))
    linhas.append(linha("percentual\\_queimado", lambda r: r["percentual_queimado"].replace(".", "{,}")))
    linhas.append(linha("percentual\\_protegido", lambda r: r["percentual_protegido"].replace(".", "{,}")))
    linhas.append(linha("checksum", lambda r: str(r["checksum"])))
    linhas.append("\\midrule")
    linhas.append("Configurações paralelas & " + " & ".join(str(len(s["tempos"])) for s in todas) + " \\\\")
    linhas.append("Execuções paralelas & " + " & ".join(str(len(s["par"])) for s in todas) + " \\\\")
    linhas.append("Resultado & " + " & ".join(
        "\\textbf{[OK]}" if resultado_identico(s) else "\\textbf{[FALHA]}" for s in todas) + " \\\\")
    linhas += ["\\bottomrule", "\\end{tabular}"]
    write_file("tab_validacao.tex", linhas)

    for s in todas:
        if not resultado_identico(s):
            print(f"[aviso] carga {s['carga']}: alguma execução paralela difere do sequencial!", file=sys.stderr)


# ---------------------------------------------------------------------------
# Tabela: independência do número de processos e de threads
def tabela_independencia(stats: dict) -> None:
    linhas = ["\\begin{tabular}{@{}rrrll@{}}", "\\toprule",
              "NPrc & $T$ & Total ignições & Pico & Checksum \\\\", "\\midrule"]
    for cfg in sorted(stats["tempos"]):
        grupo = where(stats["par"], np=cfg[0], t=cfg[1])
        valores = {(r["total_ignicoes"], r["pico_passo"], r["pico_qtd"], r["checksum"]) for r in grupo}
        if len(valores) > 1:
            print(f"[aviso] independência NPrc={cfg[0]} T={cfg[1]}: repetições divergem!", file=sys.stderr)
        r = grupo[0]
        linhas.append(f"{cfg[0]} & {cfg[1]} & {r['total_ignicoes']} & {r['pico_passo']} {r['pico_qtd']} & {r['checksum']} \\\\")
    linhas += ["\\bottomrule", "\\end{tabular}"]
    write_file("tab_independencia.tex", linhas)


# ---------------------------------------------------------------------------
# Tabela: ambiente experimental
def tabela_ambiente(ambiente: dict[str, str]) -> None:
    def valor(chave: str) -> str:
        return tex_escape(ambiente.get(chave, "")) or "\\TODO{preencher}"

    nucleos = f"{valor('nucleos_fisicos')} físicos / {valor('cpus_logicas')} lógicos ({valor('threads_por_nucleo')} \\emph{{thread}}(s) por núcleo)"
    caches = f"L1d {valor('cache_l1d')}, L2 {valor('cache_l2')}, L3 {valor('cache_l3')}"
    itens = [
        ("Nós utilizados", valor("num_nos")),
        ("Modelo do processador (por nó)", valor("cpu_modelo")),
        ("Núcleos por nó", nucleos),
        ("Cache", caches),
        ("Memória RAM (por nó)", valor("memoria")),
        ("Sistema operacional", valor("sistema")),
        ("Compilador (\\texttt{mpicc})", valor("compilador")),
        ("Implementação MPI", f"{valor('mpi')} (padrão MPI {valor('mpi_padrao')})"),
        ("Especificação OpenMP suportada", f"\\texttt{{\\_OPENMP}} \\texttt{{{valor('openmp')}}}"),
        ("\\emph{Flags} de compilação", f"\\texttt{{{valor('flags')}}}"),
        ("\\emph{Flags} do \\texttt{mpirun}", f"\\texttt{{{valor('mpirun_flags')}}}"),
        ("Repetições por configuração", f"{valor('reps')} (mediana)"),
        ("Data das execuções", valor("data")),
    ]
    linhas = ["\\begin{tabular}{@{}lp{0.62\\textwidth}@{}}", "\\toprule", "Item & Valor \\\\", "\\midrule"]
    linhas += [f"{item} & {conteudo} \\\\" for item, conteudo in itens]
    linhas += ["\\bottomrule", "\\end{tabular}"]
    write_file("tab_ambiente.tex", linhas)


# ---------------------------------------------------------------------------
# Tabelas NPrc x T: tempo, speedup absoluto e eficiência de uma carga
def tabela_matriz(stats: dict, nome: str, calcula, formata, melhor_e_maior: bool) -> None:
    valores = {cfg: calcula(cfg) for cfg in stats["tempos"]}
    validos = [v for v in valores.values() if v is not None]
    destaque = (max(validos) if melhor_e_maior else min(validos)) if validos else None

    linhas = ["\\begin{tabular}{@{}r" + "r" * len(stats["ts"]) + "@{}}", "\\toprule",
              "NPrc & " + " & ".join(f"$T={t}$" for t in stats["ts"]) + " \\\\", "\\midrule"]
    for np in stats["nps"]:
        celulas = []
        for t in stats["ts"]:
            v = valores.get((np, t))
            if v is None:
                celulas.append("--")
            elif v == destaque:
                celulas.append(f"\\textbf{{{formata(v)}}}")
            else:
                celulas.append(formata(v))
        linhas.append(f"{np} & " + " & ".join(celulas) + " \\\\")
    linhas += ["\\bottomrule", "\\end{tabular}"]
    write_file(nome, linhas)


def tabelas_por_carga(stats: dict) -> None:
    carga = stats["carga"]
    tabela_matriz(stats, f"tab_tempos_{carga}.tex", lambda cfg: stats["tempos"][cfg], lambda v: fmt_dec(v, 6), False)
    tabela_matriz(stats, f"tab_speedup_{carga}.tex", lambda cfg: speedup_abs(stats, cfg), lambda v: fmt_dec(v, 2), True)
    tabela_matriz(stats, f"tab_eficiencia_{carga}.tex", lambda cfg: speedup_abs(stats, cfg) / (cfg[0] * cfg[1]),
                  lambda v: fmt_dec(v, 2), True)


# ---------------------------------------------------------------------------
# Tabela: resumo por carga (sequencial, paralela 1x1 e melhor configuração)
def tabela_resumo(todas: list[dict]) -> None:
    linhas = ["\\begin{tabular}{@{}lrrrcrrrr@{}}", "\\toprule",
              "Carga & Atualizações & Sequencial (s) & Paralela $1{\\times}1$ (s) & Melhor NPrc${\\times}T$ & "
              "Melhor (s) & $S_\\text{abs}$ & $S_\\text{rel}$ & $E_\\text{abs}$ \\\\", "\\midrule"]
    for s in todas:
        cfg = s["melhor"]
        s_rel = speedup_rel(s, cfg)
        linhas.append(
            f"{carga_label(s['carga'])} & {round(s['atualizacoes'] / 1e6)}~M & {fmt_dec(s['t_seq'], 6)} & "
            f"{fmt_dec(s['t_par_11'], 6) if s['t_par_11'] is not None else '--'} & ${cfg[0]}{{\\times}}{cfg[1]}$ & "
            f"{fmt_dec(s['tempos'][cfg], 6)} & {fmt_dec(speedup_abs(s, cfg), 2)} & "
            f"{fmt_dec(s_rel, 2) if s_rel is not None else '--'} & {fmt_dec(speedup_abs(s, cfg) / (cfg[0] * cfg[1]), 2)} \\\\"
        )
    linhas += ["\\bottomrule", "\\end{tabular}"]
    write_file("tab_resumo.tex", linhas)


# ---------------------------------------------------------------------------
# Tabela: melhor configuração para cada total de núcleos p = NPrc x T
def melhores_por_p(stats: dict) -> dict[int, tuple[int, int]]:
    """
    Para cada total de núcleos p, devolve a configuração (NPrc, T) de menor tempo.
    """
    melhores = {}
    for cfg, tempo in stats["tempos"].items():
        p = cfg[0] * cfg[1]
        if p not in melhores or tempo < stats["tempos"][melhores[p]]:
            melhores[p] = cfg
    return dict(sorted(melhores.items()))


def tabela_escalabilidade(stats: dict) -> None:
    linhas = ["\\begin{tabular}{@{}rcrrrrrr@{}}", "\\toprule",
              "$p$ & NPrc${\\times}T$ & Tempo (s) & $S_\\text{abs}$ & $E_\\text{abs}$ & $S_\\text{rel}$ & $E_\\text{rel}$ & $e(p)$ \\\\",
              "\\midrule"]
    for p, cfg in melhores_por_p(stats).items():
        s_abs = speedup_abs(stats, cfg)
        s_rel = speedup_rel(stats, cfg)
        e_kf = karp_flatt(s_abs, p)
        linhas.append(
            f"{p} & ${cfg[0]}{{\\times}}{cfg[1]}$ & {fmt_dec(stats['tempos'][cfg], 6)} & {fmt_dec(s_abs, 2)} & {fmt_dec(s_abs / p, 2)} & "
            f"{fmt_dec(s_rel, 2) if s_rel is not None else '--'} & {fmt_dec(s_rel / p, 2) if s_rel is not None else '--'} & "
            f"{fmt_dec(e_kf, 3) if e_kf is not None else '--'} \\\\"
        )
    linhas += ["\\bottomrule", "\\end{tabular}"]
    write_file("tab_escalabilidade.tex", linhas)


# ---------------------------------------------------------------------------
# Tabela e figura: configurações híbridas com o mesmo total de núcleos
def grupos_hibridos(stats: dict) -> dict[int, list[tuple[int, int]]]:
    """
    Agrupa as configurações por total de núcleos, mantendo só os totais com mais de uma combinação.
    """
    grupos = {}
    for cfg in sorted(stats["tempos"]):
        grupos.setdefault(cfg[0] * cfg[1], []).append(cfg)
    return {p: cfgs for p, cfgs in sorted(grupos.items()) if len(cfgs) > 1}


def tabela_hibrido(stats: dict) -> None:
    linhas = ["\\begin{tabular}{@{}rrrrrr@{}}", "\\toprule",
              "$p$ & NPrc & $T$ & Tempo (s) & $S_\\text{abs}$ & $E_\\text{abs}$ \\\\", "\\midrule"]
    grupos = grupos_hibridos(stats)
    for indice, (p, cfgs) in enumerate(grupos.items()):
        if indice > 0:
            linhas.append("\\addlinespace")
        menor = min(stats["tempos"][cfg] for cfg in cfgs)
        for cfg in cfgs:
            tempo = stats["tempos"][cfg]
            tempo_tex = f"\\textbf{{{fmt_dec(tempo, 6)}}}" if tempo == menor else fmt_dec(tempo, 6)
            s_abs = speedup_abs(stats, cfg)
            linhas.append(f"{p} & {cfg[0]} & {cfg[1]} & {tempo_tex} & {fmt_dec(s_abs, 2)} & {fmt_dec(s_abs / p, 2)} \\\\")
    if not grupos:
        linhas.append("\\multicolumn{6}{c}{Nenhum total de núcleos foi medido com mais de uma combinação.} \\\\")
    linhas += ["\\bottomrule", "\\end{tabular}"]
    write_file("tab_hibrido.tex", linhas)


def figura_hibrido(stats: dict) -> None:
    grupos = grupos_hibridos(stats)
    if not grupos:
        write_file("fig_hibrido.tex", ["\\emph{Nenhum total de núcleos foi medido com mais de uma combinação.}"])
        return
    p = max(grupos)
    cfgs = grupos[p]
    rotulos = [f"{cfg[0]}x{cfg[1]}" for cfg in cfgs]
    linhas = [
        "\\begin{tikzpicture}",
        "\\begin{axis}[",
        "    width=0.75\\textwidth, height=0.42\\textwidth,",
        "    ybar, bar width=16pt, ymin=0,",
        f"    xlabel={{NPrc $\\times$ $T$ (total de {p} núcleos)}}, ylabel={{Tempo do núcleo (s)}},",
        f"    symbolic x coords={{{','.join(rotulos)}}},",
        "    xtick=data, enlarge x limits=0.2,",
        "    nodes near coords, nodes near coords style={font=\\scriptsize, /pgf/number format/.cd, fixed, precision=2},",
        "    grid=major, grid style={gray!25},",
        "]",
        "\\addplot coordinates {" + " ".join(f"({r},{stats['tempos'][cfg]:.6f})" for r, cfg in zip(rotulos, cfgs)) + "};",
        "\\end{axis}",
        "\\end{tikzpicture}",
    ]
    write_file("fig_hibrido.tex", linhas)


# ---------------------------------------------------------------------------
# Figura: tempo sequencial, paralelo 1x1 e melhor paralelo por carga
def figura_tempos(todas: list[dict]) -> None:
    rotulos = [carga_label(s["carga"]) for s in todas]
    linhas = [
        "\\begin{tikzpicture}",
        "\\begin{axis}[",
        "    width=0.8\\textwidth, height=0.45\\textwidth,",
        "    ybar, bar width=12pt,",
        "    ymode=log, log origin=infty,",
        "    xlabel={Carga}, ylabel={Tempo do núcleo (s)},",
        f"    symbolic x coords={{{','.join(rotulos)}}},",
        "    xtick=data, enlarge x limits=0.25,",
        "    legend pos=north west, legend cell align=left, grid=major, grid style={gray!25},",
        "]",
        "\\addplot coordinates {" + " ".join(f"({r},{s['t_seq']:.6f})" for r, s in zip(rotulos, todas)) + "};",
        "\\addlegendentry{sequencial}",
    ]
    if all(s["t_par_11"] is not None for s in todas):
        linhas += ["\\addplot coordinates {" + " ".join(f"({r},{s['t_par_11']:.6f})" for r, s in zip(rotulos, todas)) + "};",
                   "\\addlegendentry{paralela, $1{\\times}1$}"]
    linhas += [
        "\\addplot coordinates {" + " ".join(f"({r},{s['tempos'][s['melhor']]:.6f})" for r, s in zip(rotulos, todas)) + "};",
        "\\addlegendentry{paralela, melhor NPrc${\\times}T$}",
        "\\end{axis}",
        "\\end{tikzpicture}",
    ]
    write_file("fig_tempos.tex", linhas)


# ---------------------------------------------------------------------------
# Figuras: speedup e eficiência em função do total de núcleos, uma curva por T
def figura_por_nucleos(stats: dict, nome: str, calcula, ylabel: str, com_ideal: bool) -> None:
    totais = sorted({cfg[0] * cfg[1] for cfg in stats["tempos"]})
    linhas = [
        "\\begin{tikzpicture}",
        "\\begin{axis}[",
        "    width=0.8\\textwidth, height=0.5\\textwidth,",
        "    xmode=log, log basis x=2," + (" ymode=log, log basis y=2," if com_ideal else " ymin=0,"),
        f"    xlabel={{$p$ = NPrc $\\times$ $T$ (total de núcleos)}}, ylabel={{{ylabel}}},",
        f"    xtick={{{','.join(str(p) for p in totais)}}}, xticklabels={{{','.join(str(p) for p in totais)}}},",
    ]
    if com_ideal:
        linhas.append(f"    ytick={{{','.join(str(p) for p in totais)}}}, yticklabels={{{','.join(str(p) for p in totais)}}},")
    linhas += [
        "    legend pos=" + ("north west" if com_ideal else "south west") + ", legend cell align=left, grid=major, grid style={gray!25},",
        "]",
    ]
    for t in stats["ts"]:
        pontos = [(np * t, calcula((np, t))) for np in stats["nps"] if (np, t) in stats["tempos"]]
        linhas.append("\\addplot+[mark=*] coordinates {" + " ".join(f"({p},{v:.4f})" for p, v in pontos) + "};")
        linhas.append(f"\\addlegendentry{{$T={t}$}}")
    if com_ideal:
        linhas.append(f"\\addplot[dashed, gray, domain=1:{totais[-1]}, samples=2] {{x}};")
        linhas.append("\\addlegendentry{ideal ($S=p$)}")
    linhas += ["\\end{axis}", "\\end{tikzpicture}"]
    write_file(nome, linhas)


# ---------------------------------------------------------------------------
# Valores citados no texto do relatório
def valores_texto(todas: list[dict], ref: dict, ambiente: dict[str, str]) -> None:
    valores = {}

    cfg = ref["melhor"]
    p = cfg[0] * cfg[1]
    s_abs = speedup_abs(ref, cfg)
    s_rel = speedup_rel(ref, cfg)

    valores["cargaRef"] = carga_label(ref["carga"]).lower()
    valores["dimensoesRef"] = f"${ref['L']} \\times {ref['C']}$"
    valores["passosRef"] = str(ref["passos"])
    valores["tempoSeqRef"] = fmt_dec(ref["t_seq"], 2)
    valores["melhorNP"] = str(cfg[0])
    valores["melhorT"] = str(cfg[1])
    valores["melhorP"] = str(p)
    valores["melhorTempo"] = fmt_dec(ref["tempos"][cfg], 2)
    valores["melhorSpeedupAbs"] = fmt_dec(s_abs, 2)
    valores["melhorEficienciaAbs"] = fmt_dec(100 * s_abs / p, 0)
    if s_rel is not None:
        valores["tempoParUmRef"] = fmt_dec(ref["t_par_11"], 2)
        valores["melhorSpeedupRel"] = fmt_dec(s_rel, 2)
        valores["melhorEficienciaRel"] = fmt_dec(100 * s_rel / p, 0)
        valores["fatorBase"] = fmt_dec(ref["t_seq"] / ref["t_par_11"], 3)
        valores["comparacaoBase"] = "mais rápida" if ref["t_par_11"] < ref["t_seq"] else "mais lenta"
        valores["diferencaBasePct"] = fmt_dec(100 * abs(ref["t_par_11"] - ref["t_seq"]) / ref["t_seq"], 1)
        fracao = karp_flatt(s_rel, p)
        if fracao is not None and fracao > 0:
            valores["fracaoSerialPct"] = fmt_dec(100 * fracao, 1)
            valores["speedupMaxAmdahl"] = fmt_dec(1 / fracao, 1)

    # Faixa de speedups da melhor configuração de cada carga
    melhores = [speedup_abs(s, s["melhor"]) for s in todas]
    valores["speedupMin"] = fmt_dec(min(melhores), 2)
    valores["speedupMax"] = fmt_dec(max(melhores), 2)

    # Comparação entre configurações híbridas no maior total de núcleos com mais de uma combinação
    grupos = grupos_hibridos(ref)
    if grupos:
        p_hibrido = max(grupos)
        ordenadas = sorted(grupos[p_hibrido], key=lambda c: ref["tempos"][c])
        rapida, lenta = ordenadas[0], ordenadas[-1]
        valores["hibridoP"] = str(p_hibrido)
        valores["hibridoMelhorNP"] = str(rapida[0])
        valores["hibridoMelhorT"] = str(rapida[1])
        valores["hibridoMelhorTempo"] = fmt_dec(ref["tempos"][rapida], 2)
        valores["hibridoPiorNP"] = str(lenta[0])
        valores["hibridoPiorT"] = str(lenta[1])
        valores["hibridoPiorTempo"] = fmt_dec(ref["tempos"][lenta], 2)
        valores["hibridoDiferencaPct"] = fmt_dec(100 * (ref["tempos"][lenta] - ref["tempos"][rapida]) / ref["tempos"][rapida], 1)

    # Totais de execuções e conferência de resultados
    valores["numCargas"] = str(len(todas))
    valores["numConfiguracoes"] = str(len(ref["tempos"]))
    valores["numExecucoesPar"] = str(sum(len(s["par"]) for s in todas))
    valores["resultadoValidacao"] = "idênticos" if all(resultado_identico(s) for s in todas) else "\\textbf{DIFERENTES}"
    valores["listaNP"] = ", ".join(str(np) for np in ref["nps"])
    valores["listaT"] = ", ".join(str(t) for t in ref["ts"])
    valores["maiorP"] = str(max(c[0] * c[1] for c in ref["tempos"]))
    valores["reps"] = ambiente.get("reps", "\\TODO{reps}")

    # Capacidade física do ambiente, supondo nós iguais ao nó em que o benchmark foi disparado
    if ambiente.get("num_nos", "").isdigit() and ambiente.get("nucleos_fisicos", "").isdigit():
        nos = int(ambiente["num_nos"])
        valores["numNos"] = str(nos)
        valores["nucleosFisicosPorNo"] = ambiente["nucleos_fisicos"]
        valores["nucleosFisicosTotais"] = str(nos * int(ambiente["nucleos_fisicos"]))
        if ambiente.get("cpus_logicas", "").isdigit():
            valores["cpusLogicasTotais"] = str(nos * int(ambiente["cpus_logicas"]))

    linhas = ["% Gerado por testes/scripts/process_results.py, não editar à mão"]
    linhas += [f"\\DefValor{{{chave}}}{{{valor}}}" for chave, valor in valores.items()]
    write_file("valores.tex", linhas)


# ---------------------------------------------------------------------------
def main() -> None:
    rows = load_runs(CSV_PATH)
    ambiente = load_ambiente(AMBIENTE_PATH)
    print(f"[info] {len(rows)} execuções carregadas de {CSV_PATH}")

    cargas = sorted({r["carga"] for r in rows})
    todas = sorted((carga_stats(rows, c) for c in cargas), key=lambda s: s["atualizacoes"])
    todas = [s for s in todas if s["seq"] and s["par"]]
    if not todas:
        sys.exit("[erro] nenhuma carga tem execuções sequenciais e paralelas.")

    # A carga de referência das análises de escalabilidade é a maior, e a da tabela de independência é a intermediária
    ref = todas[-1]
    intermediaria = todas[len(todas) // 2]

    # Com --map-by node cada processo fica em um nó, então os nós realmente usados são no máximo o maior NPrc medido
    if ambiente.get("num_nos", "").isdigit():
        ambiente["num_nos"] = str(min(int(ambiente["num_nos"]), max(ref["nps"])))

    tabela_validacao(todas)
    tabela_independencia(intermediaria)
    tabela_ambiente(ambiente)
    tabela_resumo(todas)
    for s in todas:
        tabelas_por_carga(s)
    tabela_escalabilidade(ref)
    tabela_hibrido(ref)

    figura_tempos(todas)
    figura_por_nucleos(ref, "fig_speedup.tex", lambda cfg: speedup_abs(ref, cfg), "\\emph{Speedup} absoluto", True)
    figura_por_nucleos(ref, "fig_eficiencia.tex", lambda cfg: speedup_abs(ref, cfg) / (cfg[0] * cfg[1]), "Eficiência absoluta", False)
    figura_hibrido(ref)

    valores_texto(todas, ref, ambiente)

    print("\nFeito. Envie relatorio/main.tex e a pasta relatorio/plot/ pro Overleaf (ou rode 'make relatorio').")


if __name__ == "__main__":
    main()
