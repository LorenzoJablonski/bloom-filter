# Campanhas congeladas

Cada pasta é uma cópia congelada (`analyze.py freeze`) de um banco de resultados,
com os CSVs brutos de cada execução, os logs, as tabelas e os gráficos gerados a
partir dele. As rodadas novas gravam em `results/bloom.db`, que fica fora do git;
uma campanha só entra aqui depois de terminada.

| pasta | onde | o que tem |
|---|---|---|
| `vm-nvme/` | VM Debian 13, 1 GB de RAM, 2 vCPUs fixas, swap e tabelas no NVMe do host | rótulos `oficial` (campanha oficial, 2026-11-06/07, commit c2c52ec, protocolo 4), `teste-vm-nov05` (teste, commit 59a907d) e `pilot` (protocolo 3) |
| `host-preliminar/` | desktop, sem VM | sondagem de 03/11 com 1M UUIDs (protocolo 1), só indicativa |

## Como consultar

```bash
DB=results/campaigns/vm-nvme/bloom.db
python3 scripts/analyze.py --db $DB runs --label oficial
python3 scripts/analyze.py --db $DB bench --label oficial --type uuid --sizing same_fpr none
python3 scripts/analyze.py --db $DB io --label oficial --type uuid --tables 8 --neg 0.9
```

Sem `--label`, a análise usa todas as execuções limpas do protocolo mais novo; o
rótulo separa a campanha oficial do teste. Os gráficos em `vm-nvme/plots/` e as
tabelas em `vm-nvme/tables/` (CSV com média, desvio e IC 95%; LaTeX booktabs) são
da campanha `oficial`. Para refazê-los:

```bash
make venv
.venv/bin/python scripts/analyze.py --db $DB bench --label oficial --plot DIR --format pdf png
.venv/bin/python scripts/analyze.py --db $DB io --label oficial --plot DIR --format pdf png
```

## Logs de `vm-nvme/logs/`

- `oficial-campaign.log`, `oficial-host.log`: saída da campanha e estado do host
  (perfil de energia, kernel, swap da RAM da VM no host) no início e no fim.
- `teste-vm-nov05-*.log`: o mesmo para o teste de 05/11.
- `latency-rewrite.log`, `latency-age-*.log`: medições da latência de leitura
  em função da idade das tabelas no SSD (ver "Campanha oficial" em docs/PROGRESS.md).

## Ressalvas

- Baseline de UUID em 10M e 20M: 3 repetições × 1 rodada (ele pagina e leva horas);
  nos gráficos aparece como "baseline (3 amostras)". O resto: 5 × 3; CPF, CNPJ e URL
  em 10M e 20M, só filtros e só benchmark, 2 × 1.
- A latência absoluta de leitura depende da idade das tabelas no SSD: compare
  estruturas no mesmo tipo e N, não N ou tipos entre si. Contagens de leitura e FPR
  não são afetadas.
