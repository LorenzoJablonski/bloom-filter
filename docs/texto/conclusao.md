# Conclusão — conteúdo-base para o capítulo

Material de apoio para redigir a conclusão. Não é o texto final: organiza **o que
responder**, **com que evidência** (sempre apontando para a seção de
`docs/texto/resultados.md` de onde vem o número) e **o que fica para depois**. Os
números aqui são os mesmos do capítulo de resultados; se algum mudar lá, mudar aqui.

---

## 1. Retomar o problema

A proposta (seção 1.1) parte de três fatos:
- o gargalo dos sistemas de dados é o I/O, e confirmar que um dado **não existe**
  lendo o disco acumula latência;
- índices exatos em RAM resolvem isso enquanto cabem nela; em big data deixam de
  caber, e a paginação degrada ainda mais o desempenho;
- o Filtro de Bloom troca um erro pequeno e controlado por muito menos memória, mas a
  versão clássica tem três limitações: **tamanho fixo**, **sem deleção** e **custo
  linear de múltiplos hashes**.

O problema central era **como integrar, aplicar e avaliar arquiteturas otimizadas —
matrizes e particionamento — para extrair a máxima eficiência com o menor índice de
falhas**. O objetivo geral, implementar e avaliar empiricamente uma arquitetura
otimizada baseada no Filtro de Bloom, demonstrando economia de recursos.

## 2. O que foi feito (um parágrafo no texto)

- Cinco estruturas probabilísticas implementadas do zero em C99 (SBF, CBF, PBF, rDBF
  e o modelo híbrido proposto, com e sem contadores) e uma linha de base exata, todas
  pela mesma interface e o mesmo protocolo de medição.
- Três experimentos: benchmark em memória, filtro como porteiro de I/O sobre tabelas
  em disco, e concorrência com várias threads.
- Quatro tipos de chave (UUID, CPF, CNPJ, URL), de 1 a 20 milhões de chaves, numa VM
  controlada de 1 GB de RAM, em 2396 execuções registradas e versionadas.

---

## 3. Conclusões (cada uma com a evidência)

**C1. O filtro cumpre o papel de porteiro de I/O, e o ganho vem da FPR, não da
variante.** Com FPR de 1%, o filtro evita ~99% das leituras de disco inúteis e reduz a
latência das consultas de 2× a 43×, conforme o número de tabelas e a fração de
consultas a dados inexistentes. Todos os filtros dão praticamente o mesmo ganho, nos
quatro tipos de chave. A escolha da variante não muda o I/O; muda memória, CPU,
deleção e concorrência. (resultados 3.1 e 3.2)

**C2. O argumento central da proposta se confirma: o índice exato deixa de caber na
RAM e pagina; o filtro não.** A estrutura exata usa ~95× a memória de um filtro de
1% (2,3 GB contra 24 MB com 20M chaves). Enquanto cabe na RAM, ela responde um pouco
mais rápido que o filtro, porque nunca lê à toa. Quando passa da RAM, cada operação
fica até 181× mais lenta, e no I/O ela fica 27× mais lenta que o filtro. (resultados
2.1, 2.4 e 3.2)

**C3. Das três limitações clássicas, duas foram tratadas:**
- **Custo de múltiplos hashes:** rDBF e híbrido fazem uma única chamada de hash por
  operação. O híbrido insere 4,5× mais rápido que o SBF com 20M chaves, com a mesma
  memória (~10 bits/chave). O rDBF é tão rápido, mas exige ~11× a memória e, com hash
  de 32 bits, tem a FPR subindo com N (+2,8% em 20M). (resultados 2.1–2.3)
- **Deleção:** CBF e híbrido com contadores apagam sem gerar nenhum falso negativo,
  ao custo de 4× a memória. O híbrido com contadores apaga 3,7× mais rápido que o CBF.
  (resultados 2.5)
- **Tamanho fixo:** não foi tratado (ver trabalhos futuros).

**C4. Em escala, a localidade de memória pesa mais que o número de hashes.** A
complexidade O(k) se confirma em operações (2,05 hashes por busca negativa no SBF,
1,00 no híbrido, em qualquer N), mas não em tempo: quando o filtro sai do cache, cada
posição consultada vira uma falta de cache, e a inserção do SBF fica 2,2× mais lenta
de 1M para 20M (83 → 187 ns). O híbrido, que põe as k posições de uma chave num único bloco do tamanho de uma
linha de cache, cresce menos. É a justificativa do desenho do híbrido, confirmada
pelos dados. (resultados 2.2)

**C5. O modelo híbrido é viável, e o seu preço é pequeno e conhecido.** É o mais
rápido para inserir e consultar entre os filtros de ~10 bits/chave, permite deleção
na variante com contadores e escala com uma trava por bloco. O custo: FPR acima da
própria fórmula, constante (~1,06% no bloco de 512 bits e ~1,02% no de 4096, para
alvo de 1%). Responde ao "mapeamento dos limites de viabilidade" esperado na proposta
(seção 5.1): ganho real de desempenho, com uma pequena perda de precisão no bloco
menor. A causa desse desvio foi identificada: o hash duplo dentro de um bloco pequeno
(as posições de uma chave formam uma progressão aritmética em 511 células); com
posições independentes a FPR volta à fórmula, e o hash duplo "melhorado" reduz o
desvio de 6,7% para ~2,5% sem nenhuma chamada de hash a mais. (resultados 2.2, 2.3, 2.7 e 4)

**C6. A dispersão do MurmurHash2 é adequada mesmo para chaves de baixa entropia.**
CPF, CNPJ e URL dão a mesma FPR e a mesma taxa de colisões que o UUID, e ambas batem
com a teoria (colisões: 29,019% previstas, 29,023% medidas). Não houve agrupamento
causado pela chave, que era a preocupação da seção 4.3.1 da proposta. (resultados
2.3 e 2.6)

**C7. Na concorrência, a forma de sincronizar pesa mais que a estrutura.** Uma trava
global deixa a inserção mais lenta com mais threads (0,24–0,41×); uma trava por bloco
escala (até 3,2× com 6 threads). A divisão por partição do PBF, prevista na proposta,
dispensa sincronização, mas tem um teto de k/⌈k/T⌉ (3,5× com k = 7), enquanto
dividir por chave chega a 5,2×. As buscas escalam quase linearmente (até 5,5×).
E escalar mais não é ser mais rápido: com 6 threads, o híbrido segue 3,4× mais
rápido que o PBF em tempo absoluto. (resultados 4.2 e 4.3)

---

## 4. Contribuições

1. **O modelo híbrido** (filtro em blocos por chave, matriz de primos por bloco, uma
   chamada de MurmurHash64A, hash duplo, com e sem contadores) e a sua avaliação
   contra as variantes da literatura.
2. **Uma comparação controlada** das variantes: mesma linguagem, mesma interface,
   mesmo protocolo, dois regimes de dimensionamento (mesma memória e mesma FPR).
3. **Uma plataforma experimental reproduzível:** VM configurada por script,
   execuções registradas com commit, máquina, disco e hash do dataset, e campanhas
   congeladas e versionadas com dados brutos, tabelas e figuras.
4. **Achados de método**, úteis para quem mede estruturas de dados (citar como
   lições):
   - medir cada estrutura no seu processo, senão a paginação de uma é cobrada da
     seguinte;
   - sortear consultas novas a cada repetição, senão o aquecimento deixa as páginas
     na RAM e esconde a paginação;
   - a latência de leitura do SSD depende da idade dos dados (~59 µs recém-escritos
     contra ~114 µs antigos);
   - contar eventos (hashes, colisões) fora do código cronometrado, porque até um
     teste a mais na inserção mudou o tempo em 6% a 9%;
   - fixar threads um por núcleo físico, senão 6 threads caem em 3 núcleos.
5. **Uma correção a uma afirmação da literatura:** o robustBF fala em ~1,4 bits por
   elemento com FPR quase nula; com 1 bit por chave, a fórmula e as medições dão FPR de
   ~52% nesse tamanho (método, seção 3.2).

---

## 5. Limitações (resumo; detalhes no método, seção 3.12)

- Modelo em escala reduzida (1 GB de RAM): transfere-se a proporção estrutura ÷ RAM,
  não os tempos absolutos.
- Dados sintéticos e consultas uniformes (desfavorável à estrutura exata).
- Só SSD NVMe; o experimento em HD não foi feito.
- Baseline paginando com 3 repetições de uma rodada; I/O em 10M e 20M só para UUID.
- Concorrência só para PBF e híbrido, em UUID, com inserção e busca em fases
  separadas.
- Desvio da FPR do híbrido explicado, mas não corrigido no filtro avaliado.

---

## 6. Trabalhos futuros

Do que ficou de fora da proposta:
- **Tamanho fixo:** avaliar filtros que crescem (Scalable Bloom Filter) e o Cuckoo
  Filter, citados na seção 1.1 da proposta.
- **HD:** repetir os experimentos 1 e 2 num disco rotacional dedicado, onde cada
  falta de página custa ~10 ms em vez de ~80 µs e o ganho do filtro deve ser maior.

Do que os resultados levantaram:
- **FPR do híbrido:** adotar o hash duplo "melhorado" (desvio de 6,7% para ~2,5% sem
  custo de hash) ou posições independentes com uma segunda chamada de hash, e medir o
  efeito no tempo; alternativamente, uma fórmula de FPR específica para hash duplo em
  blocos pequenos.
- **rDBF com hash de 64 bits**, para tirar o desvio da FPR em N grandes.
- **Cargas mais realistas:** consultas concentradas em chaves "quentes", leitura e
  escrita simultâneas, dados reais.
- **Concorrência:** incluir o CBF; contadores atômicos em vez de trava; travas por
  faixas (menos memória que uma por bloco).
- **Integração num sistema real**, como o RocksDB, trocando o filtro dele pelo híbrido.

---

## 7. Fecho (ideia para o último parágrafo)

Aceitar um erro de 1% permitiu responder a "este dado existe?" com 95× menos memória
e evitar 99% das leituras de disco que confirmariam uma ausência. Entre as variantes,
a que mais rende é a que respeita a hierarquia de memória: o modelo híbrido, ao
concentrar cada chave numa única linha de cache, foi o mais rápido com a mesma
memória do filtro clássico, e manteve essa vantagem com várias threads. O resultado
reforça a mudança de mentalidade defendida na justificativa da proposta: em big data,
uma resposta "provavelmente sim, certamente não", barata e rápida, vale mais que uma
resposta exata que não cabe na memória.
