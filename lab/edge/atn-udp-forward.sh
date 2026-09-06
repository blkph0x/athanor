#!/bin/bash
# Athanor lab edge: DNAT UDP mesh to Windows hub (DEC lab / org-edge).
# Usage: sudo HUB_IP=YOUR_HUB_LAN_IPV4 HUB_PORT=47000 bash atn-udp-forward.sh install|status|remove
#
# Do NOT use default 30s UDP conntrack with MASQUERADE — that matched lab
# BOOM and dropped 5G echoes while ESTABLISHED/ping rc=0 still looked fine.
# Keep MASQUERADE (needed on this Technicolor edge) + udp_timeout>=180.
set -euo pipefail
HUB_IP="${HUB_IP:-YOUR_HUB_LAN_IPV4}"
HUB_PORT="${HUB_PORT:-47000}"
CHAIN="ATN_UDP_FORWARD"

install_rules() {
  sysctl -w net.ipv4.ip_forward=1 >/dev/null
  # Keep UDP mappings long enough for cellular CGNAT / probe cadence.
  sysctl -w net.netfilter.nf_conntrack_udp_timeout=180 >/dev/null 2>&1 || true
  sysctl -w net.netfilter.nf_conntrack_udp_timeout_stream=300 >/dev/null 2>&1 || true
  mkdir -p /etc/sysctl.d
  cat >/etc/sysctl.d/99-atn-forward.conf <<EOF
net.ipv4.ip_forward=1
net.netfilter.nf_conntrack_udp_timeout=180
net.netfilter.nf_conntrack_udp_timeout_stream=300
EOF

  iptables -t nat -N "$CHAIN" 2>/dev/null || iptables -t nat -F "$CHAIN"
  iptables -t nat -C PREROUTING -j "$CHAIN" 2>/dev/null || \
    iptables -t nat -I PREROUTING 1 -j "$CHAIN"
  iptables -t nat -A "$CHAIN" -p udp --dport "$HUB_PORT" \
    -j DNAT --to-destination "${HUB_IP}:${HUB_PORT}"

  # MASQUERADE so hub replies via this edge (required on this Technicolor
  # path). Pair with raised nf_conntrack_udp_timeout — default 30s matched
  # lab BOOM and killed 5G return path while tunSend still returned 0.
  iptables -t nat -C POSTROUTING -p udp -d "$HUB_IP" --dport "$HUB_PORT" \
    -j MASQUERADE 2>/dev/null || \
    iptables -t nat -A POSTROUTING -p udp -d "$HUB_IP" --dport "$HUB_PORT" \
    -j MASQUERADE

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
  echo "ATN_UDP_FORWARD installed -> ${HUB_IP}:${HUB_PORT} (DNAT+MASQ, udp_timeout=180)"
}

remove_rules() {
  iptables -t nat -D PREROUTING -j "$CHAIN" 2>/dev/null || true
  iptables -t nat -F "$CHAIN" 2>/dev/null || true
  iptables -t nat -X "$CHAIN" 2>/dev/null || true
  iptables -t nat -D POSTROUTING -p udp -d "$HUB_IP" --dport "$HUB_PORT" \
    -j MASQUERADE 2>/dev/null || true
  iptables -D FORWARD -p udp -d "$HUB_IP" --dport "$HUB_PORT" -j ACCEPT 2>/dev/null || true
  iptables -D FORWARD -p udp -s "$HUB_IP" --sport "$HUB_PORT" -j ACCEPT 2>/dev/null || true
  echo "ATN_UDP_FORWARD removed"
}

status_rules() {
  echo "ip_forward=$(sysctl -n net.ipv4.ip_forward)"
  sysctl net.netfilter.nf_conntrack_udp_timeout \
    net.netfilter.nf_conntrack_udp_timeout_stream 2>/dev/null || true
  iptables -t nat -S "$CHAIN" 2>/dev/null || echo "(no $CHAIN)"
  iptables -t nat -S POSTROUTING | grep -F "$HUB_IP" || echo "(no POSTROUTING MASQ)"
  iptables -S FORWARD | grep -F "$HUB_PORT" || true
}

case "${1:-status}" in
  install) install_rules ;;
  remove) remove_rules ;;
  status) status_rules ;;
  *) echo "usage: $0 install|status|remove"; exit 1 ;;
esac
