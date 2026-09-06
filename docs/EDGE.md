# Lab edge (public internet path)

Phone mesh over **cellular** must not use the hub’s private LAN IP. Use an
edge host that terminates the org’s public DNS name:

```
Phone (cellular) → UDP <org-domain>:<port>
                 → edge host (DMZ / port-forward target)
                 → DNAT/MASQUERADE → Windows hub <hub_lan_ipv4>:<port> (atnnode listen)
```

**Do not commit real org IPs or domains.** Operators enter them in the edge
panel (`lab/edge/www/`); runtime state is `edge-state.json` (gitignored).

## One-time on edge host

1. Open the panel (e.g. `https://<org-domain>/atn/`) and fill:
   - **domain** — org DNS name
   - **public_ipv4** — WAN address phones dial (needed if split-horizon DNS)
   - **hub_lan_ipv4** — Windows hub on the LAN
   - **edge_lan_ipv4** — this host on the LAN (optional)
   - **peer_ek** — from `atnnode listen`
2. Run the install line the panel prints:

```bash
sudo HUB_IP=<hub_lan_ipv4> HUB_PORT=<port> bash lab/edge/atn-udp-forward.sh install
```

`HUB_IP` is required (no default). Router/firewall must forward **UDP**
`<port>` to the edge host (same idea as 80/443).

## Operator panel

Emits phone `atn-node.conf` for USB/enroll. Local USB enroll also accepts
`peer_domain` (`tools/enroll-console.ps1`).

## Peer IP by path

| Path | `peer_ipv4` |
|------|-------------|
| Wi‑Fi on the hub LAN | hub LAN IPv4 (direct) |
| Cellular / off-LAN | public_ipv4 (WAN → edge → DNAT) |

Using the public IP on Wi‑Fi-only after a phone reboot can stick on
**HANDSHAKE** if the hub still has a stale session. Restart `atnnode listen`,
push a fresh `peer_ek`, and prefer the hub LAN IP on Wi‑Fi.

## Linux mapping (checklist)

On the edge host:

- `ip_forward=1`, `rp_filter=0` on the LAN NIC
- `PREROUTING` DNAT `udp/<port> → <hub_lan_ipv4>:<port>`
- `POSTROUTING` MASQUERADE to hub
- `FORWARD` ACCEPT for hub UDP + RELATED,ESTABLISHED
- `nf_conntrack_udp_timeout=180`

Confirm: `sudo HUB_IP=<hub> bash /usr/local/sbin/atn-udp-forward.sh status`

## UDP over cellular

Mesh is **UDP** (DEC-0007). Edge mapping is not the usual failure mode.

1. Phone conf uses **public_ipv4** (or a DNS name that resolves to WAN), not LAN split-DNS alone.
2. Edge DNAT+MASQUERADE with **udp_timeout≥180**.
3. After Wi‑Fi↔cellular, tap **Start/reconnect** (fresh HS). Restart hub if
   it was already `ESTABLISHED` to a dead peer.
4. Ping: `rc=0` is send-only; look for `ping echo n=… (hub live)`.

**CLAT / path MTU:** some carriers present IPv4 via CLAT with a low route
MTU. DEC-0044 splits HS_INIT into ≤512-byte UDP chunks so the join fits.
Both hub and phone must run a build with DEC-0044.
