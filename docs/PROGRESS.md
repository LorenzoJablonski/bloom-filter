# Progresso do TCC2 — estado e continuidade

Documento de passagem entre sessões/máquinas. Quem retomar o trabalho (pessoa
ou agente) deve ler isto e o `AGENTS.md` antes de mexer no código.

Última atualização: 2026-11-03 (branch `develop`, após merge de `feature/analysis`).

## Como retomar em outra máquina

```bash
git clone git@github.com:LorenzoJablonski/bloom-filter.git bloom
cd bloom
git checkout develop          # main só tem o commit inicial; o trabalho está em develop
make all && make check && make test
make data                     # datasets NÃO estão no git (N=2000, SEED=42; reprodutível)
```

Requisitos: GCC (C99), make, Python 3.10+ (só biblioteca padrão), util-linux
(`findmnt`, `lsblk`). Gráficos ainda não existem (precisarão de matplotlib).

Os artefatos gerados (`data/*`, `results/*`, incluindo `results/bloom.db`) são
ignorados pelo git e recriados pelos comandos acima. Os arquivos de sessão
(`session-ses_*.md`, exports `20??-??-??-*.txt`) também são ignorados; copie à
mão se quiser o histórico das conversas.

Para o agente (Claude Code): o `CLAUDE.md` importa o `AGENTS.md`; peça para ler
este arquivo ao começar.

## Acordos de trabalho (valem sempre)

- **Nunca commitar, mergear ou dar push antes de o autor revisar e aprovar.**
  Fluxo: implementar → `make check`/`make test` → resumir o diff → esperar
  "pode commitar". Aprovar um plano que menciona commit não é aprovar o diff.
- **Foco atual é código.** Não mexer no texto do TCC até ser pedido.
- **Sem comparação extra de threads** (por partição vs por chave) antes de o
  resto do trabalho estar pronto.
- Conversa em português; commits e comentários de código em inglês, terminando
  com a linha `Co-Authored-By` do agente quando houver.
- Git Flow: `feature/*` → `develop`. Não tocar na `main` sem pedido.
- Nome do modelo: **rDBF**, com o robustBF (Patgiri, 2021) como implementação
  de referência.

## Lista de prioridades

1. Corrigir baseline, métricas e benchmark — **feito**
2. Híbrido — **feito**
3. Experimento de I/O (filtro como gatekeeper de tabelas em disco) — **feito**
3b. Banco de resultados (SQLite) e geração de tabelas — **feito**
4. **Próximo:** rodadas em escala numa VM com pouca RAM e num HD (o autor vai
   conseguir o HD). Ver "Plano da VM" abaixo.
5. Threads (só depois do resto).
6. Texto (depois).

Pendências menores: gráficos (`.venv` + `requirements.txt` com matplotlib
opcional, integrado ao `analyze.py`).

## O que existe

| componente | arquivos | notas |
|---|---|---|
| hashes | `src/murmurhash2.c` | MurmurHash2 32 bits e MurmurHash64A, validados com smhasher |
| SBF | `src/sbf.c` | k bits num vetor |
| CBF | `src/cbf.c` | contadores de 4 bits saturando em 15, com deleção |
| PBF | `src/pbf.c` | k partições, cada uma alinhada a 64 bytes (padding conta na memória) |
| rDBF | `src/rdbf.c` | Eq. (1) do robustBF: matriz X×Y de células de 64 bits, β = 61, X ≠ Y primos ≠ 61; 1 hash, 1 bit por chave; sem deleção |
| híbrido | `src/hybrid.c` | filtro em blocos por chave: shard escolhido pela chave, matriz de primos por shard, 1 MurmurHash64A, double hashing dentro do shard; variantes 512/4096 bits e com contadores (`hybrid_c_*`, com deleção) |
| baseline | `src/baseline.c` | tabela hash exata (uthash), guarda cópia das chaves |
| interface | `include/bf.h`, `src/bf.c` | `bf_ops_t`: todos os modelos passam pelo registro |
| dimensionamento | `<modelo>_params(n, p)` | cada modelo pela sua própria fórmula de FPR |
| benchmark | `src/benchmark.c` | regimes `same_m` (m e k do SBF para todos) e `same_fpr` (cada um com seus parâmetros); rep 0 é aquecimento; contagem de hashes em passada não cronometrada |
| I/O | `src/store.c`, `src/iobench.c` | tabelas tipo SSTable em blocos de 4 KB, 1 `pread` por consulta, O_DIRECT; T tabelas consultadas da mais nova para a mais antiga |
| resultados | `scripts/run.py`, `resultsdb.py`, `analyze.py` | cada execução no `results/bloom.db` com commit, binário, máquina, disco e dataset |

## Decisões técnicas e porquês

- **Dois regimes de comparação.** `same_m` mostra o que cada modelo faz com a
  mesma memória; `same_fpr` mostra quanta memória cada um precisa para a mesma
  FPR. O rDBF perde em `same_m` (FPR ~10% com ~10 bits/chave) — é uma "falha
  esperada": 1 bit por chave troca memória por velocidade.
- **PBF alinhado a 64 bytes** para que cada partição comece numa linha de
  cache; o padding é contado na memória.
- **rDBF X ≠ Y.** Com X = Y, `h % X == h % Y` e só a diagonal seria usada. Pelo
  Teorema Chinês do Resto, (h%X, h%Y, h%61) com primos distintos equivale a
  h mod (X·Y·61). Busca-se o par de primos de menor produto para não
  desperdiçar memória; o custo restante são os 3 bits sem uso por célula (≈5%).
- **Híbrido em vez de "PBF com threads".** No PBF cada thread mexeria numa
  partição, mas todas as threads tocam toda chave; no híbrido a chave escolhe um
  shard e tudo acontece dentro dele (localidade de cache e, no futuro, menos
  disputa entre threads).
- **btrfs com `compress=zstd` ignora O_DIRECT** (as leituras vêm do page cache:
  `proc_read_bytes` = 0, ~3 µs por leitura). O `iobench` marca o diretório das
  tabelas com NOCOMP (`chattr +m`) e avisa se os bytes lidos forem bem menores
  que leituras × 4096. Em ext4/xfs isso não acontece.
- **Comparabilidade de resultados.** `BF_PROTOCOL_VERSION` (`include/common.h`)
  sobe quando uma mudança quebra a comparação com execuções antigas. O
  `run.py` recusa rodar com arquivos versionados alterados (`--allow-dirty`
  marca a execução e a análise a ignora). Execuções só são agrupadas com o
  mesmo `config_key` (protocolo, hash do dataset, parâmetros) e `env_key`
  (máquina, limite de RAM, disco). `binary_sha256` distingue o mesmo commit
  compilado com outro compilador/flags.
- **Datasets reprodutíveis.** `make data` com a mesma seed gera arquivos
  idênticos byte a byte (corrigido em 2026-11-03: UUID ignorava a seed e a
  ordem do Dinexist dependia do hash randomizado do Python).

## Resultados preliminares (máquina de desenvolvimento, só indicativos)

Notebook Fedora, i7-1065G7, 20 GB RAM, SSD NVMe SSSTC CL1-4D256 (btrfs + LUKS).

Benchmark em memória, 100 mil UUIDs, p = 1%, `same_fpr`:
- SBF: 9,59 bits/chave; ~2,04 hashes por consulta negativa.
- rDBF: ~104 bits/chave, inserção ~4× mais rápida que o SBF; FPR 1,000%.
- hybrid_4096: 9,67 bits/chave, inserção ~3× mais rápida que o SBF.
- hybrid_c: inserção ~3× mais rápida que o CBF com a mesma memória.
- baseline: ~884 bits/chave.
- 512 vs 4096 bits por shard não diferem enquanto o filtro cabe no cache;
  precisa de ~10 milhões de chaves para aparecer.

I/O, 2000 chaves, T = 8 tabelas, 90% de consultas negativas, O_DIRECT no NVMe:
- sem filtro: 7,7 leituras/consulta, ~870–940 µs de média;
- com SBF: 0,16–0,17 leituras/consulta, ~20 µs (≈40× menos latência);
- baseline (exato): 0,10 leituras/consulta (só as consultas positivas).

## Ressalvas para o texto (anotar, não escrever agora)

- O artigo do robustBF afirma ~1,382 bits/elemento com FPR quase zero; com um
  bit por chave a matemática dá FPR ≈ 52% nesse tamanho. Os nossos números
  seguem a fórmula 1 − (1 − 1/M)^n e batem com o medido.
- O rDBF usa hash de 32 bits: o espaço X·Y·61 deve ficar bem abaixo de 2³².
  Com 10 milhões de chaves em `same_fpr` (~10⁹ bits) o viés estimado eleva a
  FPR em ~1% relativo (1,004% vs 0,995%) — desprezível, mas mencionável.
- A FPR não varia entre repetições (chaves e seeds fixas); só os tempos variam.

## Plano da VM (prioridade 4)

- Disco virtual com cache do host desligado (`cache=none` / passthrough do HD),
  senão o O_DIRECT dentro da VM ainda passa pelo page cache do host.
- Sistema de arquivos ext4 ou xfs no disco dos testes (evita o problema do
  btrfs).
- RAM limitada (ex.: 1–2 GB) para que o page cache não esconda o disco nos
  testes `--cached`.
- Datasets grandes: `make data N=10000000` (ou o volume que a VM aguentar).
- Rodadas: `make benchmark REPEAT=3 LABEL=vm-...` e
  `make iobench STORE_DIR=/mnt/hd/store LABEL=hd-... REPEAT=3`; depois
  `python3 scripts/analyze.py runs` / `bench` / `io` e `freeze` para congelar.
