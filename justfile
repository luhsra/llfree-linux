set shell := [ '/bin/bash', '-euo', 'pipefail', '-c' ]

[private]
@default:
    just --list --unsorted

docker_cfg := """
    OVERLAY_FS
    NAMESPACES
    NET_NS
    PID_NS
    IPC_NS
    UTS_NS
    CGROUPS
    CGROUP_CPUACCT
    CGROUP_DEVICE
    CGROUP_FREEZER
    CGROUP_SCHED
    CPUSETS
    MEMCG
    KEYS
    VETH
    BRIDGE
    NETFILTER_ADVANCED
    BRIDGE_NETFILTER
    IP_NF_FILTER
    IP_NF_MANGLE
    IP_NF_TARGET_MASQUERADE
    NETFILTER_XT_MATCH_ADDRTYPE
    NETFILTER_XT_MATCH_CONNTRACK
    IP_VS
    NETFILTER_XT_MATCH_IPVS
    NETFILTER_XT_MARK
    IP_NF_NAT
    NF_NAT
    POSIX_MQUEUE
    BPF_SYSCALL
    CGROUP_BPF
    NF_TABLES
    NF_TABLES_INET
    NF_TABLES_NETDEV
    NFT_NUMGEN
    NFT_CT
    NFT_FLOW_OFFLOAD
    NFT_CONNLIMIT
    NFT_LOG
    NFT_LIMIT
    NFT_MASQ
    NFT_REDIR
    NFT_NAT
    NFT_TUNNEL
    NFT_QUOTA
    NFT_REJECT
    NFT_COMPAT
    NFT_HASH
    NFT_XFRM
    NFT_SOCKET
    NFT_OSF
    NFT_TPROXY
    NFT_SYNPROXY
    NF_DUP_NETDEV
    NFT_DUP_NETDEV
    NFT_FWD_NETDEV
    NFT_REJECT_NETDEV
    NF_FLOW_TABLE_INET
    NF_FLOW_TABLE
    CFS_BANDWIDTH
    BLK_DEV_THROTTLING
    IP6_NF_TARGET_MASQUERADE
    IP_NF_RAW
    IP6_NF_RAW
    IP6_NF_NAT
    NFT_FIB_IPV4
    NFT_FIB_IPV6
    NFT_FIB
    IP_NF_TARGET_REDIRECT
    IP_SCTP
    IP_VS_NFCT
    IP_VS_PROTO_TCP
    IP_VS_PROTO_UDP
    IP_VS_RR
    VXLAN
    BRIDGE_VLAN_FILTERING
    SECURITY_APPARMOR
"""

config-docker dir:
    for cfg in {{ replace(docker_cfg, "\n", " ") }}; do \
        ./scripts/config --file "{{ dir }}/.config" --enable "CONFIG_$cfg"; \
    done

check-container dir:
    ./check-container.sh "{{ dir }}/.config"


run_disk := "/shared/scratch/srastaff/wrenger/hyperalloc-bench/resources/debian.qcow2"
run_disk_root := "/dev/sda3"
run_kernel := "build-llfree-vm/arch/x86/boot/bzImage"

run *ARGS:
    qemu-system-x86_64 -m 16G,slots=2,maxmem=24G \
        -smp 8 -hda {{ run_disk }} \
        -machine pc,accel=kvm,nvdimm=on \
        -no-reboot -enable-kvm \
        -serial mon:stdio -nographic \
        -kernel "{{ run_kernel }}" \
        -append 'root={{ run_disk_root }} console=ttyS0 nokaslr earlyprintk=ttyS0' \
        -nic user,hostfwd=tcp:127.0.0.1:5222-:22 \
        --cpu host,-rdtscp \
        -s \
        -object memory-backend-file,id=mb1,share=on,mem-path=./memfile.bin,size=8G \
        -device ivshmem-plain,memdev=mb1 \
        -snapshot \
        {{ ARGS }}
