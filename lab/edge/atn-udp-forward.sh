#!/bin/bash
# Athanor edge: DNAT+MASQ UDP :HUB_PORT -> Windows hub on LAN.
# Org must pass HUB_IP (no baked-in lab addresses).
# Example: sudo HUB_IP=192.168.1.10 HUB_PORT=47000 bash atn-udp-forward.sh install
set -euo pipefail
HUB_IP="${HUB_IP:-}"
HUB_PORT="${HUB_PORT:-47000}"
CHAIN="ATN_UDP_FORWARD"

if [[ -z "$HUB_IP" ]]; then
  echo "HUB_IP is required (Windows hub LAN IPv4). Set by org edge panel / operator." >&2
  exit 1
fi
if [[ ! "$HUB_IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "HUB_IP must be dotted IPv4" >&2
  exit 1
fi

harden_sysctl() {
  sysctl -w net.ipv4.ip_forward=1 >/dev/null
  for k in all default eth0; do
    sysctl -w "net.ipv4.conf.${k}.rp_filter=0" >/dev/null 2>&1 || true
  done
  sysctl -w net.netfilter.nf_conntrack_udp_timeout=180 >/dev/null 2>&1 || true
  sysctl -w net.netfilter.nf_conntrack_udp_timeout_stream=300 >/dev/null 2>&1 || true
  mkdir -p /etc/sysctl.d
  cat >/etc/sysctl.d/99-atn-forward.conf <<EOF
net.ipv4.ip_forward=1
net.ipv4.conf.all.rp_filter=0
net.ipv4.conf.default.rp_filter=0
net.ipv4.conf.eth0.rp_filter=0
net.netfilter.nf_conntrack_udp_timeout=180
net.netfilter.nf_conntrack_udp_timeout_stream=300
EOF
}

install_rules() {
  harden_sysctl
  iptables -t nat -N "$CHAIN" 2>/dev/null || true
  iptables -t nat -F "$CHAIN"
  iptables -t nat -C PREROUTING -j "$CHAIN" 2>/dev/null || \
    iptables -t nat -I PREROUTING 1 -j "$CHAIN"
  iptables -t nat -A "$CHAIN" -p udp --dport "$HUB_PORT" \
    -j DNAT --to-destination "${HUB_IP}:${HUB_PORT}"
  iptables -t nat -C POSTROUTING -p udp -d "$HUB_IP" --dport "$HUB_PORT" -j MASQUERADE 2>/dev/null || \
    iptables -t nat -A POSTROUTING -p udp -d "$HUB_IP" --dport "$HUB_PORT" -j MASQUERADE
  iptables -C FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || \
    iptables -I FORWARD 1 -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
  iptables -C FORWARD -p udp -d "$HUB_IP" --dport "$HUB_PORT" -j ACCEPT 2>/dev/null || \
    iptables -I FORWARD 1 -p udp -d "$HUB_IP" --dport "$HUB_PORT" -j ACCEPT
  iptables -C FORWARD -p udp -s "$HUB_IP" --sport "$HUB_PORT" -j ACCEPT 2>/dev/null || \
    iptables -I FORWARD 1 -p udp -s "$HUB_IP" --sport "$HUB_PORT" -j ACCEPT
  if command -v netfilter-persistent >/dev/null 2>&1; then
    netfilter-persistent save || true
  elif command -v iptables-save >/dev/null 2>&1; then
    mkdir -p /etc/iptables
    iptables-save >/etc/iptables/rules.v4 || true
  fi
  echo "ATN edge OK: *:udp/${HUB_PORT} DNAT+MASQ -> ${HUB_IP}:${HUB_PORT}"
}

remove_rules() {
  iptables -t nat -D PREROUTING -j "$CHAIN" 2>/dev/null || true
  iptables -t nat -F "$CHAIN" 2>/dev/null || true
  iptables -t nat -X "$CHAIN" 2>/dev/null || true
  while iptables -t nat -D POSTROUTING -p udp -d "$HUB_IP" --dport "$HUB_PORT" -j MASQUERADE 2>/dev/null; do :; done
  iptables -D FORWARD -p udp -d "$HUB_IP" --dport "$HUB_PORT" -j ACCEPT 2>/dev/null || true
  iptables -D FORWARD -p udp -s "$HUB_IP" --sport "$HUB_PORT" -j ACCEPT 2>/dev/null || true
  echo removed
}

status_rules() {
  echo "HUB_IP=${HUB_IP} HUB_PORT=${HUB_PORT}"
  echo "ip_forward=$(sysctl -n net.ipv4.ip_forward) rp_filter_eth0=$(sysctl -n net.ipv4.conf.eth0.rp_filter 2>/dev/null || echo n/a)"
  iptables -t nat -S | grep -E "ATN|${HUB_PORT}|${HUB_IP}" || true
  iptables -S FORWARD | grep -E "${HUB_PORT}|${HUB_IP}|ESTABLISHED" || true
  iptables -t nat -L "$CHAIN" -n -v 2>/dev/null || true
}

case "${1:-status}" in
  install) install_rules ;;
  remove) remove_rules ;;
  status) status_rules ;;
  *) echo "usage: HUB_IP=x.x.x.x $0 install|status|remove"; exit 1 ;;
esac
