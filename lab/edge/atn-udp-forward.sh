#!/bin/bash
# Athanor lab edge: DNAT UDP mesh to Windows hub (DEC lab / org-edge).
# Usage: sudo HUB_IP=YOUR_HUB_LAN_IPV4 HUB_PORT=47000 bash atn-udp-forward.sh install|status|remove
set -euo pipefail
HUB_IP="${HUB_IP:-YOUR_HUB_LAN_IPV4}"
HUB_PORT="${HUB_PORT:-47000}"
CHAIN="ATN_UDP_FORWARD"

install_rules() {
  sysctl -w net.ipv4.ip_forward=1 >/dev/null
  mkdir -p /etc/sysctl.d
  echo "net.ipv4.ip_forward=1" >/etc/sysctl.d/99-atn-forward.conf

  iptables -t nat -N "$CHAIN" 2>/dev/null || iptables -t nat -F "$CHAIN"
  iptables -t nat -C PREROUTING -j "$CHAIN" 2>/dev/null || \
    iptables -t nat -I PREROUTING 1 -j "$CHAIN"
  iptables -t nat -A "$CHAIN" -p udp --dport "$HUB_PORT" \
    -j DNAT --to-destination "${HUB_IP}:${HUB_PORT}"

  # Reply path back to internet clients via this edge host.
  iptables -t nat -C POSTROUTING -p udp -d "$HUB_IP" --dport "$HUB_PORT" \
    -j MASQUERADE 2>/dev/null || \
    iptables -t nat -A POSTROUTING -p udp -d "$HUB_IP" --dport "$HUB_PORT" \
    -j MASQUERADE

  iptables -C FORWARD -p udp -d "$HUB_IP" --dport "$HUB_PORT" -j ACCEPT 2>/dev/null || \
    iptables -I FORWARD 1 -p udp -d "$HUB_IP" --dport "$HUB_PORT" -j ACCEPT
  iptables -C FORWARD -p udp -s "$HUB_IP" --sport "$HUB_PORT" -j ACCEPT 2>/dev/null || \
    iptables -I FORWARD 1 -p udp -s "$HUB_IP" --sport "$HUB_PORT" -j ACCEPT

  # Persist if iptables-persistent / netfilter-persistent exists.
  if command -v netfilter-persistent >/dev/null 2>&1; then
    netfilter-persistent save || true
  elif command -v iptables-save >/dev/null 2>&1; then
    mkdir -p /etc/iptables
    iptables-save >/etc/iptables/rules.v4 || true
  fi
  echo "ATN_UDP_FORWARD installed -> ${HUB_IP}:${HUB_PORT}"
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
  iptables -t nat -S "$CHAIN" 2>/dev/null || echo "(no $CHAIN)"
  iptables -t nat -S POSTROUTING | grep -F "$HUB_IP" || true
  iptables -S FORWARD | grep -F "$HUB_PORT" || true
}

case "${1:-status}" in
  install) install_rules ;;
  remove) remove_rules ;;
  status) status_rules ;;
  *) echo "usage: $0 install|status|remove"; exit 1 ;;
esac
