# Progresso do TCC2 — estado e continuidade

Documento de passagem entre sessões/máquinas. Quem retomar o trabalho (pessoa
ou agente) deve ler isto e o `AGENTS.md` antes de mexer no código.

Última atualização: 2026-11-04, desktop Pop!_OS (branch `feature/isolation`, sobre
`feature/rusage`; `feature/plots` já mergeada em `develop`).

## Como retomar em outra máquina

```bash
git clone git@github.com:LorenzoJablonski/bloom-filter.git bloom
cd bloom
git checkout develop          # main só tem o commit inicial; o trabalho está em develop
git checkout feature/isolation   # trabalho em andamento (ver "Em andamento" abaixo)
make all && make check && make test
make data                     # datasets NÃO estão no git (N=2000, SEED=42; reprodutível)
```

Requisitos: GCC (C99), make, Python 3.10+ (só biblioteca padrão), util-linux
(`findmnt`, `lsblk`). Gráficos: `make venv` uma vez (cria `.venv` com o
matplotlib; usa o `uv` se existir, útil onde falta o `python3-venv`), depois
`make plots`.

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

## Em andamento (retomar aqui)

Branches ainda não mergeadas em `develop`, nesta ordem:
- `feature/rusage`: medição de memória e paginação. CSV ganhou `rss_kb, majflt,
  minflt` por linha (`include/procstat.h`); `run.py` grava por execução
  `maxrss_kb`, faults, tempo de CPU, trocas de contexto (`nivcsw` = disputa de
  CPU) e swap in/out; `swap_kind` e `swap_total_kb` entram no `env_key`.
- `feature/isolation`: um processo por estrutura e chaves em lotes
  (`BF_PROTOCOL_VERSION` 3, schema v3 com migração). Ver a decisão abaixo.

Depois disso: a VM (abaixo) e as rodadas oficiais variando N.

## Lista de prioridades

1. Corrigir baseline, métricas e benchmark — **feito**
2. Híbrido — **feito**
3. Experimento de I/O (filtro como gatekeeper de tabelas em disco) — **feito**
3b. Banco de resultados (SQLite) e geração de tabelas — **feito**
4. **Próximo:** rodadas em escala numa VM com pouca RAM e num HD (o autor vai
   conseguir o HD). Ver "Plano da VM" abaixo.
5. Threads (só depois do resto).
6. Texto (depois).

Gráficos — **feito** (já em `develop`): `analyze.py bench|io --plot DIR
[--format pdf png]`, `make plots`.

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
| benchmark | `src/benchmark.c` | regimes `same_m` (m e k do SBF para todos) e `same_fpr` (cada um com seus parâmetros); rep 0 é aquecimento; contagem de hashes em passada não cronometrada; uma estrutura por processo, chaves em lotes (`keyset.h`) |
| I/O | `src/store.c`, `src/iobench.c` | tabelas tipo SSTable em blocos de 4 KB, 1 `pread` por consulta, O_DIRECT; T tabelas consultadas da mais nova para a mais antiga; construção separada (`--build-store`) |
| resultados | `scripts/run.py`, `resultsdb.py`, `analyze.py` | cada execução no `results/bloom.db` com commit, binário, máquina, disco e dataset |
| gráficos | `scripts/plots.py` | `--plot` no `analyze.py`: mesmos agregados das tabelas (média, IC 95%); bench por tipo × regime (baseline como referência) + memória × FPR; I/O por tipo × disco × T com as razões de negativas lado a lado |

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
- **Cada teste é um ambiente individual: um processo por estrutura, e o dataset
  fora da RAM.** Antes todos os filtros rodavam no mesmo processo, com as chaves
  na memória. Num cgroup de 150 MB, o baseline empurrava as chaves para o swap e
  o SBF, que vinha depois, pagava: 5× mais lento com só 1,2 MB, puro artefato da
  ordem de execução. Agora o `run.py` abre um processo por estrutura × regime
  (benchmark) ou por estrutura × T × neg (I/O), e as chaves são lidas em lotes por
  um buffer de 1 MiB (a leitura fica fora do cronômetro e sai do page cache com
  `posix_fadvise`). Ter todas as chaves na RAM nunca foi necessário; num sistema
  real elas também chegam aos poucos. Consequências: as tabelas em disco são
  construídas antes, numa etapa não medida; o `--n` mantém chaves na RAM e fica só
  para testes rápidos (um dataset por N, via `make data N=...`). Conferido com
  1M UUIDs: FPR, leituras e leituras inúteis idênticas às do protocolo 1.
- **Gráficos saem dos mesmos agregados das tabelas** (`aggregate()` do
  `analyze.py`), para figura e tabela nunca divergirem. O matplotlib fica só
  em `scripts/plots.py`, importado apenas com `--plot`: o resto continua só
  com a biblioteca padrão. Figuras de máquinas/discos diferentes (`env_key`)
  nunca se misturam; configurações diferentes só dividem uma figura quando não
  se sobrepõem (ex.: neg 0,5 e 0,9 lado a lado). Eixo log automático quando os
  valores variam mais de 30× (ex.: bits/chave com o baseline).
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

Desktop (Pop!_OS, 12 núcleos, 15 GB, NVMe ADATA SM2P41C3 em **ext4**:
O_DIRECT mede o disco de verdade), sondagem com 1M UUIDs, 1 rodada:
- benchmark: 16,6 s, pico 230 MB; dataset 110 MB (gerado em 4,4 s, pico 360 MB).
- híbridos ~4× mais rápidos que o SBF na inserção (eram ~3× com 100 mil);
  rDBF (104 bits/chave) já não cabe no cache e fica atrás do hybrid_512;
  512 vs 4096 ainda não se separam em velocidade.
- iobench 1 min 30 s; tabelas ~136 MB por configuração de T.
  T = 8, 90% negativas: sem filtro 7,66 leituras · 366 µs; SBF 0,17 · 8,8 µs
  (≈42×); baseline 0,10 · 7,0 µs com 90× mais memória.
- Estimativa para 10M: ~3,8 GB de disco por tipo (≤ 15 GB os 4 tipos);
  benchmark ~3 min por rodada (estimativa anterior ao isolamento).

Depois do isolamento (protocolo 3, mesmo desktop, 2 repetições, 1 rodada):
- RSS: SBF ~8 MB (antes ~230 MB, processo com dataset e baseline); baseline ~124 MB.
- Cgroup de 60 MB (zram + swap; em disco deve ser pior): baseline inserção
  277 → 11 875 ns/op, busca negativa 247 → 28 990 ns/op, 1,5 milhão de faults
  maiores; todos os filtros com 0 faults e tempos iguais aos sem limite.
- ns/op sem limite não mudaram de forma relevante em relação ao protocolo 1.
- Ruído do desktop: a latência do NVMe subiu de ~50 µs (03/11) para ~86 µs por
  leitura (04/11), igual nos binários antigo e novo alternados, com navegador e
  outros programas abertos e governor `powersave`. Causa não identificada; as
  rodadas oficiais precisam de ambiente controlado.

## Ressalvas para o texto (anotar, não escrever agora)

- O artigo do robustBF afirma ~1,382 bits/elemento com FPR quase zero; com um
  bit por chave a matemática dá FPR ≈ 52% nesse tamanho. Os nossos números
  seguem a fórmula 1 − (1 − 1/M)^n e batem com o medido.
- O rDBF usa hash de 32 bits: o espaço X·Y·61 deve ficar bem abaixo de 2³².
  Com 10 milhões de chaves em `same_fpr` (~10⁹ bits) o viés estimado eleva a
  FPR em ~1% relativo (1,004% vs 0,995%) — desprezível, mas mencionável.
- A FPR não varia entre repetições (chaves e seeds fixas); só os tempos variam.

## Plano da VM (prioridade 4)

Por que VM: ambiente limpo (sem desktop, vCPUs fixas) e RAM pequena de
verdade. Estourar a RAM **não é problema, é dado**: em big data a estrutura
determinística não cabe na memória e pagina; os filtros, por serem de bits e
probabilísticos, ficam pequenos (1M UUIDs: SBF 1,2 MB contra 108 MB do
baseline; 10M: ~12 MB contra ~1,1 GB). O eixo principal é N (quantidade de
chaves) crescendo até o baseline passar da RAM da VM.

- Imagem: Debian 13 `debian-13-genericcloud-amd64.qcow2`
  (cloud.debian.org/images/cloud/trixie/latest/; a *genericcloud* tem
  cloud-init e virtio). libvirt em `qemu:///session` (sem root); no desktop
  há `/dev/kvm`, `qemu-system-x86_64`, `virsh`, `qemu-img`, `genisoimage`.
- vCPUs fixas em núcleos dedicados (`vcpupin`); o host ainda deve ficar quieto.
- **Swap em disco, não zram.** zram é swap comprimido na própria RAM
  (padrão no Fedora/Pop!_OS): a paginação custa µs em vez de ms e a RAM
  "cresce" ~1,5–2×, o que não representa servidor de big data (swap em disco
  ou nenhum). zram pode ser variante opcional; `swap_kind` separa as duas.
- RAM: com o dataset fora da memória, a estrutura define o limite. Numa VM de
  1 GB a projeção é baseline ~540 MB em 5M e ~1,1 GB em 10M (estoura), SBF
  ~12 MB e rDBF `same_fpr` ~130 MB em 10M. A curva do baseline deve quebrar entre
  5M e 10M, então N = 1, 5, 10 e 20M basta.
- Gerar datasets e construir as tabelas no host e copiar para a VM: precisam de
  mais RAM do que ela tem (geração de 10M: pico ~3,6 GB) e não são medidos.

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
