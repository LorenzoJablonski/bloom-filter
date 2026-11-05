#!/usr/bin/env bash
# Runs once inside the VM, as root (scripts/vm/vm.sh create): prepares the data
# disk and the disk swap, and stops the periodic jobs that would disturb a run.
set -euo pipefail

disk_with_serial() {
    local dev
    dev=$(lsblk -dn -o PATH,SERIAL | awk -v s="$1" '$2 == s {print $1}')
    [ -n "$dev" ] || { echo "no disk with serial $1" >&2; exit 1; }
    printf '%s\n' "$dev"
}

exp=$(disk_with_serial bloomexp)
swp=$(disk_with_serial bloomswap)

# full inode/journal initialisation now: nothing lazy running behind the measurements
if ! blkid "$exp" >/dev/null 2>&1; then
    mkfs.ext4 -q -E lazy_itable_init=0,lazy_journal_init=0 -L bloomexp "$exp"
fi
mkdir -p /mnt/exp
grep -q '^LABEL=bloomexp' /etc/fstab || echo 'LABEL=bloomexp /mnt/exp ext4 noatime 0 2' >> /etc/fstab
mountpoint -q /mnt/exp || mount /mnt/exp
chown debian:debian /mnt/exp

if ! blkid "$swp" >/dev/null 2>&1; then
    mkswap -q -L bloomswap "$swp"
fi
grep -q '^LABEL=bloomswap' /etc/fstab || echo 'LABEL=bloomswap none swap sw 0 0' >> /etc/fstab
swapon --show=NAME --noheadings | grep -q . || swapon -a

for unit in apt-daily.timer apt-daily-upgrade.timer man-db.timer e2scrub_all.timer \
            fstrim.timer dpkg-db-backup.timer apt-listchanges.timer unattended-upgrades.service; do
    systemctl disable --now "$unit" 2>/dev/null || true
done
