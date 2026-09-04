================================================================================
PROJECT SOVEREIGNFOUNDRY (SF-ARCH) — CAUSE / EFFECT EXECUTION MAP
================================================================================
VERSION: 2.0.0  |  COMPANION TO: SOURCE_OF_TRUTH.md
STATUS RULE: Flip [ ] to [X] only after the verification gate for that REQ is met.
THIS FILE DOES NOT REPLACE THE SoT. It explains why each REQ exists, how to
finish it, what it causes, and what breaks if it is skipped.
================================================================================

HOW TO USE THIS FILE
--------------------
1. Read SOURCE_OF_TRUTH.md first. That document is law.
2. Use this file to plan, sequence, and verify work.
3. Never start a REQ whose "Depends on" items are still [ ].
4. A REQ is not done because source exists. It is done when the verification
   gate compiles, runs, and proves the stated effect.

GLOBAL CAUSE (WHY THE WHOLE SYSTEM EXISTS)
------------------------------------------
CAUSE: Third-party stacks (WireGuard/OpenVPN, Nginx/Apache/Node, cloud storage,
       vendor crypto libs, package mirrors, CDNs) introduce code we did not
       write, cannot audit line-by-line, and cannot compile in an air gap.
EFFECT IF THIS PROJECT SUCCEEDS: Every packet, page, credential, replica, and
       mobile lock is produced by binaries we authored and compiled.
EFFECT IF ANY REQ IS BYPASSED WITH A VENDOR TOOL: The SoT is violated. The
       system is no longer sovereign even if it "works".


================================================================================
SYSTEM-LEVEL CAUSE → EFFECT CHAIN
================================================================================

TIER 1  crypto primitives
          ↓ produces keys, AEAD, transcripts
        UDP tunnel
          ↓ produces a private encrypted pipe
        2FA challenge-response
          ↓ produces proof of operator identity on that pipe

TIER 2  HTTP/TLS listener  (uses Tier 1 crypto)
          ↓ produces a reachable admin surface
        handwritten admin console
          ↓ produces human control of the mesh
        authoritative DNS
          ↓ produces name→node routing without public DNS vendors

TIER 3  memory-tree store
          ↓ produces local durable state
        replication / sharding
          ↓ produces copies across nodes
        heartbeat mesh
          ↓ produces liveness + kill/wipe authority

TIER 4  Knox daemon
          ↓ binds the human endpoint to the mesh
        biometric + password policy
          ↓ stops casual endpoint takeover
        USB charge-only
          ↓ stops data exfil over cable
        attested memory-flush
          ↓ wipes secrets when integrity or heartbeat fails

TIER 5  air-gapped signer
          ↓ produces trusted update artifacts
        emulated flash pipeline
          ↓ proves updates before they touch hardware
        Faraday cache-wipe
          ↓ proves RF-isolation still destroys cached secrets

TIER 6  fuzz/leak
          ↓ proves the listener will not be the breach
        isolation audit
          ↓ proves no hidden vendor fetch remains
        exportable tree
          ↓ proves a client can rebuild without us


================================================================================
PHASE 1 — PROPRIETARY CORE BUILD & LOW-LEVEL CRYPTO
================================================================================
PHASE CAUSE: Nothing else can be trusted until packets can be encrypted and
             authenticated with math we own.
PHASE EFFECT: Later servers, replicas, 2FA, and mobile attestation all call
             the same primitive layer. If this layer is wrong, every later
             REQ encrypts garbage or trusts forgeries.


--------------------------------------------------------------------------------
REQ-1.1  Custom low-level cryptographic math primitives
--------------------------------------------------------------------------------
STATUS: [X]  (2026-09-04 — tests/test_crypto.exe ALL PASSED; see docs/BUILD_NOTES.md BN-0002)

WHY (CAUSE)
  The SoT forbids OpenSSL, libsodium, BoringSSL, Java crypto providers, and
  any other outside crypto. Confidentiality and authenticity must come from
  code we can read, compile, and freeze.

HOW TO COMPLETE
  1. Pick one implementation language using only the language + OS libc
     (C, Go std, or Rust core/std — no crates.io / no git submodules).
  2. Implement, from first principles, the exact set later binaries need:
       - constant-time compare
       - cryptographically-strong CSPRNG seeded from OS entropy
       - a hash (for transcripts, 2FA, and heartbeat tokens)
       - a MAC / keyed hash
       - a stream or block cipher + a real AEAD construction (encrypt-then-MAC
         or an equivalent authenticated mode — never raw ECB)
       - a key-derivation function (password/handshake → session keys)
       - a nonce/sequence scheme that never repeats under a key
  3. Compile to a static object / static lib. No shared vendor .so/.dll.
  4. Write known-answer tests from published test vectors for the algorithms
     we chose, generated or transcribed by us, stored in-tree.
  5. Add constant-time and memory-zeroization checks.

EFFECT IF DONE
  REQ-1.2, 1.3, 2.1, 3.1, 3.3, 4.1, 4.4, 5.1 all have a single in-house
  crypto substrate. Session keys, heartbeat tokens, TLS-like handshakes,
  and signed updates share one audited math core.

EFFECT IF SKIPPED
  The UDP tunnel cannot be confidential. 2FA cannot be bound to a key.
  Heartbeats can be forged. "Encryption" becomes theater. Downstream REQs
  either stall or cheat with a vendor lib (SoT breach).

DEPENDS ON: none (first compile target)
UNLOCKS:    REQ-1.2, REQ-1.3, REQ-2.1, REQ-5.1

VERIFICATION GATE (must all be true)
  [X] Static binary / object compiles with zero outside crypto libs
  [X] Encrypt → decrypt roundtrip on random payloads
  [X] Tampered ciphertext fails authentication 100% in tests
  [X] Nonce reuse is rejected or architecturally impossible
  [X] Secrets are wiped from buffers after use
      Residual: ISS-0003 (Poly1305 timing not measured on target CPUs).


--------------------------------------------------------------------------------
REQ-1.2  Proprietary UDP network tunneling engine (raw sockets)
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  WireGuard/OpenVPN are foreign code and foreign packet formats. We need a
  pipe we fully specify: header layout, state machine, replay window, and
  close/rekey behavior.

HOW TO COMPLETE
  1. Open a raw UDP socket with OS primitives only.
  2. Define a documented binary header: version, type, seq, nonce, length,
     MAC. Put the spec in-tree next to the code.
  3. State machine: CLOSED → HANDSHAKE → ESTABLISHED → REKEY → CLOSED.
  4. Handshake uses REQ-1.1 to derive session keys. Data packets are AEAD.
  5. Anti-replay sliding window. Drop duplicates and old seq numbers.
  6. Keepalive that later becomes the heartbeat token carrier (REQ-3.3).
  7. Userspace tun/tap or an explicit datagram API so REQ-2.1 and REQ-3.1
     can send bytes through it without knowing header internals.

EFFECT IF DONE
  Nodes can move opaque bytes privately. Web, DNS, replication, and
  heartbeat all ride one transport instead of inventing four tunnels.

EFFECT IF SKIPPED
  Every later service either talks plaintext UDP/TCP (observable, injectable)
  or pulls in WireGuard/OpenVPN (SoT breach). Replication and 2FA have no
  confidential path.

DEPENDS ON: REQ-1.1
UNLOCKS:    REQ-2.1, REQ-2.3, REQ-3.1, REQ-3.3

VERIFICATION GATE
  [ ] Two processes on loopback complete handshake and echo payload
  [ ] Packet capture shows no plaintext of the payload
  [ ] Reordered / replayed packets are dropped
  [ ] Session dies cleanly on MAC failure (no decrypt-anyway path)
  [ ] Compiles with OS sockets only


--------------------------------------------------------------------------------
REQ-1.3  Custom 2FA challenge-response authentication binary
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  Passwords alone are stolen from consoles, notes, and shoulder-surf.
  Vendor TOTP apps and SaaS 2FA put the second factor on someone else's
  algorithm and clock-sync story. We need a second factor whose hash/MAC
  math is our code, bound to our keys.

HOW TO COMPLETE
  1. Choose challenge-response (preferred) over pure TOTP:
       server sends nonce N, client returns MAC(K_device, N || context).
     Time-based is allowed by SoT but must use our hash and a documented
     timestep; challenge-response does not depend on clock skew.
  2. Device secret lives in the Knox-backed store later (REQ-4.1 / 4.4).
     For Phase 1, keep it in an in-memory / file-backed key slot with
     the same API so the mobile daemon can replace the backend.
  3. Standalone binary: enroll, challenge, verify, revoke.
  4. One-time challenges. Replay of a successful response must fail.
  5. Lockout after N failures (pairs with REQ-4.2 policy).

EFFECT IF DONE
  Admin console (REQ-2.2) and node join (REQ-3.3) can demand a second
  factor we issued. Stolen password is not enough to move the mesh.

EFFECT IF SKIPPED
  Anyone with the admin password (or a sniffed session) owns the
  infrastructure. Mobile lockout has nothing cryptographic to bind to.

DEPENDS ON: REQ-1.1
UNLOCKS:    REQ-2.2, REQ-4.2, node admission in REQ-3.3

VERIFICATION GATE
  [ ] Enroll → challenge → verify succeeds
  [ ] Wrong key fails
  [ ] Replay of a used response fails
  [ ] Hash/MAC path is our REQ-1.1 code, not a system crypto provider


================================================================================
PHASE 2 — SOVEREIGN WEB SERVER & GATEWAY ENGINE
================================================================================
PHASE CAUSE: Operators still need a control surface and a name system.
             Nginx/Apache/Node and public DNS are the usual leaks of
             dependency and of data.
PHASE EFFECT: A reachable, authenticated, in-house admin path plus
             in-house name resolution. No vendor HTTP stack, no Cloudflare,
             no bind9 package.


--------------------------------------------------------------------------------
REQ-2.1  In-house HTTP/TLS listener binary
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  A web console is useless if the bytes to the browser are terminated by
  Nginx or a language web framework. The listener is the attack surface
  that faces operators and, if misbuilt, the internet.

HOW TO COMPLETE
  1. TCP listen with OS sockets. Parse HTTP/1.1 ourselves (request line,
     headers, content-length). Reject anything we do not understand.
  2. TLS-equivalent handshake using REQ-1.1 (certificates we issue, AEAD
     records we define). Document the record format. This is not "call
     OpenSSL". It is our handshake on TCP, or our tunnel (REQ-1.2) carrying
     HTTP.
  3. Serve only static bytes loaded into memory at start (REQ-2.2 pages).
     No CGI, no template engine, no interpreter.
  4. Hard limits: max header size, max connections, idle timeout.
  5. Bind loopback or tunnel-only by default. Public bind is an explicit
     config flag, never the compile default.

EFFECT IF DONE
  REQ-2.2 has somewhere to live. REQ-6.1 has a binary to fuzz. Operators
  reach the mesh without a vendor reverse proxy.

EFFECT IF SKIPPED
  Console is either "open a file in a browser" (no remote, no TLS) or
  we cheat with Nginx/Node (SoT breach). Fuzzing in Phase 6 has no target.

DEPENDS ON: REQ-1.1, REQ-1.2
UNLOCKS:    REQ-2.2, REQ-6.1

VERIFICATION GATE
  [ ] Listener compiles with zero HTTP/TLS libraries
  [ ] GET of a memory-resident page returns exact bytes
  [ ] Oversized headers / unknown methods are rejected
  [ ] Unauthenticated sockets cannot read admin pages
  [ ] Traffic is authenticated+encrypted (our handshake or our tunnel)


--------------------------------------------------------------------------------
REQ-2.2  Raw, framework-free web administration console
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  Someone has to enroll nodes, revoke 2FA, inspect heartbeat, and trigger
  wipes. A React/Vue/npm console reintroduces the entire package-mirror
  problem into the one UI operators trust.

HOW TO COMPLETE
  1. Handwrite HTML + CSS + the smallest possible vanilla JS, all in-tree.
     No CDNs, no webfonts, no analytics, no external image hosts.
  2. Pages: login, 2FA challenge, node list, heartbeat status, enroll/
     revoke, replication health, wipe/lock commands, build/sign status.
  3. Forms POST to our listener. CSRF token derived from REQ-1.1 session.
  4. Every mutating action requires a fresh 2FA response (REQ-1.3).
  5. Assets compiled/embedded into the listener so the binary is the site.

EFFECT IF DONE
  The mesh is operable by a human without SSH-only tribal knowledge.
  Phase 5 OTA and Phase 3 node ops have a control surface.

EFFECT IF SKIPPED
  Operators improvise with curl and hex editors. Mistakes become outages
  or skipped 2FA. Or someone adds a framework "just for the UI".

DEPENDS ON: REQ-2.1, REQ-1.3
UNLOCKS:    operable Phase 3–5 workflows

VERIFICATION GATE
  [ ] Console loads with network disabled except our listener
  [ ] View-source / binary strings show no cdn / npm / google / cloudflare
  [ ] Login without 2FA cannot mutate state
  [ ] Pages are the embedded copies, not files read from a writable dir


--------------------------------------------------------------------------------
REQ-2.3  Custom authoritative DNS responder
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  Nodes and admin hosts must resolve each other without depending on
  Google/Cloudflare/ISP DNS, and without shipping bind/unbound packages.
  DNS is also how we keep names stable when IPs change across jurisdictions.

HOW TO COMPLETE
  1. UDP/53 (and TCP/53) listener, OS sockets, our parser for the DNS
     message format we will answer (A/AAAA/TXT as needed).
  2. Zone data comes from the memory-tree store (REQ-3.2) once it exists;
     until then, an embedded static zone is acceptable as a scaffold.
  3. Only answers names we own. Recursion off. No forwarding to public
     resolvers (that would violate REQ-6.2).
  4. Optionally serve over REQ-1.2 so queries are not plaintext on a WAN.
  5. Admin console can add/remove records after 2FA.

EFFECT IF DONE
  Clients find nodes by name we control. Peer discovery (REQ-3.3) has a
  first-contact mechanism that is not a public DNS vendor.

EFFECT IF SKIPPED
  Hardcoded IPs everywhere, or leakage to public DNS. Multi-jurisdiction
  failover in REQ-3.3 has no naming layer.

DEPENDS ON: REQ-1.2  |  full zone persistence waits on REQ-3.2
UNLOCKS:    REQ-3.3 peer bootstrap by name

VERIFICATION GATE
  [ ] Query for an in-zone name returns our record
  [ ] Query for an out-of-zone name is refused, not forwarded
  [ ] Packet capture of a resolve shows no traffic to 8.8.8.8 / 1.1.1.1
  [ ] Binary links no DNS library besides our parser


================================================================================
PHASE 3 — DISTRIBUTED STORAGE & NODE INTER-COMMUNICATION
================================================================================
PHASE CAUSE: Cloud disks and hosted DBs are other people's computers.
             Sovereignty requires that state live in our process memory
             and replicate over our pipe.
PHASE EFFECT: Encrypted blocks exist on more than one node; a dead node
             is detectable; a silent node can be ordered to wipe.


--------------------------------------------------------------------------------
REQ-3.1  Distributed replication and sharding
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  One box is a single point of seizure, fire, and firmware death.
  Sharding keeps working sets small; replication keeps them alive.

HOW TO COMPLETE
  1. Define a block: id, shard key, version/vector clock, AEAD payload
     (REQ-1.1), origin node id.
  2. Shard function: hash(key) → shard id → responsible node set.
     Document the function; changing it later is a migration.
  3. Replicate over REQ-1.2 streams. Never plaintext TCP for block bodies.
  4. Conflict rule: last-writer-wins is not enough for heartbeats vs
     user data. Use version vectors; expose conflicts to the console.
  5. Catch-up protocol for a node that was offline.
  6. Minimum replica count is a compile-time or signed-config constant.

EFFECT IF DONE
  REQ-3.2 data survives a node loss. Multi-jurisdiction copies exist
  without S3/GCS/Azure.

EFFECT IF SKIPPED
  The "database" is a single process. Heartbeat mesh has nothing durable
  to agree on. A wipe or crash is permanent data loss.

DEPENDS ON: REQ-1.1, REQ-1.2
UNLOCKS:    REQ-3.2 durability, REQ-3.3 shared view of membership

VERIFICATION GATE
  [ ] Write on node A is readable on node B after replicate
  [ ] Killing A still serves the block from B
  [ ] Tampered block is rejected by MAC
  [ ] Shard map is deterministic for the same key set


--------------------------------------------------------------------------------
REQ-3.2  Self-contained database logic (binary memory trees)
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  SQLite/Postgres/LevelDB are third-party engines. We need an in-process
  structure we can dump, encrypt, and walk without a vendor pager.

HOW TO COMPLETE
  1. Implement an on-heap binary tree or B+tree: insert, get, delete,
     ordered scan. Keys and values are length-prefixed blobs.
  2. Optional mmap-backed snapshot for restart: write encrypted pages
     (REQ-1.1) to a single file we own. No SQL parser.
  3. Trees stored: node roster, shards, DNS zone, 2FA enrollments,
     heartbeat last-seen, admin sessions, wipe flags.
  4. Explicit fsync / snapshot API so replication has a consistent cut.
  5. Zeroization of freed nodes.

EFFECT IF DONE
  Every service has a local source of truth that is just our process.
  DNS, 2FA, and heartbeat stop using ad-hoc files.

EFFECT IF SKIPPED
  State lives in scattered structs and text files. Replication has no
  stable iterator. Restarts lose enrollments and zone data.

DEPENDS ON: REQ-1.1  |  replication of snapshots uses REQ-3.1
UNLOCKS:    persistent DNS (2.3), membership for 3.3, admin data for 2.2

VERIFICATION GATE
  [ ] Insert / get / delete / scan pass on large random keys
  [ ] Snapshot → process restart → same gets
  [ ] Snapshot bytes are not plaintext of values
  [ ] No database library in the link line


--------------------------------------------------------------------------------
REQ-3.3  Multi-jurisdiction P2P heartbeat mesh
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  This is the live wire of the whole design. Heartbeats prove a node is
  still ours. Missing a cryptographic token is the cause that triggers
  memory self-destruct (Tier 3 + REQ-4.4). Without a mesh, wipe policy
  is either never-fire or always-fire.

HOW TO COMPLETE
  1. Peer discovery: seed list from DNS (REQ-2.3) + signed roster in the
     memory tree. No mDNS to random LANs. No cloud coordinator.
  2. Each interval, node emits token =
       MAC(K_node, time_bucket || roster_epoch || last_block_head)
     using REQ-1.1. Send over REQ-1.2.
  3. Peers verify MAC, window, and roster epoch. Record last-seen in
     REQ-3.2.
  4. Miss policy (documented constants):
       - N missed tokens → mark UNTRUSTED
       - UNTRUSTED → stop serving replicas
       - UNTRUSTED beyond M → local memory wipe (server) and notify
         mobile daemon (REQ-4.4) to flush endpoint secrets
  5. Wipe is of OUR process memory and OUR key slots — not a worm,
     not other tenants, not other vendors' devices.

EFFECT IF DONE
  Split-brain and seizure have a defined reaction. Mobile attestation
  has a signal. Admin console shows living vs dead nodes.

EFFECT IF SKIPPED
  Dead or captured nodes keep serving. Self-destruct never has a
  trustworthy trigger, so it will either never run or run on false
  positives and destroy the mesh.

DEPENDS ON: REQ-1.1, REQ-1.2, REQ-1.3 (node admission), REQ-2.3, REQ-3.2
UNLOCKS:    REQ-4.4 trigger path, REQ-5.3 related isolation wipe

VERIFICATION GATE
  [ ] Three nodes stay ESTABLISHED under lossy UDP
  [ ] Forged heartbeat (wrong MAC) is ignored
  [ ] Forced silence of one node reaches UNTRUSTED then wipe of that
      node's in-memory keys only
  [ ] Remaining nodes continue and record the death


================================================================================
PHASE 4 — MOBILE ENCLAVE INTERFACE (SAMSUNG KNOX HARDWARE BONDING)
================================================================================
PHASE CAUSE: The human endpoint (S24/S25/S26) is the most stolen, most
             inspected, most USB-connected part of the system. Software
             on the phone must be bonded to Knox-backed policy, not to
             a normal Android app sandbox pretending to be a vault.
PHASE EFFECT: The phone can prove it is still the enrolled device, refuse
             weak unlock, refuse USB data, and flush secrets when the
             mesh says so.

HARDWARE REALITY (DO NOT FANTASIZE PAST THIS)
  Samsung Knox SDK / Knox Platform for Enterprise APIs are the supported
  way to bind policy on stock S24–S26. TrustZone/TIMA is Samsung firmware.
  We ATTACH to attestation and keystore. We do not patch Samsung's TIMA
  binary or ship a custom kernel unless we are the ROM owner. REQ-4.3 and
  REQ-4.4 are therefore implemented through Knox restriction + attested
  daemon behavior, not through injecting foreign instructions into TIMA.


--------------------------------------------------------------------------------
REQ-4.1  Custom Android background daemon on raw Knox SDK layers
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  A normal Activity dies, gets swapped, and cannot hold hardware-backed
  keys or enterprise restrictions. The daemon is the phone's half of
  the heartbeat and the holder of the device 2FA secret.

HOW TO COMPLETE
  1. In-house Android service (our Java/Kotlin/C as needed) with Knox
     APIs only — no Firebase, no Play services, no Crashlytics, no
     Retrofit/OkHttp stacks. Use java.net or our own JNI sockets so
     traffic can move onto REQ-1.2.
  2. Enroll with the mesh: store K_device in Knox-backed keystore /
     TIMA-attested key slot as the SDK allows on the target devices.
  3. Foreground service + Knox keepalive so it survives Doze as far as
     policy allows. Heartbeat client for REQ-3.3.
  4. No Google/Samsung cloud backup of our files.

EFFECT IF DONE
  The phone is a mesh member, not a browser bookmark. 4.2–4.4 have a
  process to live in.

EFFECT IF SKIPPED
  Endpoint is an ordinary app. Biometrics, USB policy, and wipes have
  nowhere privileged to run. Heartbeat cannot be hardware-linked.

DEPENDS ON: REQ-1.1, REQ-1.2, REQ-1.3, REQ-3.3 protocol
UNLOCKS:    REQ-4.2, REQ-4.3, REQ-4.4, REQ-5.3 on-device trigger

VERIFICATION GATE
  [ ] Service starts on boot of a Knox-capable enrolled device
  [ ] Device key is non-exportable via Knox/TIMA keystore APIs
  [ ] Daemon speaks the tunnel protocol to a lab node
  [ ] APK/binary contains no third-party SDK beyond Knox/Android stubs


--------------------------------------------------------------------------------
REQ-4.2  Biometric lockout + 12+ character alphanumeric password
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  Stolen unlocked phones dump caches. Weak PINs are brute-forced.
  This REQ makes "having the glass" insufficient.

HOW TO COMPLETE
  1. Via Knox password/biometric policy APIs (and our app gate):
       - minimum length 12
       - alphanumeric required (letters + digits; symbols allowed)
       - biometric allowed as convenience only after the strong password
         exists; biometric failure falls back to the password, not to bypass
  2. After K failures: local lock, mesh notify, optional wipe flag
     (coordinates with 4.4). Constants documented.
  3. Our admin console login on-device must not be weaker than device
     policy (reuse REQ-1.3).

EFFECT IF DONE
  Casual seizure does not equal mesh access. Complements 2FA: something
  you are/have + something you know + device key.

EFFECT IF SKIPPED
  Device policy is whatever the user set (often 4 digits). REQ-4.4 wipe
  never gets a lockout cause. Stolen phone is an admin token.

DEPENDS ON: REQ-4.1, REQ-1.3
UNLOCKS:    defensible endpoint; REQ-6 sign-off for "endpoint not trivial"

VERIFICATION GATE
  [ ] Policy rejects passwords shorter than 12 and letter-only / digit-only
  [ ] Biometric cannot open the vault if the password was never set
  [ ] K wrong tries lock the app and report to a lab node


--------------------------------------------------------------------------------
REQ-4.3  USB data lanes forced to charge-only
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  USB is the fastest way to image a phone, drop a payload, or run ADB.
  Charge-only removes the data wires from the threat model while the
  device is enrolled.

HOW TO COMPLETE
  1. Use Knox restriction / USB policy APIs available on S24–S26
     enterprise/Knox licensed devices to disable USB data, MTP, PTP, ADB.
     Charging remains.
  2. Daemon re-asserts the policy on boot and on USB connect events.
  3. Do not rely on a user toggle. Policy is enforced, not suggested.
  4. Lab exception: a signed, 2FA-gated, time-limited debug unlock for
     our own devices only — default is charge-only.

EFFECT IF DONE
  Cable plug-in cannot exfiltrate the memory-tree cache or install a
  second admin. Faraday testing (REQ-5.3) is not bypassed by a USB poke.

EFFECT IF SKIPPED
  Any charger kiosk or hostile cable becomes a dump path. Kernel-level
  language in the SoT is satisfied only if policy actually disables data,
  not if we wrote a comment about it.

DEPENDS ON: REQ-4.1
UNLOCKS:    REQ-5.3 (USB is not an alternate exfil during RF isolation)

VERIFICATION GATE
  [ ] Host PC does not enumerate MTP/ADB on an enrolled device
  [ ] Charging still works
  [ ] Policy still holds after reboot
  [ ] Unsigned app cannot clear the restriction


--------------------------------------------------------------------------------
REQ-4.4  Memory-flush sequence bonded to Knox/TIMA attestation
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  Heartbeat miss, failed attestation, or lockout must destroy secrets
  in RAM and key slots so a later dump is empty. The SoT's "TIMA loop"
  intent is: integrity measurement keeps running, and our flush is
  scheduled in that trust story — not that we overwrite Samsung firmware.

HOW TO COMPLETE
  1. Define flush as: zeroize heap secrets, drop tunnel keys, delete
     non-hardware copies, lock the tree snapshot, optionally destroy
     Knox key aliases if the SDK permits for our enrollment.
  2. Triggers (all logged):
       - REQ-3.3 UNTRUSTED / missed token
       - Knox/TIMA attestation failure or tamper callback
       - REQ-4.2 lockout threshold
       - REQ-5.3 Faraday / RF-isolation trigger
  3. Register the flush on attested boot. If attestation cannot be
     obtained, refuse to load keys at all (fail closed).
  4. Prove flush with a lab build that can dump process memory before
     and after (on our devices).

EFFECT IF DONE
  Capture of a live node or phone after the trigger yields no usable
  session keys. This is the effect the SoT named "self-destruct of its
  own memory space".

EFFECT IF SKIPPED
  Heartbeat is cosmetic. A seized phone or silent server still holds
  keys for hours. Faraday wipe (5.3) has nothing to call.

DEPENDS ON: REQ-4.1, REQ-3.3, REQ-1.1
UNLOCKS:    REQ-5.3, Phase 6 "secrets do not survive compromise" claim

VERIFICATION GATE
  [ ] After trigger, in-process key buffers are zero
  [ ] Tunnel no longer decrypts
  [ ] Re-enroll requires 2FA + admin, not just reboot
  [ ] Attestation failure prevents key load


================================================================================
PHASE 5 — REMOTE CONTROL & AUTOMATED OVER-THE-AIR PIPELINE
================================================================================
PHASE CAUSE: Shipping updates via GitHub Actions, Play Store, or a vendor
             compiler reintroduces the supply chain we spent Phases 1–4
             eliminating.
PHASE EFFECT: We can change code, prove it, sign it, and flash it without
             a third-party build cloud.


--------------------------------------------------------------------------------
REQ-5.1  Air-gapped binary build + sign chain
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  Compilers and CI runners that fetch crates/npm/apt while building can
  inject the next dependency. Signing on a networked box leaks the only
  key that matters.

HOW TO COMPLETE
  1. A dedicated build machine with no default route. Toolchain is
     our frozen compiler binaries stored in-tree or on write-once media.
  2. Build graph: primitives → tunnel → listener → dns → store →
     android daemon. One recipe file we wrote.
  3. Sign artifacts with REQ-1.1 (or a dedicated in-house signature
     primitive in that lib). Signing key never on a net-connected host.
  4. Hash list of every source file goes into the signed manifest.

EFFECT IF DONE
  REQ-5.2 has authentic bits to test. Clients in REQ-6.3 can verify
  they built the same bytes.

EFFECT IF SKIPPED
  Every "update" is an unsigned blob. OTA becomes the easiest backdoor.
  Isolation claim in 6.2 is false the first time the builder apt-gets.

DEPENDS ON: REQ-1.1
UNLOCKS:    REQ-5.2, REQ-6.3

VERIFICATION GATE
  [ ] Full product builds with network interface down
  [ ] Signature verifies on a second machine using only our code
  [ ] Build fails if any fetch URL is present in the recipe


--------------------------------------------------------------------------------
REQ-5.2  In-house automated testing pipeline (emulate before flash)
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  Flashing phones and servers with untested tunnel/crypto/wipe code
  bricks the fleet or wipes production keys. Emulation is the dress
  rehearsal that does not consume hardware.

HOW TO COMPLETE
  1. Headless runner on the air-gapped builder: unit tests for 1.1–3.3,
     process-level integration (two tunnel ends, three-node mesh),
     HTTP listener checks, DNS checks.
  2. Android side: emulator or dedicated lab S24 image, not Play-store
     test clouds.
  3. A "flash candidate" is only emitted if the runner's signed report
     is PASS.
  4. Include negative tests: replay, forged heartbeat, USB policy,
     wipe trigger.

EFFECT IF DONE
  Hardware sees fewer suicides. REQ-6.1 fuzz can be scheduled as a
  stage in the same runner.

EFFECT IF SKIPPED
  Humans "test on the device" and ship regressions. Faraday and wipe
  tests become one-shot tribal rituals.

DEPENDS ON: REQ-5.1, code from Phases 1–4
UNLOCKS:    REQ-5.3 repeatability, REQ-6.1 as a pipeline stage

VERIFICATION GATE
  [ ] Pipeline produces a signed PASS/FAIL artifact
  [ ] A deliberately broken handshake fails the pipeline
  [ ] No test step calls a network package mirror


--------------------------------------------------------------------------------
REQ-5.3  Faraday-bag automatic cache-wipe trigger (verified in test)
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  RF isolation (Faraday bag, basement, jammer, airplane faraday) is a
  real operational event: the phone can no longer hear the mesh. If
  caches survive isolation, an examiner opens the bag later and reads
  memory. The trigger must fire because of lost cryptographic heartbeat
  / link, not because of a toy "airplane mode" checkbox.

HOW TO COMPLETE
  1. Reuse REQ-3.3 miss counters inside REQ-4.1. No extra vendor location
     or "safety app".
  2. Distinguish: user-toggled airplane (policy choice) vs unexplained
     total RF drop while enrolled. Document the rule so we do not wipe
     every underground elevator ride unless we intend to.
  3. On fire: call REQ-4.4 flush. Optional: wipe on-disk tree snapshot.
  4. Lab test: place enrolled device in a measured Faraday bag, confirm
     heartbeat misses, confirm flush, confirm USB still charge-only.

EFFECT IF DONE
  Isolation becomes a feature: secrets die when the mesh cannot be
  reached, which is exactly when physical examination usually starts.

EFFECT IF SKIPPED
  Faraday bag is a slogan. Caches remain. REQ-4.4 is only tested by
  software flags, not by the real operational cause.

DEPENDS ON: REQ-4.1, REQ-4.4, REQ-3.3, REQ-4.3 (no USB bypass)
UNLOCKS:    Phase 6 operational sign-off for endpoint capture

VERIFICATION GATE
  [ ] Bag test: tokens stop, flush runs, keys gone
  [ ] Control test: device in RF with mesh alive does not flush
  [ ] After flush, reboot does not resurrect session keys


================================================================================
PHASE 6 — RE-ENGINEERING VALIDATION & LOGISTICAL SIGN-OFF
================================================================================
PHASE CAUSE: A sovereign design that was never abused in-house is just
             a story. Sign-off is the proof we can hand a client.
PHASE EFFECT: The listener is crash-resistant, the tree has no vendor
             fetches, and a third party can compile what we compiled.


--------------------------------------------------------------------------------
REQ-6.1  Fuzzing and memory-leak tests on the web server binary
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  REQ-2.1 is the longest-lived parser we wrote. Parser bugs become
  remote code execution, and leaks become key recovery. Vendor fuzzers
  as services are off-limits; the harness must be ours.

HOW TO COMPLETE
  1. In-house mutator feeding REQ-2.1 stdin/socket with garbage HTTP
     and garbage handshake records.
  2. Run under an in-house or OS allocator shim that tracks alloc/free
     (our code, or compiler asan if the frozen toolchain includes it —
     still not an external service).
  3. Crash = fail the REQ-5.2 pipeline.
  4. Same harness later pointed at DNS and tunnel parsers.

EFFECT IF DONE
  We have evidence the admin surface is not a gift to the first malformed
  packet. Sign-off is not just "it loaded in a browser".

EFFECT IF SKIPPED
  First hostile client owns the console. Phase 6 cannot honestly close.

DEPENDS ON: REQ-2.1, REQ-5.2
UNLOCKS:    REQ-6.3 credibility

VERIFICATION GATE
  [ ] N hours fuzz, zero unhandled crashes (N documented)
  [ ] Leak detector stable on long GET/POST loops
  [ ] Findings either fixed or explicitly accepted in-tree


--------------------------------------------------------------------------------
REQ-6.2  Confirm 100% isolation from third-party networks, mirrors, CDNs
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  This is the SoT's pass/fail. One apt/npm/crates/go-proxy fetch, one
  webfont, one crash reporter, and the system is not independent.

HOW TO COMPLETE
  1. Static audit: grep/link-line/strings for URLs, package registries,
     CDN hosts, telemetry.
  2. Dynamic audit: build + run all binaries with a default-deny netns
     or disconnected NIC. Only expected tunnel/DNS/heartbeat peers.
  3. Console loaded with browser devtools offline except our listener.
  4. Android daemon: no Play, no Firebase, no OEM cloud backup.
  5. Write an isolation report into the export (REQ-6.3).

EFFECT IF DONE
  We can state "NONE (0%)" as a measured fact, not a banner.

EFFECT IF SKIPPED
  Hidden fetches remain. Clients compiling "our" tree may still phone
  home. The project name becomes marketing.

DEPENDS ON: all runtime REQs 1.1–5.3
UNLOCKS:    honest REQ-6.3 export

VERIFICATION GATE
  [ ] Disconnected build succeeds
  [ ] Disconnected runtime of server + console succeeds
  [ ] Strings/link audit has zero vendor network endpoints
  [ ] Report filed in-tree


--------------------------------------------------------------------------------
REQ-6.3  Export the complete closed-loop codebase for independent compile
--------------------------------------------------------------------------------
STATUS: [ ]

WHY (CAUSE)
  Sovereignty that only exists on our disk is not transferable. A client
  must rebuild every binary from a single tree, on their air gap, with
  our recipe, and get matching hashes.

HOW TO COMPLETE
  1. One export archive: source, SoT, this map, frozen toolchain notes,
     recipes, test vectors, isolation report, signed hash list.
  2. No "download the compiler from the internet" steps.
  3. Reproduce on a clean machine: same hashes for REQ-1.1 through 2.3
     at minimum; document any hardware-tied Android differences.
  4. Client compile does not require our live nodes.

EFFECT IF DONE
  The loop is closed. The SoT sentence "independent client compilation"
  is true. Our job as authors is inspectable.

EFFECT IF SKIPPED
  Clients must trust prebuilt blobs — the opposite of the project.
  Any later incident cannot be independently reproduced.

DEPENDS ON: REQ-5.1, REQ-6.1, REQ-6.2
UNLOCKS:    logistical sign-off / end of v2.0.0 checklist

VERIFICATION GATE
  [ ] Clean-room compile from export succeeds
  [ ] Artifact hashes match the signed list
  [ ] Export contains SOURCE_OF_TRUTH.md and this map
  [ ] Export contains no hidden vendor tarballs


================================================================================
EXECUTION ORDER (DO NOT SKIP AHEAD)
================================================================================

  1.1 crypto
        ├─► 1.2 tunnel ─► 2.1 listener ─► 2.2 console
        │                    └─► 6.1 fuzz
        ├─► 1.3 2FA ──────────► 2.2 console
        └─► 5.1 signer ─► 5.2 pipeline ─► 6.2 isolation ─► 6.3 export

  1.2 tunnel ─► 2.3 DNS ─► 3.3 heartbeat
  1.1 + 1.2 ─► 3.1 replicate
  1.1 ─► 3.2 memory tree ─► 3.3 heartbeat
  3.3 + 1.2 + 1.3 ─► 4.1 Knox daemon
        ├─► 4.2 password/biometric
        ├─► 4.3 USB charge-only
        └─► 4.4 attested flush ◄─ 3.3 misses
                └─► 5.3 Faraday test (needs 4.3 so USB is not a bypass)

FORBIDDEN SHORTCUTS
  - Linking OpenSSL "just to get TLS working" kills REQ-2.1 and REQ-6.2.
  - npm for the console kills REQ-2.2 and REQ-6.2.
  - Public DNS forwarders kill REQ-2.3 and REQ-6.2.
  - Play services / Firebase for the daemon kills REQ-4.1 and REQ-6.2.
  - Signing on a networked laptop kills REQ-5.1.
  - Marking a checkbox because code was typed, not because the gate ran,
    kills the SoT tracking rule.


================================================================================
ROLL-UP CHECKLIST (MIRROR OF SoT, WITH CAUSE IN ONE LINE)
================================================================================

PHASE 1
[X] REQ-1.1  Crypto primitives     — CAUSE: own the math; EFFECT: every secret derives here
[X] REQ-1.1-PQ ML-KEM-1024 + SHA-3  — CAUSE: quantum computers; EFFECT: key establishment is FIPS 203 category 5
[ ] REQ-1.2  UDP tunnel            — CAUSE: own the pipe; EFFECT: no WireGuard/OpenVPN
[ ] REQ-1.3  2FA binary            — CAUSE: own the second factor; EFFECT: password theft is not enough

PHASE 2
[ ] REQ-2.1  HTTP/TLS listener     — CAUSE: own the socket; EFFECT: no Nginx/Node
[ ] REQ-2.2  Admin console         — CAUSE: own the UI bytes; EFFECT: no CDN/framework
[ ] REQ-2.3  Auth DNS              — CAUSE: own the names; EFFECT: no public resolver

PHASE 3
[ ] REQ-3.1  Replicate/shard       — CAUSE: no cloud disk; EFFECT: node loss is not data loss
[ ] REQ-3.2  Memory trees          — CAUSE: no vendor DB; EFFECT: state is our process
[ ] REQ-3.3  Heartbeat mesh        — CAUSE: need a live proof of "still ours"; EFFECT: wipe has a trigger

PHASE 4
[ ] REQ-4.1  Knox daemon           — CAUSE: phone must be a mesh member; EFFECT: 4.2–4.4 have a process
[ ] REQ-4.2  Biometric+12 char     — CAUSE: stolen glass; EFFECT: weak PIN cannot open the vault
[ ] REQ-4.3  USB charge-only       — CAUSE: cable dump/ADB; EFFECT: charge without data
[ ] REQ-4.4  Attested memory flush — CAUSE: capture after miss/tamper; EFFECT: keys die in our memory

PHASE 5
[ ] REQ-5.1  Air-gap sign chain    — CAUSE: CI/supply chain; EFFECT: updates are our signatures
[ ] REQ-5.2  Emulated test pipe    — CAUSE: flash-without-proof bricks fleet; EFFECT: only PASS artifacts flash
[ ] REQ-5.3  Faraday wipe test     — CAUSE: RF isolation precedes exam; EFFECT: caches empty in the bag

PHASE 6
[ ] REQ-6.1  Fuzz + leak           — CAUSE: our parser is the breach surface; EFFECT: crashes are found by us first
[ ] REQ-6.2  Isolation audit       — CAUSE: one fetch undoes sovereignty; EFFECT: "0% external" is measured
[ ] REQ-6.3  Exportable tree       — CAUSE: trust must be reconstructable; EFFECT: client compiles without us

================================================================================
END OF CAUSE / EFFECT MAP — OBEY SOURCE_OF_TRUTH.md, TRACK GATES HERE
================================================================================
