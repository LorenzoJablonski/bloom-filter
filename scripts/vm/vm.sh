#!/usr/bin/env bash
# Controlled environment for the official runs: a small Debian 13 VM with pinned
# vCPUs, swap on disk and no host page cache between the guest and the disk.
# Datasets and tables are built on the host (they need more RAM than the VM has
# and are not measured) and copied in. Runs on qemu:///session, no root needed.
#
#   scripts/vm/vm.sh create                  download the image, create and boot the VM
#   scripts/vm/vm.sh push-code               send the committed HEAD to the guest and build it
#   scripts/vm/vm.sh data N [TYPE] [T...]    generate on the host (if missing) and copy to the guest (T = none: no tables)
#   scripts/vm/vm.sh campaign [VAR=value...] start scripts/vm/campaign.sh detached in the guest
#   scripts/vm/vm.sh official [VAR=value...] start the official campaign (scripts/vm/official.sh) detached in the guest;
#                                            PLAN=extra.sh runs scripts/vm/extra.sh (collisions, delete) instead
#   scripts/vm/vm.sh pull                    bring results/ back to data/vm/results/
#   scripts/vm/vm.sh ssh [command]           shell in the guest
#   scripts/vm/vm.sh start|stop|status|destroy
#
# Settings (environment): VM_DIR VM_NAME VM_RAM_MB SSH_PORT VCPU_PINS EMU_PIN
#   EXP_GB SWAP_GB DISK_DIR DISK_MODEL DISK_ROTATION HOST_MEM_CAP SEED VALUE_SIZE
# To test on another disk (an empty one, see docs/PROGRESS.md): destroy, then create with DISK_DIR on it,
# DISK_MODEL=<its name> and DISK_ROTATION=<rpm>; both are recorded with the results.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
VM_NAME=${VM_NAME:-bloom-vm}
VM_DIR=${VM_DIR:-$HOME/.local/share/bloom-vm}
DISK_DIR=${DISK_DIR:-$VM_DIR}
VM_RAM_MB=${VM_RAM_MB:-1024}
SSH_PORT=${SSH_PORT:-2222}
VCPU_PINS=${VCPU_PINS:-"8 10"} # distinct physical cores (i5-12500: 8/9 and 10/11 are siblings)
EMU_PIN=${EMU_PIN:-6-7}
EXP_GB=${EXP_GB:-60}
SWAP_GB=${SWAP_GB:-8}
DISK_MODEL=${DISK_MODEL:-HOST-NVME}
DISK_ROTATION=${DISK_ROTATION:-1} # 1 = non-rotational
HOST_MEM_CAP=${HOST_MEM_CAP:-4G}
SEED=${SEED:-42}
VALUE_SIZE=${VALUE_SIZE:-100}

IMG_BASE=https://cloud.debian.org/images/cloud/trixie/latest
IMG_NAME=debian-13-genericcloud-amd64.qcow2
VIRSH=(virsh -c qemu:///session)
SSH_OPTS=(-i "$VM_DIR/id_ed25519" -p "$SSH_PORT" -o StrictHostKeyChecking=no
          -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5)
GUEST=debian@127.0.0.1

log() { printf 'vm: %s\n' "$*" >&2; }
die() { printf 'vm: %s\n' "$*" >&2; exit 1; }
g() { ssh "${SSH_OPTS[@]}" "$GUEST" "$@"; }
to_guest() { rsync -a --inplace --info=progress2 -e "ssh ${SSH_OPTS[*]}" "$@"; }

download_image() {
    mkdir -p "$VM_DIR"
    local img="$VM_DIR/$IMG_NAME"
    if [ ! -f "$img" ]; then
        log "downloading $IMG_NAME"
        curl -fsSL --retry 3 -o "$img.part" "$IMG_BASE/$IMG_NAME"
        mv "$img.part" "$img"
    fi
    log "verifying the image against SHA512SUMS"
    local want
    want=$(curl -fsSL "$IMG_BASE/SHA512SUMS" | awk -v f="$IMG_NAME" '$2 == f {print $1}')
    [ -n "$want" ] || die "$IMG_NAME is not listed in SHA512SUMS"
    if ! printf '%s  %s\n' "$want" "$img" | sha512sum -c --quiet -; then
        # "latest" moves: the local copy may just be an older release
        die "checksum mismatch for $img; delete it and run create again"
    fi
}

make_seed() {
    [ -f "$VM_DIR/id_ed25519" ] || ssh-keygen -q -t ed25519 -N '' -C "$VM_NAME" -f "$VM_DIR/id_ed25519"
    local seed="$VM_DIR/seed"
    mkdir -p "$seed"
    printf 'instance-id: %s\nlocal-hostname: %s\n' "$VM_NAME" "$VM_NAME" > "$seed/meta-data"
    cat > "$seed/user-data" <<EOF
#cloud-config
users:
  - name: debian
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    ssh_authorized_keys:
      - $(cat "$VM_DIR/id_ed25519.pub")
package_update: true
packages: [gcc, make, git, python3, rsync]
EOF
    genisoimage -quiet -output "$VM_DIR/seed.iso" -volid cidata -joliet -rock \
        "$seed/user-data" "$seed/meta-data"
}

make_disks() {
    mkdir -p "$DISK_DIR"
    qemu-img create -q -f qcow2 -F qcow2 -b "$VM_DIR/$IMG_NAME" "$VM_DIR/root.qcow2" 16G
    # data disk: sparse on the host, written once by the copy; swap: allocated now,
    # so paging never pays for block allocation in the host filesystem
    qemu-img create -q -f raw "$DISK_DIR/exp.raw" "${EXP_GB}G"
    log "allocating ${SWAP_GB} GB of swap disk"
    qemu-img create -q -f raw -o preallocation=full "$DISK_DIR/swap.raw" "${SWAP_GB}G"
}

domain_xml() {
    local pins="" i=0 p
    for p in $VCPU_PINS; do
        pins+="    <vcpupin vcpu='$i' cpuset='$p'/>"$'\n'
        i=$((i + 1))
    done
    cat <<EOF
<domain type='kvm' xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>
  <name>$VM_NAME</name>
  <memory unit='MiB'>$VM_RAM_MB</memory>
  <vcpu placement='static'>$i</vcpu>
  <cputune>
$pins    <emulatorpin cpuset='$EMU_PIN'/>
  </cputune>
  <os><type arch='x86_64' machine='q35'>hvm</type><boot dev='hd'/></os>
  <features><acpi/><apic/></features>
  <cpu mode='host-passthrough'/>
  <clock offset='utc'/>
  <on_crash>destroy</on_crash>
  <devices>
    <disk type='file' device='disk'>
      <driver name='qemu' type='qcow2' cache='none' io='native'/>
      <source file='$VM_DIR/root.qcow2'/>
      <target dev='vda' bus='virtio'/>
    </disk>
    <controller type='scsi' model='virtio-scsi'/>
    <disk type='file' device='disk'>
      <driver name='qemu' type='raw' cache='none' io='native'/>
      <source file='$DISK_DIR/exp.raw'/>
      <target dev='sda' bus='scsi' rotation_rate='$DISK_ROTATION'/>
      <serial>bloomexp</serial>
      <vendor>BLOOM</vendor>
      <product>$DISK_MODEL</product>
    </disk>
    <disk type='file' device='disk'>
      <driver name='qemu' type='raw' cache='none' io='native'/>
      <source file='$DISK_DIR/swap.raw'/>
      <target dev='sdb' bus='scsi' rotation_rate='$DISK_ROTATION'/>
      <serial>bloomswap</serial>
      <vendor>BLOOM</vendor>
      <product>$DISK_MODEL</product>
    </disk>
    <disk type='file' device='cdrom'>
      <driver name='qemu' type='raw'/>
      <source file='$VM_DIR/seed.iso'/>
      <target dev='sdz' bus='sata'/>
      <readonly/>
    </disk>
    <serial type='file'><source path='$VM_DIR/console.log'/><target port='0'/></serial>
    <video><model type='vga'/></video> <!-- without it this image resets before the kernel starts -->
    <memballoon model='none'/>
    <rng model='virtio'><backend model='random'>/dev/urandom</backend></rng>
  </devices>
  <qemu:commandline>
    <qemu:arg value='-netdev'/>
    <qemu:arg value='user,id=net0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22'/>
    <qemu:arg value='-device'/>
    <qemu:arg value='virtio-net-pci,netdev=net0,bus=pcie.0,addr=0x10'/>
  </qemu:commandline>
</domain>
EOF
}

wait_ssh() {
    log "waiting for ssh"
    local i
    for i in $(seq 1 90); do
        g true 2>/dev/null && return 0
        sleep 2
    done
    die "no ssh after 3 minutes; see $VM_DIR/console.log"
}

cmd_create() {
    "${VIRSH[@]}" dominfo "$VM_NAME" >/dev/null 2>&1 && die "$VM_NAME already exists (destroy first)"
    download_image
    make_seed
    make_disks
    domain_xml > "$VM_DIR/domain.xml"
    "${VIRSH[@]}" define "$VM_DIR/domain.xml" >/dev/null
    "${VIRSH[@]}" start "$VM_NAME" >/dev/null
    wait_ssh
    log "waiting for cloud-init (installs the compiler)"
    g cloud-init status --wait >/dev/null || die "cloud-init failed; see $VM_DIR/console.log"
    g sudo bash -s < "$ROOT/scripts/vm/guest-setup.sh"
    log "ready; next: push-code, data, campaign"
    cmd_status
}

cmd_push_code() {
    local branch sha bundle
    if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no)" ] && [ -z "${ALLOW_DIRTY:-}" ]; then
        die "tracked files have uncommitted changes; only commits reach the guest (ALLOW_DIRTY=1 to send HEAD anyway)"
    fi
    branch=$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)
    sha=$(git -C "$ROOT" rev-parse HEAD)
    bundle=$(mktemp)
    git -C "$ROOT" bundle create -q "$bundle" HEAD "$branch"
    scp -q -i "$VM_DIR/id_ed25519" -P "$SSH_PORT" -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR "$bundle" "$GUEST:bloom.bundle"
    rm -f "$bundle"
    g "set -e
       [ -d bloom ] || git clone -q bloom.bundle bloom
       cd bloom
       git fetch -q ../bloom.bundle '$branch'
       git checkout -q -B '$branch' '$sha'
       make -s all && make -s check >/dev/null && make -s test | tail -1"
    log "guest at $branch ${sha:0:8}"
}

cmd_data() {
    local n=${1:?usage: data N [TYPE] [T...]} type=${2:-uuid}
    shift; [ $# -gt 0 ] && shift
    local tables=("$@")
    [ ${#tables[@]} -gt 0 ] || tables=(1 8)
    [ "${tables[0]}" != none ] || tables=()
    local dir="data/vm/n$n/data"
    local cap=(systemd-run --user --scope -q -p "MemoryMax=$HOST_MEM_CAP")
    make -s -C "$ROOT" all
    cd "$ROOT"
    if [ ! -f "$dir/$type/inexist.txt" ]; then
        log "generating $type, N=$n (memory capped at $HOST_MEM_CAP)"
        "${cap[@]}" python3 scripts/gen_datasets.py --type "$type" --count "$n" --seed "$SEED" --outdir "$dir"
        "${cap[@]}" python3 scripts/partition.py --type "$type" --seed "$SEED" --outdir "$dir"
    fi
    local t
    for t in "${tables[@]}"; do # reuses existing tables (fingerprint in the header)
        (cd "data/vm/n$n" && "${cap[@]}" "$ROOT/bloom_app" --iobench --build-store --type "$type" \
            --tables "$t" --value-size "$VALUE_SIZE" --store-dir data/store)
    done
    log "copying to the guest"
    g "mkdir -p /mnt/exp/n$n/$type /mnt/exp/store"
    to_guest --exclude pool.txt "$dir/$type/" "$GUEST:/mnt/exp/n$n/$type/"
    for t in "${tables[@]}"; do
        to_guest "$dir/store/${type}_n${n}_T${t}_v${VALUE_SIZE}" "$GUEST:/mnt/exp/store/"
    done
    g "df -h /mnt/exp | tail -1"
}

cmd_campaign() {
    # the script travels on its own so it can be started without another push-code,
    # but the code it measures is the guest's clone, whose commit run.py records
    scp -q -i "$VM_DIR/id_ed25519" -P "$SSH_PORT" -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
        "$ROOT/scripts/vm/campaign.sh" "$GUEST:campaign.sh"
    g "nohup env $* bash campaign.sh > campaign.log 2>&1 < /dev/null & echo started, pid \$!"
    log "follow with: scripts/vm/vm.sh ssh tail -f campaign.log"
}

cmd_official() {
    scp -q -i "$VM_DIR/id_ed25519" -P "$SSH_PORT" -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
        "$ROOT/scripts/vm/campaign.sh" "$ROOT/scripts/vm/${PLAN:-official.sh}" "$GUEST:"
    g "nohup env $* bash ${PLAN:-official.sh} > campaign.log 2>&1 < /dev/null & echo started, pid \$!"
    log "follow with: scripts/vm/vm.sh ssh tail -f campaign.log"
}

cmd_pull() {
    mkdir -p "$ROOT/data/vm/results"
    rsync -a -e "ssh ${SSH_OPTS[*]}" "$GUEST:bloom/results/" "$ROOT/data/vm/results/"
    rsync -a -e "ssh ${SSH_OPTS[*]}" "$GUEST:campaign.log" "$ROOT/data/vm/results/" 2>/dev/null || true
    log "database: data/vm/results/bloom.db (analyze.py --db)"
}

cmd_status() {
    "${VIRSH[@]}" domstate "$VM_NAME"
    "${VIRSH[@]}" vcpupin "$VM_NAME" | sed -n '3,$p' | sed '/^$/d'
    g 'free -m | sed -n 1,3p; cat /proc/swaps; lsblk -o NAME,SIZE,ROTA,MODEL,MOUNTPOINTS; df -h /mnt/exp | tail -1; uname -r'
}

cmd_destroy() {
    read -r -p "delete $VM_NAME and its disks (keeps the base image)? [y/N] " ans
    [ "$ans" = y ] || exit 0
    "${VIRSH[@]}" destroy "$VM_NAME" 2>/dev/null || true
    "${VIRSH[@]}" undefine "$VM_NAME" 2>/dev/null || true
    rm -f "$VM_DIR/root.qcow2" "$DISK_DIR/exp.raw" "$DISK_DIR/swap.raw" "$VM_DIR/seed.iso" "$VM_DIR/console.log"
}

case ${1:-} in
    create)    cmd_create ;;
    push-code) cmd_push_code ;;
    data)      shift; cmd_data "$@" ;;
    campaign)  shift; cmd_campaign "$@" ;;
    official)  shift; cmd_official "$@" ;;
    pull)      cmd_pull ;;
    ssh)       shift; g "$@" ;;
    start)     "${VIRSH[@]}" start "$VM_NAME"; wait_ssh ;;
    stop)      "${VIRSH[@]}" shutdown "$VM_NAME" ;;
    status)    cmd_status ;;
    destroy)   cmd_destroy ;;
    *)         sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
