# Lab edge (public internet path)

Phone mesh over **cellular** must not use the LAN IP (`YOUR_HUB_LAN_IPV4`). Use the
edge VM that terminates `mesh.example.org`:

```
Phone (5G) → UDP mesh.example.org:47000
          → edge VM YOUR_EDGE_LAN_IPV4 (public YOUR_PUBLIC_IPV4)
          → DNAT/MASQUERADE → Windows hub YOUR_HUB_LAN_IPV4:47000 (atnnode listen)
```

## One-time on edge VM (YOUR_EDGE_LAN_IPV4)

```bash
sudo HUB_IP=YOUR_HUB_LAN_IPV4 HUB_PORT=47000 bash lab/edge/atn-udp-forward.sh install
# panel: https://mesh.example.org/atn/
```

Router/firewall must forward **UDP 47000** to `YOUR_EDGE_LAN_IPV4` (same as 80/443).

## Operator panel

Open [https://mesh.example.org/atn/](https://mesh.example.org/atn/) — set
domain, **public_ipv4** (`YOUR_PUBLIC_IPV4`; LAN DNS may resolve the domain to
`YOUR_EDGE_LAN_IPV4`), and paste `peer_ek` from `atnnode listen`. Emits phone
`atn-node.conf` for USB/enroll.

Local USB enroll also accepts `peer_domain` (`tools/enroll-console.ps1`).

Router: UPnP maps `UDP 47000 → YOUR_EDGE_LAN_IPV4:47000` (Technicolor IGD), then VM
DNAT → `YOUR_HUB_LAN_IPV4:47000`.

## UDP over 5G

Yes — mesh is **UDP** (DEC-0007). Cellular works if:

1. Phone conf uses **public** WAN IP (`public_ipv4`), not LAN split-DNS.
2. Edge uses DNAT+MASQUERADE with **udp_timeout≥180** (default 30s
   killed return path at boom time). Probes every 3s keep the mapping warm.
3. After Wi‑Fi↔cellular, tap **Start/reconnect** (fresh HS). Stale
   `ESTABLISHED` does not mean the return path is alive.
4. Ping: `rc=0` is send-only; look for `ping echo n=… (hub live)`.
