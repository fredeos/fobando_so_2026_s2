#!/usr/bin/env bash
# Ubuntu 24.04 / OpenMPI 4.1 lab. No changes until the explicit create/deploy action.
# Sources: https://ubuntu.com/server/docs/how-to/virtualisation/libvirt/
# https://cloud-images.ubuntu.com/noble/current/
# https://www.open-mpi.org/faq/?category=rsh
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
usage() {
    cat <<'HELP'
Uso:
  bash scripts/vm-cluster.sh check
  sudo bash scripts/vm-cluster.sh create USUARIO CLAVE_PUBLICA.pub
  sudo virsh -c qemu:///system domifaddr dips-worker-1 --source lease
  sudo virsh -c qemu:///system domifaddr dips-worker-2 --source lease
  bash scripts/vm-cluster.sh deploy IP_VM1 IP_VM2

Preparacion del host Ubuntu (una sola vez):
  sudo apt update
  sudo apt install qemu-system-x86 qemu-utils libvirt-daemon-system libvirt-clients virtinst curl python3
  sudo systemctl enable --now libvirtd
  # Si /dev/kvm no existe, revisar VT-x/AMD-V en BIOS y el modulo del host:
  sudo modprobe kvm_intel   # Intel; en AMD usar kvm_amd
  # Crear clave SSH solo si no tienes una:
  ssh-keygen -t ed25519
  # En "Enter file in which to save the key", presiona Enter para aceptar
  # ~/.ssh/id_ed25519. Esa pregunta pide una ruta, no tu nombre de usuario.

create descarga Ubuntu Cloud amd64, verifica SHA256 y crea dos VMs KVM con
2 vCPU, 2 GiB RAM y disco qcow2 de 16 GiB cada una. Instala las dependencias
con cloud-init y habilita acceso SSH por tu clave publica; no copia claves
privadas ni guarda DIPS_AES_KEY. No sobrescribe dominios/discos existentes.

deploy requiere SSH no interactivo: probar primero ssh USUARIO@IP en ambas VMs
(verificar la huella). Si tu clave tiene passphrase, cargarla con ssh-add.
USUARIO debe ser el mismo que ejecuta make en el host. Espera cloud-init,
comprueba OpenMPI, copia output/server y genera output/cluster.env.

Ejecucion posterior (desde proyecto_2):
  source output/cluster.env
  make cluster-demo IMAGE=images/fgm-scaling-heat.png RESIZE=128x160 FILTER=sobel

Apagar sin borrar discos:
  sudo virsh -c qemu:///system shutdown dips-worker-1
  sudo virsh -c qemu:///system shutdown dips-worker-2
HELP
}
need() { command -v "$1" >/dev/null || { printf 'Falta %s. Consulta --help.\n' "$1" >&2; exit 1; }; }
check() {
    local missing=0
    for command in virsh virt-install qemu-img curl python3 ssh; do
        if command -v "$command" >/dev/null; then printf '%s: disponible\n' "$command"
        else printf '%s: falta\n' "$command"; missing=1; fi
    done
    if [[ -c /dev/kvm ]]; then printf '/dev/kvm: disponible\n'
    else printf '/dev/kvm: no disponible; no se pueden iniciar VMs KVM todavia.\n'; missing=1; fi
    return "$missing"
}
create() {
    [[ $# == 2 ]] || { usage; exit 1; }
    [[ $EUID == 0 ]] || { printf 'create requiere sudo.\n' >&2; exit 1; }
    local user=$1 public=$2
    [[ "$user" =~ ^[a-z_][a-z0-9_-]*$ ]] || { printf 'Nombre de usuario invalido: %s\n' "$user" >&2; exit 1; }
    if [[ ! -f "$public" || ! -r "$public" ]]; then
        printf 'La clave publica SSH no existe o no es legible: %s\n' "$public" >&2
        printf 'Indica el archivo .pub creado por ssh-keygen.\n' >&2
        printf 'Para la ruta predeterminada, acepta con Enter la pregunta sobre el archivo.\n' >&2
        exit 1
    fi
    [[ -c /dev/kvm ]] || { printf 'Falta /dev/kvm. Revisa --help antes de crear VMs.\n' >&2; exit 1; }
    for command in virsh virt-install qemu-img curl python3 sha256sum; do need "$command"; done
    local storage=/var/lib/libvirt/images/dips image=noble-server-cloudimg-amd64.img
    local url=https://cloud-images.ubuntu.com/noble/current
    vm_work=$(mktemp -d)
    trap 'rm -rf -- "$vm_work"' EXIT
    mkdir -p -- "$storage"
    if [[ ! -f "$storage/$image" ]]; then
        curl --fail --location --retry 3 "$url/$image" -o "$vm_work/$image"
        curl --fail --location "$url/SHA256SUMS" -o "$vm_work/SHA256SUMS"
        (cd "$vm_work"; awk -v image="$image" '{sub(/^\*/, "", $2); if ($2 == image) print $1 "  " $2}' SHA256SUMS | sha256sum --check --status)
        install -m 0644 "$vm_work/$image" "$storage/$image"
    fi
    # Domain/network operations are explicit and never remove existing VMs.
    if ! virsh -c qemu:///system net-list --name | grep -qx default; then
        virsh -c qemu:///system net-start default
    fi
    virsh -c qemu:///system net-autostart default
    for number in 1 2; do
        local name=dips-worker-$number disk=$storage/dips-worker-$number.qcow2
        if virsh -c qemu:///system dominfo "$name" >/dev/null 2>&1 || [[ -e "$disk" ]]; then
            printf '%s ya existe; no se modifica.\n' "$name"
            continue
        fi
        python3 - "$name" "$user" "$public" "$vm_work/user-data" <<'PY'
import json, pathlib, sys
name, user, public, output = sys.argv[1:]
key = pathlib.Path(public).read_text().strip()
if '\n' in key or not key.startswith(('ssh-ed25519 ', 'ssh-rsa ', 'ecdsa-sha2-')):
    raise SystemExit('Clave publica SSH invalida')
config = {
    'hostname': name, 'manage_etc_hosts': True, 'ssh_pwauth': False,
    'users': [{'name': user, 'shell': '/bin/bash', 'lock_passwd': True,
               'sudo': 'ALL=(ALL) NOPASSWD:ALL', 'ssh_authorized_keys': [key]}],
    'package_update': True,
    'packages': ['openmpi-bin', 'libopenmpi-dev', 'libssl-dev', 'libpng-dev',
                 'libjpeg-dev', 'openssh-server'],
    'runcmd': [['systemctl', 'enable', '--now', 'ssh']],
}
pathlib.Path(output).write_text('#cloud-config\n' + json.dumps(config))
PY
        qemu-img create -f qcow2 -F qcow2 -b "$storage/$image" "$disk" 16G
        virt-install --connect qemu:///system --name "$name" --virt-type kvm \
            --memory 2048 --vcpus 2 --cpu host --import --os-variant ubuntu24.04 \
            --disk "path=$disk,format=qcow2,bus=virtio" \
            --network network=default,model=virtio --graphics none --noautoconsole \
            --cloud-init "user-data=$vm_work/user-data"
    done
    printf 'VMs iniciadas. Espera cloud-init; obtiene IPs con virsh domifaddr.\n'
}
deploy() {
    [[ $# == 2 && $EUID != 0 ]] || { usage; exit 1; }
    for command in ssh python3 ip mpirun; do need "$command"; done
    local first=$1 second=$2 user
    user=$(id -un)
    for host in "$first" "$second"; do
        [[ "$host" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { printf 'Indica dos IPv4 de las VMs.\n' >&2; exit 1; }
    done
    [[ "$first" != "$second" ]] || { printf 'Las VMs deben tener IP distintas.\n' >&2; exit 1; }
    make -C "$project_dir" cluster-build
    local version remote_version command cidr subnet local_ip libraries
    version=$(mpirun --version | head -1)
    for host in "$first" "$second"; do
        ssh -o BatchMode=yes -o ConnectTimeout=10 "$user@$host" cloud-init status --wait
        remote_version=$(ssh -o BatchMode=yes "$user@$host" 'mpirun --version | head -1')
        [[ "$version" == "$remote_version" ]] || { printf 'Version OpenMPI distinta en %s\n' "$host" >&2; exit 1; }
        printf -v command 'mkdir -p -- %q && cat > %q && chmod 700 -- %q' \
            "$project_dir/output" "$project_dir/output/server" "$project_dir/output/server"
        ssh -o BatchMode=yes "$user@$host" "$command" < "$project_dir/output/server"
        printf -v command 'ldd %q' "$project_dir/output/server"
        libraries=$(ssh -o BatchMode=yes "$user@$host" "$command")
        if grep -q 'not found' <<< "$libraries"; then
            printf 'Faltan bibliotecas en %s.\n' "$host" >&2; exit 1
        fi
    done
    cidr=$(ip -4 -o addr show virbr0 | awk '{print $4; exit}')
    [[ -n "$cidr" ]] || { printf 'No se encontro la red virbr0.\n' >&2; exit 1; }
    subnet=$(python3 -c 'import ipaddress,sys; print(ipaddress.ip_interface(sys.argv[1]).network)' "$cidr")
    local_ip=${cidr%/*}
    python3 - "$cidr" "$first" "$second" <<'PY'
import ipaddress, sys
network = ipaddress.ip_interface(sys.argv[1]).network
if any(ipaddress.ip_address(ip) not in network for ip in sys.argv[2:]):
    raise SystemExit('Las dos VMs deben estar en la red libvirt default/virbr0')
PY
    # Flat SSH launch: the controller starts both VMs; no private keys in guests.
    # Restrict MPI data to the shared subnet. Leave OOB control autodetection:
    # OpenMPI 4.1 can resolve an OOB CIDR to the controller's interface name and
    # propagate it to guests (virbr0 on host != enp1s0 inside the VMs).
    {
        printf 'export CLUSTER_HOSTS=%q\n' "$local_ip:2,$first:1,$second:1"
        printf 'export OMPI_MCA_plm_rsh_no_tree_spawn=1\n'
        printf 'export OMPI_MCA_btl_tcp_if_include=%q\n' "$subnet"
        printf 'unset OMPI_MCA_oob_tcp_if_include OMPI_MCA_oob_tcp_if_exclude\n'
    } > "$project_dir/output/cluster.env"
    printf 'Preparado: host (coordinador + 1 worker), VM1 y VM2 (1 worker cada una).\n'
    printf 'Ejecuta: source output/cluster.env; make cluster-demo\n'
}
case ${1:---help} in
    check) check ;;
    create) shift; create "$@" ;;
    deploy) shift; deploy "$@" ;;
    --help|-h) usage ;;
    *) usage; exit 1 ;;
esac
