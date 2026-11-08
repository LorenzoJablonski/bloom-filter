#!/usr/bin/env bash
# Second VM, for the concurrency experiment (threadbench): the same image, RAM,
# disks and guest setup as the default VM of vm.sh, but 6 vCPUs, one per physical
# core of the i5-12500 (CPUs 0, 2, ..., 10; 1/0, 3/2, ... are their SMT siblings),
# and the QEMU threads on two sibling CPUs. Same commands as vm.sh:
#   scripts/vm/vm6.sh create | push-code | data N TYPE none | official | pull | ...
# Only one of the two VMs may run at a time: both pin vCPUs to cores 8 and 10.
export VM_NAME=${VM_NAME:-bloom-vm6}
export VM_DIR=${VM_DIR:-$HOME/.local/share/bloom-vm6}
export SSH_PORT=${SSH_PORT:-2223}
export VCPU_PINS=${VCPU_PINS:-"0 2 4 6 8 10"}
export EMU_PIN=${EMU_PIN:-"1,3"}
exec "$(dirname "${BASH_SOURCE[0]}")/vm.sh" "$@"
