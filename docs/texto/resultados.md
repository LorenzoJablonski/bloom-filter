# Resultados — conteúdo-base para o capítulo

Material de apoio para redigir o capítulo de resultados e discussão. Não é o texto
final: cada seção traz **o que foi medido**, os **números** (tirados dos bancos
congelados, não de memória) e **a leitura** deles, para ser reescrito em prosa. O
método de cada experimento está em `docs/texto/materiais-e-metodos.md`.

Fontes (todas em `results/campaigns/`, abertas só para leitura pelo `analyze.py`):

| experimento | pasta | rótulo | o que tem |
|---|---|---|---|
| 1 e 2 | `vm-nvme/` | `oficial` | benchmark e I/O, 4 tipos de chave, N = 1, 5, 10 e 20M |
| 1 (complemento) | `vm-nvme-extra/` | `colisoes`, `delecao` | colisões estruturais e deleção |
| 3 | `vm6-nvme/` | `threads`, `ponte` | concorrência e comparação entre as duas VMs |

Convenções:
- Valores como "média ± IC 95%" (t de Student); quando o IC é desprezível, só a média.
- Salvo indicação, **UUID** e regime **`same_fpr`** (cada filtro dimensionado para
  FPR de 1% pela sua fórmula).
- 1M, 5M, 10M e 20M = 1, 5, 10 e 20 milhões de chaves inseridas.
- **[FIGURA]** aponta o arquivo já gerado; **[FIGURA A GERAR]**, uma figura que ainda
  não existe e que valeria fazer.

Comandos para refazer qualquer tabela abaixo:

```bash
O=results/campaigns/vm-nvme/bloom.db
python3 scripts/analyze.py --db $O bench --label oficial --type uuid --sizing same_fpr none
python3 scripts/analyze.py --db $O io --label oficial --type uuid --tables 8 --neg 0.9
python3 scripts/analyze.py --db results/campaigns/vm6-nvme/bloom.db threads --label threads
```

---

## 1. Validação: as implementações estão corretas

Antes dos desempenhos, o que garante que os números medem estruturas corretas:

- **Zero falsos negativos** em todas as execuções dos três experimentos (2396 no
  total), inclusive depois da deleção e com várias threads.
- **Zero erros no I/O**: toda resposta conferida com o gabarito.
- **Concorrência**: em todas as 1140 amostras com threads, os falsos positivos são
  **idênticos** aos da inserção serial da mesma estrutura.
- **FPR medida ≈ teórica** (seção 2.3), e taxa de colisões igual à prevista pela
  teoria (seção 2.6).
- **Ponte entre as VMs**: com uma thread, a VM de 6 vCPUs repete a campanha oficial
  dentro de ±3,3% em inserção e buscas, com falsos positivos idênticos (seção 4.1).

---

## 2. Experimento 1 — benchmark em memória

### 2.1 Memória

Memória ocupada (`same_fpr`, FPR alvo de 1%):

| estrutura | bits/chave | 1M | 20M | em relação ao SBF |
|---|---|---|---|---|
| SBF | 9,59 | 1,2 MB | 24,0 MB | 1× |
| PBF | 9,59 | 1,2 MB | 24,0 MB | 1× (o padding de alinhamento é desprezível) |
| hybrid_4096 | 9,65 | 1,2 MB | 24,1 MB | 1,01× |
| hybrid_512 | 9,92 | 1,2 MB | 24,8 MB | 1,03× |
| CBF | 38,37 | 4,8 MB | 95,9 MB | 4× (contadores de 4 bits) |
| hybrid_c_4096 | 38,61 | 4,8 MB | 96,5 MB | 4,03× |
| hybrid_c_512 | 39,66 | 5,0 MB | 99,2 MB | 4,14× |
| rDBF | 104,4 | 13,0 MB | 261,0 MB | 10,9× |
| **baseline** (exata) | 867–907 | 108,4 MB | **2268,4 MB** | **~95×** |

Leitura:
- Um filtro de ~10 bits por chave guarda 20 milhões de chaves em ~24 MB; a tabela
  exata, que guarda as chaves, precisa de ~2,3 GB.
- A contagem (para permitir deleção) custa exatamente 4× a memória.
- O híbrido custa só 1–3% a mais que o SBF; o preço da localidade é pequeno.
- O rDBF troca memória por velocidade: com 1 bit por chave, precisa de ~n/p bits
  para FPR p (seção 3.2 do método).

[FIGURA] `vm-nvme/plots/tradeoff-uuid-n20000000-same-fpr.pdf` (memória × FPR).

### 2.2 Tempos de inserção e consulta

ns por operação (`same_fpr`, UUID):

| estrutura | inserção 1M | inserção 20M | busca existente 20M | busca negativa 1M | busca negativa 20M |
|---|---|---|---|---|---|
| SBF | 83,3 | 187,0 | 182,2 | 48,7 | 82,9 |
| CBF | 147,3 | 277,9 | 259,0 | 55,9 | 122,0 |
| PBF | 88,1 | 190,8 | 184,4 | 48,9 | 69,8 |
| rDBF | 26,3 | 40,0 | 37,6 | 23,9 | 37,8 |
| hybrid_512 | 21,2 | 41,4 | 38,2 | 25,6 | 40,3 |
| hybrid_4096 | 22,9 | 57,8 | 51,4 | 30,3 | 49,8 |
| hybrid_c_512 | 42,5 | 72,2 | 60,7 | 28,6 | 53,9 |
| hybrid_c_4096 | 45,7 | 98,2 | 70,0 | 34,8 | 72,9 |
| baseline | 284,7 | 65 512 | 95 195 | 259,9 | 64 926 |

Chamadas de hash por busca negativa: **2,05** no SBF, CBF e PBF (com k = 7) e **1,00**
no rDBF e nos híbridos.

Leitura:
- **Menos hashes e menos acessos à memória valem mais que tudo.** Com 20M, o
  hybrid_512 insere **4,5× mais rápido** que o SBF (41,4 contra 187 ns), com a mesma
  memória. O rDBF é tão rápido quanto, mas com 11× a memória.
- **A busca negativa do SBF para cedo**: em média 2,05 hashes em vez de 7, por isso ela
  custa menos que a busca existente (82,9 contra 182,2 ns em 20M).
- **O tempo por operação cresce com N, mesmo com o número de hashes constante.** O
  SBF vai de 83 para 187 ns na inserção entre 1M e 20M. O filtro sai do cache (1,2 MB
  cabem no L2/L3; 24 MB passam dos 18 MB de L3) e cada uma das k posições vira uma
  falta de cache. No híbrido, as k posições de uma chave ficam num único bloco, quase
  sempre numa linha de cache: ele cresce de 21 para 41 ns.
- 512 contra 4096 bits por bloco: o bloco de 512 bits (uma linha de cache) é mais
  rápido em 20M (41 contra 58 ns na inserção), à custa de uma FPR um pouco maior
  (seção 2.3).
- **A baseline cabe na RAM até 5M** (285–361 ns por inserção) e **pagina a partir de
  10M** (seção 2.4).

[FIGURA] `vm-nvme/plots/bench-uuid-n{1000000,5000000,10000000,20000000}-same-fpr.pdf`.
[FIGURA] `vm-nvme/plots/scaling-uuid-same-fpr-08e7cf3e.pdf`: tempo por operação × N,
uma linha por estrutura, escala log (inserção, busca existente e busca negativa). Mostra
de uma vez o crescimento suave dos filtros e o salto da baseline em 10M; há a mesma
figura para CPF, CNPJ e URL (baseline só até 5M) e para `same_m`.

**Por tipo de chave** (inserção, ns/op, 1M): chaves curtas são mais rápidas onde há
muitos hashes por chave.

| estrutura | CPF (11 bytes) | CNPJ (14) | URL (~32) | UUID (36) |
|---|---|---|---|---|
| SBF | 40,7 | 44,5 | 88,8 | 83,3 |
| rDBF | 18,3 | 19,1 | 28,7 | 26,3 |
| hybrid_512 | 17,6 | 18,5 | 25,5 | 21,2 |

O SBF calcula 7 hashes da chave inteira, então o tamanho da chave pesa 7 vezes; o
rDBF e o híbrido calculam um só.

### 2.3 Taxa de falsos positivos: prática × teórica

FPR medida (%), alvo de 1%, `same_fpr`:

| estrutura | UUID 1M | UUID 20M | CPF 20M | CNPJ 20M | URL 20M | medida ÷ teórica − 1 (média dos tipos, 20M) |
|---|---|---|---|---|---|---|
| SBF | 0,993 | 1,004 | 1,000 | 1,004 | 1,005 | +0,4% |
| PBF | 0,984 | 1,000 | 1,001 | 1,002 | 0,999 | +0,0% |
| CBF | 0,993 | 1,004 | — | — | — | igual ao SBF |
| rDBF | 0,993 | 1,027 | 1,030 | 1,028 | 1,032 | **+2,8%** |
| hybrid_4096 | 1,023 | 1,015 | 1,013 | 1,012 | 1,012 | +1,4% |
| hybrid_512 | 1,076 | 1,067 | 1,061 | 1,065 | 1,062 | **+6,6%** |

Leitura:
- **A dispersão do MurmurHash2 é boa para todos os tipos de chave.** CPF e CNPJ, de
  baixa entropia, e URL, com prefixos comuns, dão a mesma FPR do UUID (diferenças na
  terceira casa). Não há agrupamento causado pela chave — é a pergunta da seção 4.3.1
  da proposta.
- **SBF, CBF e PBF batem com a fórmula** (dentro de ±0,6%). O CBF tem a mesma FPR do
  SBF, como esperado (mesmas posições).
- **rDBF: o desvio cresce com N**, igual em todos os tipos: +0,0% em 5M, +1,2% em 10M,
  +2,8% em 20M. A causa é o hash de 32 bits: o espaço endereçado (X·Y·61 ≈ 2·10⁹ bits
  em 20M) se aproxima de 2³² ≈ 4,3·10⁹. Com N maiores o desvio cresceria.
- **Híbrido: FPR acima da própria fórmula, de forma constante** (+6,6% relativo no
  hybrid_512, +1,4% no hybrid_4096), em todos os N e tipos. Em números absolutos, o
  hybrid_512 entrega 1,06% em vez de 1%. A causa foi investigada (seção 2.7).

**Mesma memória (`same_m`)**, todos com o m e o k do SBF (FPR %, 10M): SBF 1,002;
PBF 0,998; hybrid_4096 1,034; hybrid_512 1,254; **rDBF 9,885**. Com o orçamento de
memória do SBF, o rDBF erra ~10 vezes mais; é o custo de usar 1 bit por chave.

### 2.4 A estrutura exata deixa de caber na RAM

Baseline (UUID), por repetição:

| N | memória | inserção (ns/op) | busca negativa (ns/op) | faltas de página maiores |
|---|---|---|---|---|
| 1M | 108 MB | 284,7 ± 2,9 | 259,9 ± 2,3 | 0 |
| 5M | 567 MB | 361,2 ± 2,5 | 241,0 ± 1,1 | 0 |
| 10M | 1134 MB | 17 390 ± 1834 | 30 341 ± 6160 | ~6,8 milhões |
| 20M | 2268 MB | 65 512 ± 1807 | 64 926 ± 1625 | ~39,8 milhões |

- Entre 5M e 10M a inserção fica **48× mais lenta** e, em 20M, **181×**: a tabela passa
  da RAM de ~1 GB da VM e o sistema troca páginas com o disco.
- Os filtros, no mesmo N, têm **zero faltas de página**: o maior (rDBF) ocupa 261 MB.
- 10M e 20M da baseline têm 3 repetições de uma rodada (método, seção 3.7): os
  intervalos são largos, mas o efeito é de dezenas de vezes.

### 2.5 Deleção (CBF, híbridos com contadores e baseline)

ns por chave apagada (UUID; apaga o Dexist, 10% das chaves):

| N | CBF | hybrid_c_512 | hybrid_c_4096 | baseline |
|---|---|---|---|---|
| 1M | 145,0 | 42,4 | 45,4 | 300,6 |
| 5M | 222,5 | 63,8 | 82,3 | 320,2 |
| 10M | 257,4 | 70,8 | 95,2 | — |
| 20M | 272,8 | 73,6 | 100,8 | — |

- Apagar custa o mesmo que inserir em cada estrutura (mesmas posições). O
  hybrid_c_512 apaga **3,7× mais rápido** que o CBF.
- **Nenhum falso negativo depois da deleção**, em todas as execuções e nos 4 tipos de
  chave: a saturação (contador em 15 nunca é decrementado) protege as outras chaves.
- ~0,6% das chaves apagadas ainda respondem "presente" (617 de 100 mil no CBF com
  1M): são falsos positivos comuns, abaixo de 1% porque a ocupação cai depois da
  deleção.
- Baseline só em 1M e 5M (em 10M e 20M ela pagina e a conferência levaria horas).

[FIGURA] `vm-nvme-extra/plots/delecao/bench-uuid-n20000000-same-fpr.pdf`.

### 2.6 Colisões estruturais

Percentual das posições escritas que já estavam ocupadas (`same_fpr`):

| estrutura | taxa | |
|---|---|---|
| SBF, CBF, PBF | 29,0% | k = 7 bits por chave, ocupação final ~52% |
| hybrid_4096 | 28,9% | |
| hybrid_512 | 25,0% | usa k = 6 |
| rDBF | 0,5% | 1 bit por chave numa matriz de ~104 bits/chave |

- **Igual à teoria**: `1 − m·(1 − e^(−kn/m))/(kn)` dá 29,019% para o SBF com 10M; o
  medido é 29,023%.
- **Igual em todos os N e tipos de chave** (diferenças na segunda casa decimal),
  porque o dimensionamento mantém k·n/m constante. Mais uma confirmação de que a
  chave não causa agrupamento.
- Colisão não é erro: é o que faz a ocupação ficar abaixo de k·n/m. O que importa
  para o erro é a FPR (seção 2.3).

### 2.7 Por que a FPR do híbrido fica acima da fórmula

Investigação com `tools/fprlab.c`, que reconstrói o híbrido e troca uma peça de cada
vez, com os mesmos blocos, células e k. A versão "atual" reproduz exatamente os falsos
positivos do filtro real (conferido nos 6 casos). Medido com 20M chaves e 20M
consultas negativas (erro de medida ~±0,45% relativo); saída completa em
`results/campaigns/fprlab/fprlab.txt`.

FPR medida acima da fórmula (relativo):

| variante | bloco de 512 bits (UUID / CPF) | bloco de 4096 bits (UUID / CPF) |
|---|---|---|
| atual: hash duplo `g_i = h1 + i·h2` | **+6,7% / +6,1%** | **+1,5% / +1,3%** |
| k hashes independentes por chave | **+0,7% / +0,8%** | **+0,1% / −0,2%** |
| passo h2 vindo de outra chamada de hash | +6,6% / +7,0% | +1,5% / +1,3% |
| bloco com número primo de células (509, 4079) | +6,5% / +5,9% | +1,1% / +0,9% |
| hash duplo "melhorado" (Kirsch e Mitzenmacher, `+ (i³−i)/6`) | +2,6% / +2,3% | +0,6% / +0,6% |

E a fórmula com a carga **real** de cada bloco dá o mesmo que a de Poisson (1,0002%
contra 1,0000%).

Leitura:
- **A causa é o hash duplo dentro de um bloco pequeno.** Com k posições
  independentes, a FPR volta à fórmula; nenhuma das outras hipóteses explica o
  desvio: a aproximação de Poisson para a carga dos blocos é boa, trocar a origem do
  passo não ajuda (não é correlação entre o bloco e as posições), e um número primo de
  células também não (não é o 511 = 7 × 73 ser composto).
- **Mecanismo, em duas partes** (pela estimativa a partir da ocupação real de cada
  bloco):
  - cerca de **1/3** do excesso: o hash duplo nunca repete uma posição dentro de uma
    chave, enquanto posições independentes repetiriam em ~2,9% das chaves; cada chave
    liga um pouco mais de bits do que a fórmula supõe;
  - cerca de **2/3**: as k posições de uma chave formam uma progressão aritmética
    dentro de 511 células, e as progressões das consultas se sobrepõem às gravadas mais
    do que sorteios independentes se sobreporiam.
- **Por que o bloco de 4096 sofre menos:** com 8× mais células, há muito mais
  progressões possíveis e a sobreposição cai (+1,5% contra +6,7%).
- Isso é conhecido na literatura: o hash duplo tem a mesma FPR assintótica que hashes
  independentes (Kirsch e Mitzenmacher, 2006), mas o desvio aparece em tabelas
  pequenas — e o bloco do híbrido é pequeno de propósito.
- **Correções possíveis** (não aplicadas; decisão do autor): o hash duplo "melhorado"
  reduz o desvio a ~2,5% (512) e ~0,6% (4096) sem nenhuma chamada de hash a mais;
  posições independentes eliminam o desvio, mas exigem mais bits de hash por chave do
  que uma chamada de 64 bits fornece (uma segunda chamada); ou recalibrar o
  dimensionamento para absorver o excesso medido, o que custa pouca memória: ~1,6% no
  bloco de 512 bits e ~0,3% no de 4096 (calculado pela fórmula, supondo o mesmo excesso
  relativo no novo tamanho).

---

## 3. Experimento 2 — filtros como porteiros de I/O

### 3.1 Leituras de disco evitadas

10 000 consultas por repetição; cada leitura é um bloco de 4 KB com O_DIRECT.

| T (tabelas) | consultas negativas | sem filtro: leituras/consulta | com filtro (SBF) | baseline | leituras inúteis: sem filtro → SBF |
|---|---|---|---|---|---|
| 1 | 50% | 1,000 | 0,505 | 0,500 | 5000 → ~50 |
| 1 | 90% | 1,000 | 0,109 | 0,100 | 9000 → ~90 |
| 8 | 50% | 6,25 | 0,556 | 0,500 | 57 450 → ~570 |
| 8 | 90% | 7,65 | 0,176 | 0,100 | 75 500 → ~760 |

- O filtro corta as leituras inúteis em **~99%**: sobra ~1% delas, que é exatamente
  a FPR. Com T = 8 e 90% de negativas: 75 500 leituras inúteis sem filtro, ~760 com.
- **Todos os filtros se comportam igual** aqui (dentro de ~5% nas leituras inúteis):
  todos foram dimensionados para 1%. O hybrid_512 tem um pouco mais (~800), coerente
  com a FPR de 1,06%.
- A baseline nunca lê à toa: 0 leituras inúteis.

### 3.2 Latência das consultas

Latência média por consulta (µs), UUID, T = 8, 90% de consultas negativas:

| N | sem filtro | SBF | hybrid_512 | rDBF | baseline | redução com SBF | memória SBF / baseline |
|---|---|---|---|---|---|---|---|
| 1M | 770,8 | 18,0 | 18,2 | 17,8 | 12,2 | 43× | 1,2 MB / 108 MB |
| 5M | 856,3 | 20,3 | 20,7 | 20,1 | 13,5 | 42× | 6 MB / 563 MB |
| 10M | 882,4 | 21,2 | 21,4 | 20,8 | **250,1** | 42× | 12 MB / 1,1 GB |
| 20M | 737,7 | 18,0 | 18,0 | 17,4 | **492,1** | 41× | 24 MB / 2,3 GB |

A redução depende de quantas leituras o filtro evita, e quase nada do filtro:

| T | negativas | redução da latência com filtro (UUID, todos os N) |
|---|---|---|
| 1 | 50% | ~2× |
| 1 | 90% | ~9× |
| 8 | 50% | ~11× |
| 8 | 90% | ~41–43× |

- **Igual para os quatro tipos de chave**: com T = 8 e 90% de negativas, 42–43× em
  UUID, CPF, CNPJ e URL (1M e 5M).
- **Enquanto cabe na RAM, a baseline é um pouco mais rápida** que os filtros (12,2
  contra 18,0 µs em 1M), porque não faz nenhuma leitura inútil — mas com ~90× a
  memória.
- **Quando deixa de caber, ela perde**: em 20M, o SBF responde em 18,0 µs e a baseline
  em 492 µs, **27× mais lenta**, usando 95× mais memória. O p99 da baseline chega a
  1110 µs contra 128 µs.
- **Cuidado ao comparar N entre si**: o "sem filtro" em 20M (738 µs) é mais rápido
  que em 10M (882 µs) porque as tabelas foram escritas em momentos diferentes e a
  latência do SSD depende da idade dos dados (método, seção 3.5). Compare sempre as
  estruturas dentro do mesmo N; as **contagens** de leitura não são afetadas.

[FIGURA] `vm-nvme/plots/io-uuid-n20000000-HOST-NVME-direct-T8.pdf` (e os outros N, T=1).

---

## 4. Experimento 3 — concorrência

### 4.1 As duas VMs medem o mesmo

Etapa `ponte`, uma thread, VM de 6 vCPUs × campanha oficial (VM de 2 vCPUs), ns/op:

| estrutura | inserção 1M | inserção 20M | busca negativa 20M |
|---|---|---|---|
| SBF | 83,3 → 86,1 (+3,3%) | 187,0 → 187,8 (+0,5%) | 82,9 → 83,0 (+0,1%) |
| PBF | 88,1 → 86,3 (−2,0%) | 190,8 → 188,2 (−1,4%) | 69,8 → 68,4 (−2,0%) |
| hybrid_512 | 21,2 → 21,1 (−0,1%) | 41,4 → 41,9 (+1,0%) | 40,3 → 40,8 (+1,3%) |
| hybrid_c_512 | 42,5 → 42,5 (+0,1%) | 72,2 → 71,9 (−0,4%) | 53,9 → 54,0 (+0,1%) |

Todas as medidas a no máximo ±3,3%, falsos positivos idênticos: as campanhas de uma
thread continuam válidas.

### 4.2 Aceleração com T threads

Aceleração = tempo médio com 1 thread (serial) ÷ tempo médio com T threads, **da
mesma estrutura**. 6 seria o ideal com 6 threads; abaixo de 1, ficou mais lento.

**Inserção:**

| estrutura · estratégia | 1M, T=2 | 1M, T=6 | 20M, T=2 | 20M, T=4 | 20M, T=6 |
|---|---|---|---|---|---|
| PBF · por partição | 1,77 | 3,63 | 1,63 | 3,07 | 3,20 |
| PBF · por chave (atômico) | 1,20 | 2,87 | 1,75 | 3,50 | **5,19** |
| hybrid_512 · atômico | 1,01 | 2,70 | 1,48 | 2,86 | 4,04 |
| hybrid_4096 · atômico | 0,90 | 2,10 | 1,53 | 2,93 | 4,12 |
| hybrid_c_512 · trava por bloco | 1,39 | 3,18 | 1,18 | 1,98 | 2,35 |
| hybrid_c_4096 · trava por bloco | 1,25 | 2,71 | 1,41 | 1,98 | 2,40 |
| hybrid_c_512 · trava global | 0,38 | **0,28** | 0,34 | 0,36 | **0,33** |
| hybrid_c_4096 · trava global | 0,35 | 0,24 | 0,38 | 0,41 | 0,37 |

**Busca negativa** (sem trava, todas as estruturas): 5,2–5,5× com 6 threads em 1M e
4,2–5,4× em 20M.

**Tempo por chave com 6 threads** (inserção, 20M): PBF atômico 36,9 ns; hybrid_512
atômico **10,8 ns**; hybrid_c_512 com trava por bloco 30,6 ns; com trava global
219,5 ns. Com 1 thread, o hybrid_512 leva 43,7 ns.

[FIGURA] `vm6-nvme/plots/threads-uuid-n1000000-04919c3b.pdf` e
`threads-uuid-n20000000-04919c3b.pdf` (aceleração × T, com a linha ideal).

### 4.3 Leitura

1. **A forma de sincronizar pesa mais que a estrutura.** Sem trava (partição, atômico)
   ou com trava fina (uma por bloco), a inserção escala; com uma trava global, ela
   fica **mais lenta que com uma thread** em qualquer T: as threads passam o tempo
   esperando (de 360 mil a 410 mil esperas em 1M; de 6,2 a 8,9 milhões em 20M, para
   20 milhões de inserções). A trava por bloco tem pouquíssimas esperas (~200 no
   hybrid_c_512 com 6 threads; ~2 mil no hybrid_c_4096, que tem 8× menos blocos), ao
   custo de ~40 bytes de mutex por bloco (15,5 MB em 20M para o hybrid_c_512, ~16% da
   memória do filtro).
2. **A divisão por partição do PBF funciona, mas tem um teto.** Sem nenhuma
   sincronização, ela é a melhor estratégia em 1M (3,63×). Mas com k = 7 partições, a
   thread mais carregada fica com ⌈7/T⌉ delas, e a aceleração máxima é 7/⌈7/T⌉ = 3,5
   com 4 ou 6 threads. Medido: 3,53 e 3,63 em 1M, 3,07 e 3,20 em 20M. Dividir por
   chave não tem esse teto e, em 20M, chega a **5,19×**.
3. **O custo do atômico depende de onde o filtro está.** Com uma thread, o OR atômico
   deixa o hybrid_512 em 0,61 do serial em 1M (o filtro está no cache e a instrução
   atômica é o que pesa) e em 0,77 em 20M (o acesso à memória já domina). Por isso as
   estratégias atômicas escalam melhor em 20M (4,0–5,2×) do que em 1M (2,1–2,9×).
4. **Aceleração não é velocidade.** O PBF escala mais (5,19×), mas o hybrid_512 continua
   **3,4× mais rápido** em tempo absoluto com 6 threads (10,8 contra 36,9 ns), porque
   parte de 43,7 ns contra 191 ns. No texto, apresentar as duas medidas juntas.
5. **Buscas escalam quase linearmente** (até 5,5× com 6 threads): só leem e não
   precisam de sincronização.

---

## 5. Respostas aos resultados esperados (seção 5.1 da proposta)

| esperado na proposta | resultado | situação |
|---|---|---|
| Redução substancial do tempo de acesso a dados inexistentes (filtro como *gatekeeper*) | latência 2× a 43× menor, conforme T e a fração de negativas; ~99% das leituras inúteis evitadas; em 20M, 27× mais rápido que a estrutura exata | **confirmado** |
| Complexidade O(k) constante na verificação, mesmo em larga escala | o **número de hashes** é constante (2,05 e 1,00 por busca negativa); o **tempo** não: cresce com N porque o filtro sai do cache (SBF: 49 → 83 ns na busca negativa de 1M a 20M). O híbrido, com 1 linha de cache por chave, cresce menos | **confirmado em operações, não em tempo** |
| Convergência entre FPR prática e teórica | SBF, CBF e PBF dentro de ±0,6%; rDBF +2,8% em 20M (hash de 32 bits); híbrido +1,4% a +6,6% acima da própria fórmula, constante, causado pelo hash duplo dentro do bloco (seção 2.7) | **confirmado nos clássicos; desvios do rDBF e do híbrido explicados** |
| Limites de viabilidade do modelo híbrido | o mais rápido para inserir (4,5× o SBF em 20M) com memória ~igual à do SBF; FPR um pouco acima do alvo (1,06% no bloco de 512 bits); com contadores, deleção 3,7× mais rápida que o CBF; na concorrência, a trava por bloco escala e a global inviabiliza | **viável; o preço é uma FPR ~6% maior no bloco pequeno** |

Objetivos específicos (seção 1.3 da proposta):
- **Redução de chamadas de hash:** rDBF e híbrido fazem 1 chamada por operação contra
  k = 7 na inserção do SBF (2,05 na busca negativa, pela parada antecipada).
- **Memória com e sem contagem:** a contagem multiplica a memória por 4 (CBF 38,4 contra
  SBF 9,6 bits/chave), e permite deleção sem falsos negativos.
- **Memória e busca contra a estrutura determinística:** 95× menos memória; busca
  mais lenta enquanto a tabela cabe na RAM, ordens de grandeza mais rápida quando não
  cabe.

---

## 6. Para a discussão (ligar com as limitações do método, seção 3.12)

- Os números absolutos valem para esta máquina e para um modelo em escala reduzida
  (1 GB de RAM); o que se transfere é a proporção estrutura ÷ RAM e o comportamento.
- A baseline paginando representa um índice exato que deixou de caber na RAM, não uma
  configuração de produção; com consultas concentradas em chaves "quentes" ela
  paginaria menos (as consultas aqui são uniformes).
- Latência absoluta de I/O: comparar só dentro do mesmo N (idade das tabelas no SSD).
- O desvio de FPR do híbrido tem causa identificada (hash duplo dentro de um bloco
  pequeno, seção 2.7). É um custo do desenho escolhido para ter uma só chamada de
  hash, e há correções baratas para discutir (hash duplo melhorado).
- A concorrência cobre PBF e híbrido em UUID, com inserção e busca em fases separadas.

## 7. Figuras sugeridas para o documento

1. Memória × FPR (`vm-nvme/plots/tradeoff-uuid-n20000000-same-fpr.pdf`).
2. Tempo por operação × N com a baseline (`vm-nvme/plots/scaling-uuid-same-fpr-08e7cf3e.pdf`).
3. Tempos por estrutura em 20M (`vm-nvme/plots/bench-uuid-n20000000-same-fpr.pdf`).
4. I/O com T = 8 em 20M (`vm-nvme/plots/io-uuid-n20000000-HOST-NVME-direct-T8.pdf`).
5. Aceleração × threads em 20M (`vm6-nvme/plots/threads-uuid-n20000000-04919c3b.pdf`).
6. Deleção (`vm-nvme-extra/plots/delecao/bench-uuid-n20000000-same-fpr.pdf`).
