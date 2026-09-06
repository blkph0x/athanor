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

## 5G soak

1. Hub: `.\atnnode.exe listen 47000` on Windows; paste ek into `/atn/`.
2. Phone conf `peer_ipv4=<public A of mesh.example.org>`.
3. Disable Wi‑Fi; enable mobile data; Start/reconnect; expect MESH UP + hub `recv`.
