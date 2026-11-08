# Materiais e Métodos — conteúdo-base para o capítulo

Material de apoio para redigir o capítulo. Não é o texto final: cada seção
explica **o que** foi usado ou feito, **como** e **por quê**, para ser reescrito
em prosa acadêmica. Os detalhes vêm do código, que é a fonte da verdade.

Base: branch `develop` (commit `0085143`, de 07/11/2026), **protocolo
experimental 5** e schema 5 do banco.

Os resultados vêm de duas campanhas na VM, ambas concluídas e congeladas em
`results/campaigns/`:

| campanha | rótulo | protocolo | commit | execuções | falhas | duração | pasta |
|---|---|---|---|---|---|---|---|
| oficial | `oficial` | 4 | `c2c52ec` | 1786 | 0 | ~8,5 h (06–07/11) | `vm-nvme/` |
| complementar | `colisoes`, `delecao` | 5 | `30fc9a0` | 334 | 0 | ~1 h (07/11) | `vm-nvme-extra/` |

A campanha oficial mede memória, tempos, FPR e I/O. A complementar mede o que
o protocolo 5 acrescentou: colisões estruturais e deleção (seções 3.4 e 3.7).
O protocolo 5 **só acrescenta medições**: a inserção e a busca cronometradas
são o mesmo código de máquina do protocolo 4 (seção 3.4). Mesmo assim, as duas
campanhas nunca são agrupadas numa mesma estatística (seção 3.9).

Marcadores usados:
- **[PROPOSTA]**: onde o desenvolvimento se afastou do TCC I. O capítulo deve
  dizer e justificar cada afastamento. A seção 4 reúne todos.
- **[PREENCHER]**: dado ainda em aberto.

---

## 1. Caracterização da pesquisa

- Pesquisa **aplicada**, **quantitativa** e **experimental**, como na proposta.
- O objeto de estudo são seis estruturas de pertinência:
  - quatro variantes de Filtro de Bloom da literatura (SBF, CBF, PBF, rDBF);
  - um modelo híbrido proposto;
  - uma linha de base determinística.
- Todas foram implementadas do zero, na mesma linguagem, com a mesma interface
  e o mesmo protocolo de medição. Assim as diferenças medidas vêm da estrutura,
  e não da implementação ou do ambiente.
- São dois experimentos:
  1. **Benchmark em memória**: custo de memória, tempo de inserção e de consulta,
     e taxa de falsos positivos de cada estrutura.
  2. **Experimento de I/O**: cada filtro como "porteiro" (*gatekeeper*) na frente
     de tabelas gravadas em disco, medindo quantas leituras de disco ele evita e
     o efeito disso na latência das consultas. É a parte que liga o trabalho ao
     título, "otimização de I/O".

---

## 2. Materiais

### 2.1 Hardware e sistema operacional

Durante o desenvolvimento foram usadas duas máquinas. Os números delas são só
indicativos:

| máquina | CPU | RAM | armazenamento | sistema de arquivos |
|---|---|---|---|---|
| notebook | Intel Core i7-1065G7 (4 núcleos/8 threads, 1,3 GHz base) | 20 GB | SSD NVMe SSSTC CL1-4D256 | btrfs (compressão zstd) sobre LUKS |
| desktop (host da VM) | Intel Core i5-12500 (6 núcleos/12 threads) | 15 GB | SSD NVMe ADATA SM2P41C3 | ext4 |

As campanhas rodaram numa **máquina virtual dedicada**, criada e operada por
`scripts/vm/vm.sh`. A configuração está toda no código:

| item | configuração | motivo |
|---|---|---|
| sistema convidado | Debian 13 (trixie), imagem oficial *genericcloud* (já vem com cloud-init e drivers virtio) | sistema mínimo, sem interface gráfica |
| integridade da imagem | baixada de cloud.debian.org e conferida pelo SHA-512 publicado | garante a imagem oficial |
| virtualização | QEMU/KVM gerenciado pelo libvirt em modo sessão (`qemu:///session`, sem root); CPU repassada ao convidado (`host-passthrough`) | o convidado vê as mesmas instruções do processador real |
| CPU | 2 vCPUs **fixadas em núcleos físicos distintos** (`vcpupin`), e as threads do próprio QEMU em outros núcleos (`emulatorpin`) | nenhuma outra tarefa do host disputa os núcleos do experimento |
| RAM | 1 GB, sem *balloon* (o host não pode tomar memória de volta) | pequena o bastante para a estrutura exata deixar de caber a partir de ~10 milhões de chaves |
| disco do sistema | qcow2, virtio | — |
| disco de testes | 60 GB, imagem raw, ext4 com tabelas de inodes e journal inicializados na criação (`lazy_itable_init=0`, `lazy_journal_init=0`), montado com `noatime` | nenhuma inicialização preguiçosa do sistema de arquivos rodando por trás das medições; sem gravação de horário de acesso |
| swap | 8 GB num disco próprio, imagem raw **pré-alocada** | a paginação nunca paga pela alocação de blocos no host |
| cache do host | `cache=none`, `io=native` nos discos | o O_DIRECT dentro da VM chega ao disco físico, sem passar pelo cache do host |
| identificação dos discos | discos SCSI com modelo e velocidade de rotação declarados (`rotation_rate`) | o convidado registra o tipo real de disco (SSD ou HD); sem isso diria "rotacional" e sem modelo |
| tarefas periódicas | desligadas no convidado: atualizações automáticas (apt), `fstrim`, `e2scrub`, `man-db`, backup do dpkg | nada agendado roda durante uma medição |

**Fluxo de uso:**
1. `create`: cria e configura a VM.
2. `push-code`: envia **só o código commitado** (um *git bundle* do commit
   atual), compila e roda os testes dentro da VM.
3. `data N TIPO T...`: gera o dataset e as tabelas **no host**, com a memória
   limitada a 4 GB (`systemd-run -p MemoryMax`), e copia para a VM com `rsync`.
   Isso fica fora da VM porque precisa de mais RAM do que ela tem e não é medido.
4. `campaign`: inicia a campanha dentro da VM em segundo plano.
5. `pull`: traz o banco de resultados de volta.

Ressalva: no modo sessão do libvirt não é possível travar a RAM da VM na memória
do host. Durante as rodadas é preciso conferir que o processo do QEMU não foi
para o swap do host (campo `VmSwap` em `/proc/<pid>/status`).

**Disco das rodadas:**
- Campanhas oficial e complementar: o SSD NVMe do host.
- Variante em HD: **não realizada nesta etapa** **[PROPOSTA]**. Exige um disco
  vazio dedicado, e o único HD disponível guarda arquivos que não podem ser
  arriscados. Os scripts já aceitam outro disco (`DISK_DIR`, `DISK_MODEL`,
  `DISK_ROTATION`), e o modelo é registrado com os resultados; fica como trabalho
  futuro (seção 3.11).

Por que uma VM:
- **Ambiente limpo e repetível**, sem a interferência do desktop gráfico. No
  desktop, a latência do NVMe variou de ~50 µs para ~86 µs por leitura entre dois
  dias, com os mesmos binários.
- **RAM pequena de verdade.** Em big data a estrutura exata não cabe na memória
  e o sistema pagina; o experimento quer mostrar justamente esse ponto de quebra.
- **Swap em disco, não zram.** O zram é swap comprimido dentro da própria RAM:
  a paginação custa microssegundos em vez de milissegundos, o que não representa
  um servidor de dados.
- **Disco trocável.** A mesma VM pode usar um SSD ou um HD para as tabelas e o
  swap, o que permite mostrar como o ganho do filtro depende do custo de cada
  leitura. As campanhas rodaram no SSD; o HD ficou como trabalho futuro.

### 2.2 Linguagens, compilador e bibliotecas

**C (padrão C99)**: todas as estruturas e os dois experimentos.
- O nome correto do padrão é ISO/IEC 9899:1999 (C99). "ANSI C" costuma designar
  o C89, então vale corrigir no texto **[PROPOSTA]**.
- As justificativas da proposta continuam válidas:
  - não há coletor de lixo para interferir no cronômetro;
  - há operações bit a bit nativas;
  - há tipos de tamanho fixo (`<stdint.h>`);
  - o programador controla o layout da memória, o que permitiu alinhar
    partições e blocos a linhas de cache de 64 bytes.
- O código usa algumas interfaces POSIX/Linux além do C99:
  - `clock_gettime`, `posix_memalign`, `pread`;
  - `O_DIRECT`, `posix_fadvise`, `getrusage`;
  - os arquivos `/proc/self/io` e `/proc/self/statm`.

**GCC** com `-std=c99 -Wall -Wextra -O3`:
- `-O3` é a otimização máxima de desempenho prevista na proposta.
- `-Wall -Wextra` ativa os avisos extras; o código compila sem avisos.
- A compilação é feita com `make`, que também recompila quando um cabeçalho muda.

**Funções de hash** (seção 3.1):
- **MurmurHash2 de 32 bits** (Appleby): implementada do zero.
- **MurmurHash64A** (variante de 64 bits da mesma família): usada só pelo
  modelo híbrido.

Ambas foram validadas contra os valores oficiais de verificação do SMHasher
(o conjunto de testes do próprio autor do MurmurHash): `0x27864C1E` para a de
32 bits e `0x1F0D3804` para a de 64 bits. Com isso a implementação é
comprovadamente igual à de referência.

**uthash** (Troy D. Hanson), para a linha de base:
- É um único cabeçalho de macros e usa a sua própria função de hash padrão
  (Jenkins), não a MurmurHash.
- Isso é coerente: a linha de base representa uma tabela hash "de mercado".

**Python 3**: geração dos datasets, orquestração das execuções, banco de
resultados, tabelas e gráficos.
- Tudo usa **só a biblioteca padrão**, exceto os gráficos.
- O Matplotlib é opcional: fica num ambiente virtual próprio (`make venv`) e só é
  importado quando se pede uma figura.
- Pandas e NumPy não foram necessários **[PROPOSTA]**. O SQLite faz o
  armazenamento e a seleção, e a estatística é simples (média, desvio, intervalo
  de confiança).

**SQLite**: banco de resultados num único arquivo (`results/bloom.db`), sem
servidor (seção 3.8).

**Git**, com o fluxo Git Flow (`main`, `develop` e `feature/*`):
- Além de versionar o código, o git é um instrumento de rastreabilidade.
- Toda execução registra o commit exato do código que rodou (seção 3.8).

**Ferramentas do Linux**, usadas para registrar o ambiente de cada execução:
- `findmnt` e `lsblk` identificam o sistema de arquivos e o disco físico por
  trás do diretório das tabelas, inclusive através de LUKS ou LVM.
- `/proc/cpuinfo`, `/proc/meminfo`, `/proc/swaps` e `/proc/vmstat` dão a CPU, a
  memória e o swap.
- `systemd-detect-virt` detecta se a máquina é virtual.

### 2.3 Datasets

**Quatro tipos de chave**, todos sintéticos. Os três primeiros são as tipologias
da seção 4.3.1 da proposta; CPF e CNPJ são duas variantes do mesmo grupo.

| tipo | formato | tamanho | por que está no estudo |
|---|---|---|---|
| UUID | versão 4, texto com hífens | 36 caracteres fixos | alta entropia: o caso "ideal" de dispersão |
| CPF | 11 dígitos, sem pontuação, com os dois dígitos verificadores corretos | fixo | baixa entropia, chaves muito parecidas entre si: testa o efeito avalanche |
| CNPJ | 14 dígitos, com os dois dígitos verificadores corretos | fixo | idem, com outro comprimento |
| URL | `http(s)://<domínio>.<tld>[/caminho...][?param=valor]` | variável | prefixos repetidos e tamanho heterogêneo (uso típico em proxies e listas de bloqueio) |

**Geração** (`scripts/gen_datasets.py`):
- **CPF e CNPJ** são "estruturalmente válidos": os dígitos verificadores são
  calculados pelo algoritmo oficial (módulo 11 com os pesos de cada documento).
  O Dinexist, portanto, contém CPFs e CNPJs válidos, como a proposta exige.
- **URLs** combinam listas fixas:
  - 12 domínios e 9 TLDs, incluindo `.com.br` e `.net.br`;
  - de 0 a 3 segmentos de caminho, de uma lista de 20 palavras;
  - em 30% das URLs, um parâmetro de consulta.
  Por isso muitas URLs compartilham o prefixo inteiro, que é a característica a
  ser testada.
- **UUIDs** são montados a partir do gerador pseudoaleatório com semente:
  128 bits aleatórios formatados como UUID versão 4.
- O pool de cada tipo não tem chaves repetidas.

**Reprodutibilidade:**
- Toda a geração usa um gerador pseudoaleatório com **semente fixa** (padrão 42).
- A mesma semente gera **arquivos idênticos byte a byte**. Isso foi verificado
  rodando a geração duas vezes com sementes de hash do Python diferentes.
- Para chegar a isso foram corrigidos dois pontos:
  - o UUID usava a aleatoriedade do sistema operacional (`uuid4()`) e ignorava
    a semente;
  - o Dinexist era gravado a partir de um conjunto, cuja ordem muda a cada
    execução do Python.
- Cada dataset grava um `meta.json` com as sementes e as contagens.
- Cada execução registra um **hash SHA-256 do conteúdo** dos arquivos usados
  (seção 3.8).

**Partição** (`scripts/partition.py`): segue o protocolo da seção 4.3.2 da
proposta.
- **Dins**: N chaves sorteadas do pool. São as chaves inseridas.
- **Dexist**: uma amostra de 10% do Dins. Toda consulta a essas chaves deve dar
  "presente"; qualquer "ausente" é um falso negativo, ou seja, um erro de
  implementação.
- **Dinexist**: N chaves **novas**, produzidas pelo mesmo gerador e rejeitadas
  se já estiverem no Dins. Mesmo domínio, nunca inseridas. Toda resposta
  "presente" a essas chaves é um falso positivo.
- Antes de gravar, o script verifica as invariantes:
  - Dins não tem chaves repetidas;
  - Dexist ⊂ Dins;
  - Dinexist ∩ Dins = ∅.

**Volumes:**
- Desenvolvimento: N = 2 mil, 100 mil e 1 milhão.
- Rodadas oficiais: **N = 1, 5, 10 e 20 milhões**. Cada N é um dataset próprio,
  gerado com a mesma semente.

  Por que esse eixo: na VM de 1 GB, a tabela exata (baseline) ocupa 563 MB com
  5 milhões de chaves, 1,13 GB com 10 milhões e 2,27 GB com 20 milhões (medido),
  ou seja, passa da RAM entre 5 e 10 milhões. O SBF fica em 12 MB com 10 milhões. Os dois N menores mostram o
  regime em que tudo cabe na memória, e os dois maiores o regime em que a
  estrutura exata pagina.
- **Custos medidos (UUID)**, todos lineares em N:
  - a partição usa ~305 bytes de RAM por chave (20 milhões: ~6 GB de pico);
  - o dataset ocupa ~78 bytes por chave em disco, e as tabelas de I/O
    ~136 bytes por chave para cada valor de T;
  - somando N = 1, 5, 10 e 20 milhões, são ~13 GB de disco por tipo de chave.
- Datasets e tabelas são gerados no host e copiados para a VM (seção 2.1). A
  geração não é medida.

---

## 3. Métodos

### 3.1 Arquitetura do software

**Interface genérica única** (`include/bf.h`):
- Cada estrutura se registra numa tabela de operações (`bf_ops_t`) com:
  - criar, destruir, inserir, consultar e remover (quando suportado);
  - memória ocupada e número de células;
  - FPR teórica e taxa de ocupação;
  - função de dimensionamento.
- O programa principal, o benchmark e o experimento de I/O percorrem esse
  registro. Nenhum deles tem código específico de um filtro, então **todas as
  estruturas passam exatamente pelo mesmo caminho de medição**.
- É o "orquestrador central com módulos" previsto na seção 4.1 da proposta.

**Um único executável** (`bloom_app`) com vários modos:
- `--demo`: demonstração;
- `--repl`: sessão interativa;
- `--test`: verificação rápida de invariantes;
- `--benchmark`, `--iobench` e `--build-store`: os experimentos;
- `--info`: versão do protocolo e lista de filtros, lida pelo orquestrador.

**Testes unitários em C** (`tests/`, rodados com `make check`): um arquivo por
módulo. Ver a seção 3.10.

**Regras gerais da implementação:**
- Os filtros nunca guardam ponteiros para as chaves, só bits ou contadores.
- A baseline guarda uma **cópia** de cada chave, como um HashSet real que
  armazena os dados.
- A manipulação é sempre bit a bit, sem abstrações.

### 3.2 As estruturas implementadas

Notação: n = chaves inseridas, m = células, k = posições por chave e
p = FPR alvo.

**Hash com semente (SBF, CBF, PBF).** As k posições vêm de k chamadas à
MurmurHash2 com sementes diferentes, `seed_i = (i+1) × 131542391`. A proposta
falava em sementes primas; na implementação as sementes são múltiplos distintos
de uma constante **[PROPOSTA]**. A posição é `h mod m` (no PBF, `h mod s`).

#### SBF — Filtro de Bloom padrão (`src/sbf.c`)
- Vetor de m bits; a inserção liga k bits.
- A consulta **para no primeiro bit desligado**. Por isso uma consulta negativa
  custa em média bem menos que k hashes; o experimento mede isso (≈2 hashes com
  k = 7).
- FPR teórica: `(1 − e^(−kn/m))^k`.

#### CBF — Filtro de Bloom com contagem (`src/cbf.c`)
- Igual ao SBF, mas cada célula é um **contador de 4 bits**: dois por byte, ou
  seja, 4× a memória do SBF.
- A remoção decrementa os k contadores.
- **Saturação em 15:** um contador saturado nunca é decrementado. Ele "perde a
  conta", e decrementá-lo poderia zerar uma posição ainda usada por outra chave,
  causando falso negativo. Assim a remoção nunca causa falso negativo.
- FPR teórica: a mesma do SBF.

#### PBF — Filtro de Bloom particionado (`src/pbf.c`)
- k partições disjuntas de s bits; a i-ésima função de hash só escreve na
  i-ésima partição (como a seção 4.1 da proposta descreve).
- Tudo fica numa única alocação, e **cada partição começa num endereço múltiplo
  de 64 bytes**, o tamanho de uma linha de cache, para que duas partições não
  dividam a mesma linha. O espaço de alinhamento (*padding*) **é contado na
  memória** do PBF.
- FPR teórica: `(1 − (1 − 1/s)^n)^k`.

#### rDBF — Filtro de Bloom r-dimensional (`src/rdbf.c`)
- Implementação 2D que segue a **Equação (1) do robustBF** (Patgiri, 2021), a
  referência de rDBF adotada.
- A estrutura é uma matriz X × Y de células de 64 bits, e uma **única** chamada
  à MurmurHash2 gera todas as coordenadas:

  `h = H(chave)`, `i = h mod X`, `j = h mod Y`, `ρ = h mod 61`,
  `B[i][j] ← B[i][j] OR (1 << ρ)`.

- **β = 61**, o maior primo menor que 64. Os 3 bits restantes de cada célula
  nunca são usados, e esse custo de ≈5% de memória vem do próprio desenho do
  robustBF.
- **X, Y e 61 precisam ser primos distintos.**
  - Pelo Teorema Chinês do Resto, o trio (h mod X, h mod Y, h mod 61) equivale a
    `h mod (X·Y·61)`. Com primos distintos, todas as X·Y·61 combinações são
    alcançáveis e a distribuição é uniforme.
  - Se X = Y, toda chave cai em `i = j` e só a diagonal da matriz seria usada.
  - Este detalhe não está explícito no artigo. Um teste unitário confirma que
    todas as células e todos os 61 bits são alcançados.
- **Escolha de X e Y:** o par de primos distintos (e diferentes de 61) de
  **menor produto** que atende ao tamanho pedido, para que o arredondamento a
  primos desperdice o mínimo de memória (em vez de, por exemplo, tomar o próximo
  primo depois de √m para os dois lados).
- Cada chave liga **exatamente 1 bit**, então k = 1. FPR teórica:
  `1 − (1 − 1/M)^n`, com M = X·Y·61. Para a mesma FPR, o rDBF gasta muito mais
  memória que o SBF (≈ n/p bits contra ≈ 1,44·log₂(1/p)·n), em troca de 1 hash e
  1 acesso à memória por operação. Isso é uma consequência do modelo, não uma
  falha da implementação.
- **Sem remoção.** O robustBF remove com XOR, mas sem contadores isso apaga bits
  compartilhados entre chaves e gera falsos negativos.
- Duas observações para o texto:
  - **Afirmação do artigo:** o robustBF fala em ~1,382 bits por elemento com FPR
    quase nula. Com 1 bit por chave, a fórmula dá FPR ≈ 52% nesse tamanho. Os
    números medidos aqui batem com a fórmula.
  - **Limite do hash de 32 bits:** a posição vem de `h mod (X·Y·61)` com h de 32
    bits, e o espaço X·Y·61 cresce com n (≈10⁹ bits com 10M e ≈2·10⁹ com 20M, perto
    de 2³² ≈ 4,3·10⁹). Medido: FPR de 1,01% com 10M e ~1,03% com 20M para alvo de
    1%, **igual nos quatro tipos de chave**, o que aponta o hash de 32 bits, e não
    as chaves, como causa. Pequeno, mas vale citar.

#### Híbrido — proposta deste trabalho (`src/hybrid.c`)

Combina as três ideias que a proposta queria juntar.

1. **Uma chamada de hash, como no rDBF**, mas MurmurHash64A, de 64 bits
   **[PROPOSTA]**. Desse único valor saem:
   - o **shard** (bloco): os 32 bits altos, levados ao intervalo [0, S);
   - `h1`: os 32 bits baixos;
   - `h2`: uma remistura multiplicativa do mesmo valor, sem nova chamada.

   A proposta previa só MurmurHash2 de 32 bits. O híbrido precisa de 64 bits
   porque o mesmo valor tem de dar ao mesmo tempo a escolha do bloco e as
   posições dentro dele. Com 32 bits, as duas partes ficariam correlacionadas e
   curtas demais em volumes grandes.

2. **Isolamento de memória, como no PBF, mas por chave.**
   - O filtro é dividido em S blocos (*shards*) pequenos, cada um começando numa
     linha de cache (64 bytes, com o padding contado na memória).
   - A chave escolhe **um único bloco**, e todos os seus k bits ficam dentro
     dele. Uma operação toca 1 bloco, em geral 1 ou poucas linhas de cache, em
     vez de k posições espalhadas pela memória.
   - Este é um *blocked Bloom filter*. Ele substitui a ideia de "uma thread por
     partição": no PBF todas as threads tocariam todas as chaves, enquanto aqui
     cada chave pertence a um bloco só, o que basta para uma trava por bloco no
     futuro.

3. **Matriz de primos dentro do bloco, como no rDBF.**
   - Cada bloco é uma matriz p1 × p2 de primos, com C = p1·p2 células.
   - Duas variantes de tamanho:
     - 512 bits: 7 × 73 = 511 células;
     - 4096 bits: 61 × 67 = 4087 células.
     Em cada caso é o par de primos de maior produto que cabe no tamanho.
   - As k posições vêm de **hash duplo**: `g_i = (h1 + i·h2) mod C`, com o passo
     forçado a ser não nulo. Como a matriz é guardada linha a linha, o índice
     linear é o próprio `g_i`.

4. **Mutabilidade, como no CBF**, só nas variantes `hybrid_c_512` e
   `hybrid_c_4096`: contadores de 4 bits com saturação, como no CBF, e com
   remoção.

O resultado são quatro variantes: `hybrid_512`, `hybrid_4096`, `hybrid_c_512` e
`hybrid_c_4096`.

FPR teórica: fórmula de filtro em blocos. O número de chaves por bloco segue uma
Poisson com λ = n/S, e
`FPR = Σ_j P(j; λ) · (1 − (1 − 1/C)^(jk))^k`.
Ela é um pouco maior que a de um SBF do mesmo tamanho, porque alguns blocos
recebem mais chaves que a média; é o preço da localidade.

#### Linha de base determinística (`src/baseline.c`)
- HashSet com a biblioteca uthash, como na proposta.
- Cada entrada guarda uma **cópia** da chave.
- A memória é contada de forma completa:
  - as entradas, com o cabeçalho do uthash e os bytes da chave;
  - a tabela de buckets;
  - a estrutura de controle.
- Exata: zero falsos positivos e zero falsos negativos, por construção.
- Remoção exata (`baseline_remove`): apaga a entrada e libera a memória da
  cópia da chave, sem afetar as outras. Ela serve de referência para a deleção
  dos filtros com contadores (seção 3.4).

### 3.3 Dimensionamento e regimes de comparação

**Dimensionamento** (`<modelo>_params(n, p)`): cada modelo calcula os próprios
parâmetros a partir de n e p, **pela sua própria fórmula de FPR**.
- SBF e CBF testam k de 1 a 32 (com `m = ⌈−k·n / ln(1 − p^(1/k))⌉`) e ficam com
  o menor m.
- PBF inverte a fórmula exata por partição.
- rDBF usa `m = 1 / (1 − (1 − p)^(1/n))`, com k = 1.
- Híbrido faz uma busca binária no número de blocos S, para cada k.

A proposta (seção 4.4.1) previa calcular m e k a partir de n e p. Aqui isso foi
feito **separadamente por modelo**, porque cada um tem uma relação diferente
entre memória e erro.

**Dois regimes**, rodados para todos os filtros:
- **`same_m` (mesma memória):** todos recebem o m e o k que o SBF precisa para
  atingir p. Responde à pergunta "com o mesmo orçamento de células, quanto erra e
  quão rápido é cada um?". O CBF continua pagando 4 bits por célula.
- **`same_fpr` (mesmo erro):** cada um recebe os parâmetros da sua própria
  fórmula. Responde à pergunta "para errar p, quanta memória cada um precisa e
  quão rápido fica?".

A baseline não tem parâmetros e roda uma vez, com o regime marcado como "none".

Por que dois regimes: um só esconderia metade do *trade-off*. Por exemplo, o rDBF
"perde" em `same_m` (FPR ≈ 10% com o orçamento do SBF) e "ganha" em velocidade
em `same_fpr`, pagando ~10× a memória.

### 3.4 Experimento 1 — benchmark em memória (`src/benchmark.c`)

Procedimento para cada **estrutura × regime**:

1. **Processo isolado.** O orquestrador abre um processo novo para cada
   combinação de estrutura e regime. Assim, o que uma estrutura gasta (e
   eventualmente pagina) não é cobrado da próxima.

   Isso foi uma correção de método. Antes, todas rodavam no mesmo processo: num
   limite de 150 MB, a baseline empurrava os dados para o swap e o SBF, medido
   depois, ficava 5× mais lento usando só 1,2 MB. Era um artefato da ordem de
   execução.

2. **Chaves lidas em lotes.** O processo não carrega o dataset na RAM. As chaves
   são lidas do arquivo em lotes, por um buffer fixo de 1 MiB (≈28 mil UUIDs), e
   as partes já lidas são retiradas do cache de páginas do sistema
   (`posix_fadvise`).
   - Com isso, a memória do processo é basicamente a estrutura medida; se algo
     pagina, é a estrutura.
   - Também é mais realista: num sistema real as chaves chegam aos poucos.

3. **Contagem de hashes e de colisões.** Uma passada separada, **não
   cronometrada**, conta:
   - as chamadas de hash na inserção do Dins, na consulta do Dexist e na
     consulta do Dinexist;
   - as **colisões estruturais** da inserção (protocolo 5): cada posição sondada
     que **já estava não nula** soma 1. Isso acontece quando ela foi escrita por
     outra chave ou, raramente, por uma sondagem anterior da mesma chave.

   Nas passadas cronometradas o contador de hashes fica desligado e a inserção é
   a comum, sem contagem.

   Por que a contagem de colisões fica numa **cópia separada da inserção**
   (`<modelo>_insert_counted`): contar dentro da inserção medida mudava o tempo
   medido. A primeira tentativa (o contador dentro da inserção) deixou o
   hybrid_512 6% mais lento; a segunda (um único teste por chave), o SBF 9%. Na
   versão final:
   - `*_insert` e `*_lookup` compilam para **as mesmas instruções** do binário
     anterior (conferido com `objdump`);
   - um teste A/B de tempo (4 rodadas × 5 repetições) ficou dentro do intervalo
     de confiança.

   Validação: nos filtros de bits, o total de colisões bate exatamente com
   k·n − posições ocupadas (conferido com 1 milhão de UUIDs).

4. **Repetições.** Para cada repetição r = 0..R:
   1. cria a estrutura vazia;
   2. **insere** todo o Dins, cronometrando só o laço de inserção de cada lote,
      e não a leitura do arquivo;
   3. lê a memória residente do processo (RSS);
   4. **consulta o Dexist** (tempo e número de "presentes"; o que faltar é falso
      negativo);
   5. **consulta o Dinexist** (tempo e número de "presentes", que são os falsos
      positivos);
   6. lê as faltas de página do intervalo e as **métricas de estado** (memória,
      ocupação e FPR teórica), antes que a deleção altere a estrutura;
   7. **só nas estruturas com remoção** (CBF, hybrid_c_512, hybrid_c_4096 e a
      baseline; protocolo 5):
      - **apaga todo o Dexist**, cronometrado (`delete_ms`, em ns por chave
        apagada);
      - consulta o Dexist de novo: as apagadas que ainda respondem "presente"
        (`del_still_present`). Nos filtros são falsos positivos ou contadores
        saturados; na baseline deve ser 0;
      - consulta todo o Dins: as chaves **não apagadas** que deixaram de
        responder "presente" são **falsos negativos causados pela deleção**
        (`fn_after_delete`), e têm que ser 0. Como o Dexist está contido no Dins
        e não tem repetições, essa conta é exata: presentes no Dins = não
        apagadas encontradas + apagadas ainda presentes;
   8. destrói a estrutura.

   A deleção vem depois das buscas e da contagem de faltas de página, então as
   outras colunas mantêm o mesmo significado do protocolo 4.

   A **repetição 0 é aquecimento** e não é gravada: ela aquece caches, TLB e a
   frequência da CPU.

**Cronômetro:** `clock_gettime(CLOCK_MONOTONIC)`. É um relógio que não sofre
ajustes de hora, com resolução de nanossegundos. Os tempos totais são divididos
pelo número de operações e reportados em **ns por operação**.

### 3.5 Experimento 2 — filtros como porteiros de I/O (`src/store.c`, `src/iobench.c`)

**Modelo do sistema.** Simula o caminho de leitura de uma árvore LSM (o desenho
de RocksDB, Cassandra e LevelDB), que é onde Filtros de Bloom são usados na
prática:
- o Dins é dividido em **T lotes contíguos**, como T "descargas" sucessivas de
  dados para o disco;
- cada lote vira uma **tabela ordenada em disco** (no estilo SSTable) com **o
  seu próprio filtro**, dimensionado para o tamanho do lote;
- uma consulta procura **da tabela mais nova para a mais antiga** e para ao
  encontrar a chave;
- antes de cada tabela, o filtro é consultado, e **o disco só é lido se o filtro
  disser "talvez"**.

**Formato da tabela** (`store.c`): blocos de 4 KB.
- Bloco 0: cabeçalho.
- Blocos de dados: registros ordenados `[tamanho][chave][valor]`, com um valor
  sintético de 100 bytes por padrão.
- No fim: um índice esparso com a primeira chave de cada bloco.

O índice fica na RAM depois de aberto. Uma busca faz uma busca binária no índice
e **exatamente uma leitura de 4 KB** (`pread`). Essa leitura é a unidade de I/O
que o filtro tenta evitar.

**Construção separada.** As tabelas são construídas antes, numa etapa **não
medida** (`--build-store`), porque ordenar exige todas as chaves na RAM.
- A gravação é atômica: arquivo temporário, `fsync` e renomeação.
- O cabeçalho guarda uma impressão digital do conjunto de chaves. Tabelas
  existentes são **reaproveitadas** enquanto as chaves não mudarem.
- Isso também evita desgastar o SSD, que só se desgasta com escrita; as
  consultas só leem.

**Leitura direta do disco (`O_DIRECT`).** Sem isso, depois da primeira passada
as tabelas ficariam no cache de páginas do sistema, e o experimento mediria a
RAM em vez do disco.
- Há um modo `--cached` para comparar.
- **Armadilha encontrada:** no btrfs com compressão, o `O_DIRECT` é ignorado
  silenciosamente e as leituras vêm do cache (~3 µs em vez de ~100 µs).
- O programa desliga a compressão no diretório das tabelas e **confere** o volume
  realmente lido do dispositivo (`/proc/self/io`). Se ele for bem menor que
  leituras × 4 KB, o programa avisa.
- Em ext4 ou xfs o problema não existe.

**Carga de consultas:**
- Q consultas, das quais uma fração `neg` é de chaves **inexistentes** (sorteadas
  do Dinexist) e o resto de chaves existentes (sorteadas do Dexist).
- O sorteio usa xorshift64* com semente fixa e depois um embaralhamento de
  Fisher–Yates, então **a mesma sequência é usada para todos os porteiros**.
- **Cada repetição tem a sua própria amostra de Q consultas** (protocolo 4). São
  sorteadas R + 1 amostras de uma vez, uma para o aquecimento e uma para cada
  repetição gravada, e só essas chaves ficam na memória. A repetição r de todos
  os porteiros usa a mesma amostra r.

  Por que: com uma amostra só, repetida, o aquecimento trazia para a RAM as
  páginas que aquelas consultas usam, e nas repetições gravadas uma estrutura que
  não cabe na memória parecia não paginar. No piloto na VM (10 milhões de chaves,
  T = 1, 90% de negativas), a baseline foi de 0 para ~4 850 faltas de página
  maiores por repetição e de 12,5 µs para ~49 µs de latência média. O SBF
  continuou com 0 faltas e 13,5 µs. Repetir a amostra escondia exatamente o
  efeito que o experimento quer medir.
- Parâmetros da campanha: Q = 10 000 consultas, T ∈ {1, 8} e
  neg ∈ {0,5; 0,9}.
- Mais negativas e mais tabelas significam mais leituras evitáveis.

**Porteiros comparados:**
- **"none"** (controle): toda tabela visitada é lida.
- **Cada filtro probabilístico:** pula a leitura quando responde "ausente"; cada
  falso positivo custa uma leitura inútil.
- **A baseline exata:** nunca desperdiça leitura, mas custa muita RAM.

Cada porteiro roda num processo próprio. Os filtros são construídos lendo o Dins
em lotes, e só as chaves sorteadas para consulta ficam na memória.

**Medição** (para cada repetição, com a repetição 0 de aquecimento):
- latência de **cada consulta** individual, de onde saem a média, a mediana
  (p50) e o percentil 99;
- leituras de disco e **leituras inúteis**, que acontecem quando o filtro diz
  "talvez" e a tabela não tem a chave;
- bytes realmente lidos do dispositivo;
- **erros**: toda resposta é conferida com o gabarito, e qualquer diferença é
  contada.

**Idade das tabelas no SSD.** A latência de uma leitura depende de **quando a
tabela foi escrita no SSD**, e não de N. Foi medido na VM com o controle `none`
e T = 1:
- uma tabela de 1M lia a ~100 µs por leitura;
- reescrita com conteúdo idêntico, passou a ~59 µs, enquanto a de 5M, intacta,
  continuou estável em ~114 µs;
- depois de ~40 min, e de ~30 min de escrita de outros dados, a tabela reescrita
  já estava em 82 µs.

O mecanismo não foi confirmado; pode ser o cache SLC ou o mapeamento interno do
SSD. Isso também explica os ~57 µs do 1M no teste de 05/11 (a tabela tinha
acabado de ser copiada) e provavelmente a subida de ~50 para ~86 µs vista no
desktop.

Procedimento adotado por causa disso:
- nenhuma escrita de dados novos depois que a campanha começa;
- a campanha só começa com as latências estáveis;
- cada porteiro é comparado com o controle `none` **da mesma rodada, mesmo tipo
  e mesmo N**, e nunca entre N ou entre tipos de chave.

O efeito muda o **tempo** de cada leitura, mas não o **número** de leituras. Por
isso leituras por consulta, leituras inúteis e FPR não são afetadas; só a
latência absoluta é. Na campanha oficial, por exemplo, o `none` com 20M (738 µs)
ficou mais rápido que com 10M (882 µs): eram tabelas escritas em momentos
diferentes.

### 3.6 Variáveis independentes

| variável | valores | onde |
|---|---|---|
| estrutura | sbf, cbf, pbf, rdbf, hybrid_512, hybrid_4096, hybrid_c_512, hybrid_c_4096, baseline | ambos |
| tipo de chave | uuid, cpf, cnpj, url | ambos |
| N (tamanho do Dins) | 1, 5, 10 e 20 milhões | ambos |
| p (FPR alvo) | 0,01 | ambos |
| regime de dimensionamento | same_m, same_fpr | benchmark |
| T (tabelas) | 1, 8 | I/O |
| fração de consultas negativas | 0,5; 0,9 | I/O |
| consultas por repetição (Q) | 10 000 | I/O |
| modo de leitura | O_DIRECT | I/O |
| dispositivo | SSD NVMe (HD não realizado, seção 3.11) | ambos (swap) e I/O (tabelas) |
| RAM disponível | 1 GB (VM) | ambos |

**Campanha oficial** (protocolo 4, executada de 06 a 07/11/2026 no NVMe, rótulo
`oficial`). Cada linha diz quantas repetições gravadas há por execução (R) e
quantas vezes a grade inteira é rodada (rodadas). O plano está em
`scripts/vm/official.sh`:
- antes de começar, o script **confere todos os datasets e tabelas**; se faltar
  qualquer item, nada roda, para que uma lacuna não apareça horas depois;
- as etapas vão da mais barata para a mais cara, então uma interrupção ainda
  deixa a maior parte dos resultados.

| tipo | N | o que roda | R × rodadas |
|---|---|---|---|
| UUID | 1, 5, 10 e 20M | filtros: benchmark e I/O | 5 × 3 |
| UUID | 1 e 5M | baseline: benchmark e I/O | 5 × 3 |
| UUID | 10 e 20M | baseline: benchmark e I/O | 3 × 1 |
| CPF, CNPJ, URL | 1 e 5M | tudo, com baseline e I/O | 5 × 3 |
| CPF, CNPJ, URL | 10 e 20M | só filtros, só benchmark | 2 × 1 |

Duração real: ~8,5 h (a estimativa era de ~7 h), 1786 execuções, 0 falhas. A
etapa mais longa foi a baseline UUID com 20M: só a execução do benchmark levou
~3 h 37 min (aquecimento + 3 repetições, ~54 min por passada; a estimativa era
de ~35 min).

**Campanha complementar** (protocolo 5, 07/11/2026, `scripts/vm/extra.sh`,
rótulos `colisoes` e `delecao`; ~1 h, 334 execuções, 0 falhas):

| rótulo | tipo | N | estruturas | R × rodadas | por quê |
|---|---|---|---|---|---|
| `colisoes` | UUID, CPF, CNPJ, URL | 1, 5, 10 e 20M | todos os filtros, só benchmark | 1 × 1 | as colisões vêm da passada não cronometrada e são determinísticas |
| `delecao` | UUID | 1, 5, 10 e 20M | CBF, hybrid_c_512, hybrid_c_4096 | 5 × 3 | o tempo de deleção precisa de repetições |
| `delecao` | UUID | 1 e 5M | baseline | 5 × 3 | apagar exige N consultas a mais; com 10M e 20M ela pagina e levaria horas |

Justificativas da grade:
- **Baseline paginando com 3 × 1.** Quando ela pagina, cada operação fica de 50
  a 120 vezes mais lenta (no piloto, a inserção foi de ~350 ns/op com 5M para
  ~16 400 ns/op com 10M, e cada falta de página custou ~80 µs). Com 5 × 3, só a UUID
  levaria ~18 h. Um efeito desse tamanho aparece mesmo com um intervalo de
  confiança largo.
  - Por que não 2 repetições: com n = 2 o IC multiplica o desvio por
    t = 12,7; com 3, por 4,3.
  - O texto deve declarar que essas 3 repetições são do mesmo processo e não
    captam a variação entre rodadas.
- **Os outros tipos de chave com menos N.** O capítulo 4.3.1 da proposta usa os
  tipos de chave para testar a dispersão do hash, e isso não depende de
  paginação. A FPR não varia entre repetições, então 2 × 1 basta em 10M e 20M.
  Sem I/O também não são necessárias as tabelas (~8 GB por tipo em 20M).
  - O texto deve declarar que a baseline paginando e o I/O em 10M e 20M só
    existem para UUID.

### 3.7 Variáveis dependentes (métricas)

**Benchmark** (uma linha por estrutura × regime × repetição):

| métrica | definição | relação com a proposta (seção 4.4.2) |
|---|---|---|
| `mem_bytes`, `bits_per_key` | memória alocada pela estrutura (com padding e contadores) e 8·mem/n | 1. consumo de memória |
| `rss_kb` | memória residente do processo após as inserções | complementa a 1. (memória real, não só a alocada) |
| `fpr` | falsos positivos / tamanho do Dinexist | 2. FPR prática |
| `fpr_theory` | FPR esperada pela fórmula de cada modelo | 2. comparação teoria × prática |
| `fn` | falsos negativos no Dexist (deve ser 0) | validação, seção 4.3.2 |
| `insert_ms` (→ ns/op) | tempo de inserção do Dins | 3. *build time* |
| `lookup_exist_ms`, `lookup_inexist_ms` (→ ns/op) | tempo de consulta positiva e negativa | 4. *lookup time* |
| `hash_insert`, `hash_lookup_*` | chamadas de hash por operação | mostra a economia do rDBF e do híbrido e a parada antecipada do SBF |
| `collisions`, `collision_rate` | sondagens de inserção em posições já não nulas; a taxa divide pelo total de posições escritas (n·k; no rDBF, n) | 5. taxa de colisões estruturais (protocolo 5) |
| `fill_ratio` | fração de células não nulas | complementa a 5. |
| `delete_ms` (→ ns por chave apagada) | tempo para apagar o Dexist (estruturas com remoção) | deleção, prometida na seção 4.1 (protocolo 5) |
| `del_still_present` | chaves apagadas que ainda respondem "presente" | efeito da deleção (filtros: falsos positivos ou contadores saturados) |
| `fn_after_delete` | chaves não apagadas que deixaram de ser encontradas (deve ser 0) | a deleção não pode gerar falso negativo |
| `majflt`, `minflt` | faltas de página maiores (precisaram de disco/swap) e menores | indicam paginação |

Por execução (processo inteiro), registrado pelo orquestrador:
- RSS máximo;
- tempo de CPU de usuário e de sistema;
- trocas de contexto voluntárias e involuntárias. As involuntárias indicam
  disputa de CPU com outros processos.
- páginas de swap lidas e gravadas durante a execução;
- tipo de swap (disco, zram, nenhum), tamanho, e **modelo e tipo do disco do
  swap**. O custo de paginar depende do disco: ~80 µs por falta no NVMe, contra
  ~10 ms esperados num HD.

**Taxa de colisões estruturais** (métrica 5 da proposta). Medida diretamente a
partir do protocolo 5, como a proposta descreve: quantas vezes chaves distintas
escrevem na mesma posição.
- Definição: uma colisão é uma sondagem de inserção numa posição que já estava
  não nula, e a taxa é colisões ÷ posições escritas (n·k).
- Tem fórmula teórica para comparar. Num filtro de m posições com k·n
  escritas uniformes, a taxa esperada é `1 − m·(1 − e^(−kn/m)) / (kn)`. Por
  exemplo, no SBF com 10M o previsto é 29,019% e o medido 29,023%.
- Como o dimensionamento mantém k·n/m constante, a taxa é a mesma em todos os N
  e tipos de chave. Uma diferença entre tipos indicaria agrupamento causado pela
  chave, que é a pergunta da métrica original (a dispersão é uniforme?).
- Duas medidas complementam essa:
  - a **ocupação** (`fill_ratio`): as colisões são o que faz a ocupação ficar
    abaixo de k·n/m;
  - a **FPR medida contra a teórica**: um agrupamento anormal, como hash ruim
    ou bloco sobrecarregado, apareceria como FPR acima da fórmula.

**Experimento de I/O** (uma linha por porteiro × repetição):
- leituras por consulta;
- leituras inúteis;
- bytes lidos do dispositivo;
- latência média, p50 e p99 (µs);
- tempo total;
- memória dos filtros (`gk_mem_bytes`) e do índice esparso;
- k usado;
- erros (deve ser 0);
- RSS e faltas de página durante as consultas.

O `pread` com `O_DIRECT` não gera falta de página. Por isso, faltas maiores
nesse trecho significam que o próprio porteiro foi paginado.

### 3.8 Orquestração, registro e rastreabilidade

**Orquestrador** (`scripts/run.py`). Executa a grade de parâmetros: um processo
por estrutura × regime no benchmark, ou por estrutura × T × neg no I/O.
- Com `--repeat R`, a **grade inteira** é repetida R vezes, então as repetições
  de uma mesma configuração ficam espalhadas no tempo, e não em sequência. Isso
  dilui efeitos transitórios da máquina, como aquecimento térmico ou outro
  processo.
- Antes de rodar, ele recompila o programa.
- Ele **recusa rodar com alterações não commitadas** em arquivos versionados.
  Há a opção `--allow-dirty`, mas a execução fica marcada e a análise a ignora.

**Banco de resultados** (SQLite, `scripts/resultsdb.py`):
- Cada execução vira uma linha em `runs`, com:
  - **código**: versão do protocolo, commit do git, indicação de alterações não
    commitadas, hash SHA-256 do executável e versão do compilador;
  - **máquina**: host, modelo e número de CPUs, RAM total, limite de memória do
    cgroup, kernel, virtualização, tipo e tamanho do swap;
  - **disco**: sistema de arquivos, opções de montagem, dispositivo, modelo e se
    é rotacional;
  - **dataset**: tipo, hash SHA-256 do conteúdo, metadados e contagens;
  - **parâmetros** e **recursos** consumidos.
- As linhas do CSV de cada execução vão para `filter_results` (benchmark) ou
  `io_results` (I/O).
- O CSV bruto também é guardado (`results/raw/`). É o registro primário, e o
  banco pode ser reconstruído a partir dele.

**Comparabilidade:**
- **`config_key`**: hash do que foi medido (protocolo, conteúdo do dataset,
  parâmetros).
- **`env_key`**: hash de onde foi medido:
  - host, CPU, RAM total, limite de RAM do cgroup e virtualização;
  - sistema de arquivos e disco das tabelas;
  - tipo e tamanho do swap, e modelo e rotação do disco do swap.

  O disco do swap entrou no schema 4. Sem ele, execuções do benchmark que
  paginam num SSD e num HD seriam agrupadas juntas, porque o benchmark não usa as
  tabelas e não tinha nenhuma informação de disco.
- Duas execuções **só são agrupadas** se tiverem as duas chaves iguais.
- **Versão do protocolo** (`BF_PROTOCOL_VERSION`): incrementada toda vez que uma
  mudança torna execuções antigas incomparáveis (fórmula, método de medição,
  colunas). Está em **5**:
  - 1: versão inicial;
  - 2: colunas de memória e paginação;
  - 3: um processo por estrutura e chaves lidas em lotes;
  - 4: amostra nova de consultas a cada repetição no I/O;
  - 5: colisões estruturais e deleção (só colunas novas; inserção e busca
    medidas inalteradas).

  A campanha oficial usa só a versão 4; a complementar, só a 5.
- **Na VM, o código também é rastreado.** Só código commitado chega à VM (o
  `push-code` recusa alterações não commitadas). O `run.py` roda dentro da VM e
  registra o commit e o hash do executável compilado lá.
- **O mesmo código** é identificado pelo commit do git, que é um hash de todos os
  arquivos versionados, mais a marca de alterações não commitadas. O hash do
  executável distingue o mesmo commit compilado com outro compilador ou outras
  opções.

### 3.9 Análise estatística e apresentação (`scripts/analyze.py`, `scripts/plots.py`)

**Grupos.** Uma linha de resultado por tipo × N × regime × estrutura no benchmark,
ou por tipo × N × disco × modo × T × neg × porteiro no I/O, sempre dentro do mesmo
`config_key` e `env_key`. Por padrão, só a versão de protocolo mais recente e só
execuções sem alterações não commitadas.

**Amostras.** Cada repetição gravada de cada execução selecionada é uma amostra.
Para cada métrica, o programa calcula:
- a média;
- o desvio-padrão amostral;
- o **intervalo de confiança de 95%** pela distribuição t de Student:
  `IC = t(0,975; n−1) · s / √n`.

**No benchmark, FPR e contagens não variam entre repetições**, porque as chaves
e as sementes são fixas. A variação está nos tempos. Isso é esperado e vale
mencionar. No I/O, as leituras inúteis variam um pouco entre repetições, porque
cada repetição sorteia consultas novas.

**Repetições e rodadas.** As R repetições de uma execução acontecem dentro do
mesmo processo, e as rodadas repetem a grade inteira, espalhadas pela campanha.
O IC junta as duas fontes de variação. Onde há só uma rodada (baseline paginando
e os outros tipos com 10M e 20M), ele mede só a variação dentro de um processo,
e isso deve ser dito junto com o resultado.

**Saídas:**
- tabela no terminal;
- CSV com média, desvio e IC;
- **tabela LaTeX** (booktabs) para o documento;
- **figuras** (PDF por padrão) geradas dos **mesmos agregados** das tabelas, então
  figura e tabela nunca divergem:
  - eixo logarítmico automático quando os valores variam mais de 30×;
  - figuras de máquinas ou discos diferentes nunca se misturam.

**Protocolos não se misturam.** A análise seleciona por padrão só a versão de
protocolo mais nova de cada banco, e o `config_key` inclui o protocolo. Então as
campanhas oficial (4) e complementar (5) nunca entram na mesma média, mesmo que o
protocolo 5 só acrescente colunas.

**Congelamento.** Quando uma campanha termina, `analyze.py freeze` copia o banco
para `results/campaigns/<nome>/`. Essa pasta é **versionada no git**, junto com
os CSVs brutos de cada execução, os logs da campanha e do host e as tabelas e
figuras geradas dela. Junto com o commit, isso permite reproduzir cada tabela e
figura do documento. O banco de trabalho (`results/bloom.db`) fica fora do git,
então rodadas novas nunca alteram uma campanha fechada.

**Análise só para leitura.** O `analyze.py` abre os bancos **em modo somente
leitura** (`resultsdb.connect_readonly`, SQLite com `mode=ro`), sem script de
schema e sem migração. Um banco congelado continua idêntico byte a byte depois
de qualquer consulta. Colunas que um schema mais antigo não tem são lidas como
vazias. Só o `run.py`, que grava execuções novas, cria ou migra o schema.

Isso foi uma correção: antes, só de consultar um banco congelado de schema 4 com
o código de schema 5, o arquivo versionado era migrado no próprio lugar.

### 3.10 Validação da implementação

- **Hashes:** valores oficiais de verificação do SMHasher para as duas funções.
- **Testes unitários por estrutura** (`make check`):
  - nenhum falso negativo;
  - FPR medida dentro da margem da teórica (±20%, no rDBF e no híbrido);
  - geometria (primos distintos no rDBF, alinhamento de 64 bytes no PBF e no
    híbrido, cobertura de todas as células e bits no rDBF);
  - dimensionamento mínimo e remoção (CBF, híbridos com contadores e baseline);
  - a tabela em disco: exatamente uma leitura por busca, nos modos com cache e
    direto, e rejeição de chaves duplicadas;
  - a leitura das chaves em lotes.
- **Verificação de dimensionamento** (`make test`): para cada modelo, a FPR
  teórica com os parâmetros calculados fica ≤ p, e ≥ 0,9p quando n ≥ 10⁶ (sem
  superdimensionar). Pela interface genérica, também confere que:
  - reinserir a mesma chave gera exatamente k colisões (1 no rDBF), e a chave
    continua sendo encontrada;
  - apagar metade das chaves não gera falso negativo nas restantes, e na
    baseline as apagadas somem.
- **Em todo experimento:**
  - o benchmark registra os falsos negativos, que devem ser 0, antes e depois
    da deleção;
  - o I/O confere cada resposta com o gabarito e registra os erros, que devem
    ser 0;
  - o protocolo de partição verifica as suas invariantes antes de gravar.

### 3.11 Limitações e ameaças à validade (para a discussão)

- **Dados sintéticos.** Os geradores imitam formato e entropia, não a
  distribuição de dados reais. Por exemplo, os CPFs são uniformes, sem a
  concentração regional mencionada na proposta.
- **Uma linha de execução só.** O paralelismo com pthreads, previsto na proposta,
  foi adiado para depois das rodadas principais **[PROPOSTA]**.
- **Modelo em escala reduzida.** A VM tem 1 GB de RAM, e as proporções
  reproduzem as de um sistema de big data, não os tamanhos:
  - baseline ÷ RAM vai de 0,1× (1M) a 2,3× (20M);
  - tabelas em disco ÷ RAM vai de 0,14× a 2,9×.

  O que se transfere para sistemas reais é a **proporção** (o que acontece quando
  o índice exato deixa de caber na RAM), não os tempos absolutos.
- **A baseline paginando não é uma configuração de produção.** Ninguém deixaria
  uma tabela hash paginar em swap. Ela representa o custo de um índice exato em
  RAM que deixou de caber nela, que é a situação que os filtros evitam.
- **Consultas uniformes.** As consultas são sorteadas uniformemente, sem chaves
  "quentes". Isso é **desfavorável à baseline**: com uma distribuição
  concentrada, as páginas das chaves frequentes ficariam na RAM e ela paginaria
  menos. Deve ser declarado junto com os resultados da baseline.
- **Idade das tabelas no SSD.** A latência absoluta de leitura depende de quando
  cada tabela foi escrita (seção 3.5). Por isso as comparações são sempre entre
  estruturas no mesmo tipo e N, nunca entre N. Contagens de leitura e FPR não
  são afetadas.
- **Deleção com cobertura parcial.** Com repetições, medida só para UUID, e a
  baseline só em 1M e 5M (seção 3.6). Para CPF, CNPJ e URL há uma repetição de
  cada, vinda da rodada `colisoes` (os filtros com remoção apagam o Dexist em toda
  execução), também com 0 falsos negativos. Apaga-se o Dexist (10% do Dins) de uma vez, depois das
  inserções, e não intercalado com elas.
- **O modelo de I/O é simplificado:** um nível, sem compactação, um bloco por
  consulta e índice sempre na RAM. Ele isola o efeito do filtro, mas não é um
  banco completo.
- **Ruído do ambiente.** Mitigado pela VM dedicada, pelas vCPUs fixas, pelas
  repetições espalhadas no tempo e pelo aquecimento. As trocas de contexto
  involuntárias ficam registradas como indício. No desktop, sem esses
  cuidados, a latência do NVMe variou de ~50 para ~86 µs por leitura entre dois
  dias, com os mesmos binários.
- **Cobertura desigual da grade.** A baseline paginando e o I/O em 10M e 20M só
  existem para UUID, e a baseline paginando tem 3 repetições de uma só rodada
  (seção 3.6).
- **RAM da VM não travada no host.** O modo sessão do libvirt não permite.
  Mitigado por verificação: o log do host registrou `VmSwap: 0 kB` para o QEMU
  no início e no fim da campanha oficial (`vm-nvme/logs/oficial-host.log`). São
  só dois instantes, não uma medição contínua.
- **HD.** O experimento em disco rotacional não foi feito: depende de um disco
  vazio dedicado, que não estava disponível. Num HD, cada falta de página custa ~10 ms (contra ~80 µs no
  NVMe), e a baseline com 10M passaria de ~19 h por repetição, então precisaria
  de um N logo acima da RAM ou de um limite de operações.
- **Hash de 32 bits no rDBF:** a FPR sobe de 1,00% para ~1,03% com 20M (seção 3.2); com N bem maiores o efeito cresceria.

---

## 4. Resumo das diferenças em relação à proposta (TCC I)

| proposta | o que foi feito | por quê |
|---|---|---|
| "ANSI C99" | C99 (ISO/IEC 9899:1999) + POSIX | nome correto do padrão; O_DIRECT, pread etc. são POSIX/Linux |
| Só MurmurHash2 32 bits | MurmurHash2 32 bits em SBF/CBF/PBF/rDBF; MurmurHash64A no híbrido | o híbrido tira o bloco e as posições de um só hash e precisa de 64 bits independentes |
| Sementes primas | sementes `(i+1)·131542391` | só precisam ser distintas; o efeito avalanche da MurmurHash descorrelaciona as saídas |
| rDBF "matriz de primos, 1 hash" | rDBF seguindo a Eq. (1) do robustBF (células de 64 bits, β = 61, X ≠ Y) | fidelidade a uma referência publicada; X ≠ Y é necessário (Teorema Chinês do Resto) |
| Híbrido "rDBF + PBF + CBF" | filtro em blocos por chave, matriz de primos por bloco, hash duplo; com e sem contadores | o particionamento por função de hash não isola nada (toda chave toca todas as partições); por chave, sim |
| m e k calculados de n e p | uma função de dimensionamento por modelo + dois regimes (same_m, same_fpr) | modelos diferentes têm relações diferentes entre memória e erro |
| "Simular gargalos de I/O" | experimento de porteiro sobre tabelas no estilo SSTable, com O_DIRECT | torna concreta a ligação com o título (leituras evitadas) |
| Taxa de colisões estruturais "nas variantes modificadas" | medida em todos os filtros (protocolo 5), numa passada não cronometrada, mais ocupação e FPR medida × teórica | todas as estruturas comparáveis; tem fórmula teórica; contar dentro da inserção medida alterava o tempo |
| Avaliar a deleção | deleção cronometrada e conferida no CBF, nos híbridos com contadores e na baseline (protocolo 5) | completa a promessa da seção 4.1 da proposta; a baseline dá a referência exata |
| Memória em KB/MB | bytes alocados, bits/chave, RSS e faltas de página | bits/chave compara estruturas; RSS e faltas mostram a paginação |
| Pandas, NumPy, Matplotlib | biblioteca padrão do Python + SQLite; Matplotlib opcional | menos dependências; o banco organiza muitas execuções |
| pthreads com mutexes | adiado | prioridade às rodadas em escala; o híbrido já foi desenhado para uma trava por bloco |
| — | um processo por estrutura e chaves lidas em lotes | sem isso, a paginação de uma estrutura era cobrada da seguinte |
| — | amostra nova de consultas a cada repetição no I/O | uma amostra repetida ficava na RAM e escondia a paginação |
| Testes na máquina de desenvolvimento | VM dedicada, configurada por script (1 GB, vCPUs fixas, swap em disco) | ambiente controlado e repetível, com RAM pequena de verdade |

---

## 5. Para fechar o capítulo

Já conhecido (dos bancos congelados e dos logs):
- [x] Convidado: Debian 13, kernel `6.12.115+deb13.1-cloud-amd64`, GCC 14.2.0,
      2 vCPUs, ~968 MiB de RAM visível, KVM. Host: Pop!_OS, kernel 7.2.6, perfil
      de energia "Performance", Intel Core i5-12500.
- [x] Duração: oficial ~8,5 h; complementar ~1 h. Baseline UUID 20M: ~3 h 37 min
      só no benchmark.
- [x] Commits e bancos: oficial `c2c52ec` (protocolo 4) em
      `results/campaigns/vm-nvme/`; complementar `30fc9a0` (protocolo 5) em
      `results/campaigns/vm-nvme-extra/`.
- [x] QEMU sem swap no host no início e no fim da campanha oficial.
- [x] Deleção e colisões medidas.

- [x] Python 3.13.5 na VM. vCPUs fixadas nos processadores lógicos 8 e 10 (núcleos
      físicos distintos do i5-12500, cujos pares de threads são 8/9 e 10/11) e o
      QEMU em 6–7. Conferido na VM em 07/11 (`virsh vcpupin`, `emulatorpin`); nada
      disso é registrado no banco, então citar como configuração do script.
- [x] HD: não realizado nesta etapa (seções 2.1 e 3.11).

Ainda falta:
- [ ] Decidir se o experimento com threads entra.
