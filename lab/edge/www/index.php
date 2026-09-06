<?php
/**
 * Athanor lab edge panel — domain + hub keys for phone conf.
 * Served on the public edge host (mesh.example.org/atn/).
 * Does not speak mesh HTTP; only emits atn-node.conf text for USB/enroll.
 */
declare(strict_types=1);
header('X-Content-Type-Options: nosniff');
header('Cache-Control: no-store');

$store = __DIR__ . '/edge-state.json';
$default = [
    'domain' => 'mesh.example.org',
    'public_ipv4' => 'YOUR_PUBLIC_IPV4',
    'peer_port' => '47000',
    'peer_ek' => '',
    'diag' => '1',
    'flush_mode' => 'log_only',
    'outage_class' => 'normal',
    'updated' => '',
];

function load_state(string $store, array $default): array
{
    if (!is_readable($store)) {
        return $default;
    }
    $j = json_decode((string)file_get_contents($store), true);
    if (!is_array($j)) {
        return $default;
    }
    return array_merge($default, $j);
}

function resolve_ipv4(string $host): string
{
    $host = trim($host);
    if ($host === '') {
        return '';
    }
    if (filter_var($host, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
        return $host;
    }
    $ips = @gethostbynamel($host);
    if (!is_array($ips) || count($ips) < 1) {
        return '';
    }
    return $ips[0];
}

function valid_ek(string $ek): bool
{
    return (bool)preg_match('/^[0-9a-fA-F]{3136}$/', $ek);
}

$state = load_state($store, $default);
$msg = '';
$err = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $domain = trim((string)($_POST['domain'] ?? ''));
    $public = trim((string)($_POST['public_ipv4'] ?? ''));
    $port = trim((string)($_POST['peer_port'] ?? ''));
    $ek = strtolower(preg_replace('/\s+/', '', (string)($_POST['peer_ek'] ?? '')) ?? '');
    $diag = isset($_POST['diag']) ? '1' : '0';
    $flush = trim((string)($_POST['flush_mode'] ?? 'log_only'));
    if ($domain === '' || !preg_match('/^[A-Za-z0-9.-]+$/', $domain)) {
        $err = 'bad domain';
    } elseif ($public !== '' && !filter_var($public, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
        $err = 'bad public_ipv4';
    } elseif (!preg_match('/^[1-9][0-9]{0,4}$/', $port) || (int)$port > 65535) {
        $err = 'bad peer_port';
    } elseif ($ek !== '' && !valid_ek($ek)) {
        $err = 'peer_ek must be empty or 3136 hex chars (ML-KEM-1024 ek)';
    } elseif (!in_array($flush, ['log_only', 'zeroize'], true)) {
        $err = 'bad flush_mode';
    } else {
        $state = [
            'domain' => $domain,
            'public_ipv4' => $public,
            'peer_port' => $port,
            'peer_ek' => $ek,
            'diag' => $diag,
            'flush_mode' => $flush,
            'outage_class' => 'normal',
            'updated' => gmdate('c'),
        ];
        if (file_put_contents($store, json_encode($state, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES) . "\n", LOCK_EX) === false) {
            $err = 'failed to write edge-state.json (check permissions)';
        } else {
            $msg = 'saved';
        }
    }
}

$resolvedDns = resolve_ipv4($state['domain']);
$resolved = $state['public_ipv4'] !== '' ? $state['public_ipv4'] : $resolvedDns;
$conf = '';
if ($resolved !== '' && valid_ek($state['peer_ek'])) {
    $conf = "peer_ipv4={$resolved}\n"
        . "peer_port={$state['peer_port']}\n"
        . "peer_ek={$state['peer_ek']}\n"
        . "diag={$state['diag']}\n"
        . "flush_mode={$state['flush_mode']}\n"
        . "outage_class={$state['outage_class']}\n";
}
?><!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<title>Athanor lab edge</title>
<style>
body{font:16px/1.4 ui-monospace,Consolas,monospace;margin:2rem;max-width:52rem;background:#111;color:#ddd}
h1{font-size:1.25rem;color:#fff}
label{display:block;margin:.75rem 0 .25rem}
input[type=text],textarea{width:100%;box-sizing:border-box;padding:.4rem;background:#222;color:#eee;border:1px solid #444}
textarea{min-height:8rem}
button{margin-top:1rem;padding:.5rem 1rem;background:#2a6;color:#fff;border:0;cursor:pointer}
.ok{color:#6c6}.err{color:#f66}.note{color:#aaa;font-size:.9rem}
pre{background:#1a1a1a;padding:1rem;overflow:auto;border:1px solid #333}
</style>
</head>
<body>
<h1>Athanor lab edge</h1>
<p class="note">Public path for phone mesh UDP. Hub ML-KEM ek from
<code>atnnode listen</code> on the Windows hub. Edge VM DNATs UDP to the hub.
Use <strong>public_ipv4</strong> when LAN DNS is split-horizon (domain → 10.x).</p>
<?php if ($msg !== ''): ?><p class="ok"><?= htmlspecialchars($msg, ENT_QUOTES, 'UTF-8') ?></p><?php endif; ?>
<?php if ($err !== ''): ?><p class="err"><?= htmlspecialchars($err, ENT_QUOTES, 'UTF-8') ?></p><?php endif; ?>
<form method="post" action="">
<label>Domain (DNS label)</label>
<input type="text" name="domain" value="<?= htmlspecialchars($state['domain'], ENT_QUOTES, 'UTF-8') ?>" required/>
<label>public_ipv4 (phone conf; overrides LAN DNS)</label>
<input type="text" name="public_ipv4" value="<?= htmlspecialchars($state['public_ipv4'], ENT_QUOTES, 'UTF-8') ?>" placeholder="YOUR_PUBLIC_IPV4"/>
<label>peer_port (UDP)</label>
<input type="text" name="peer_port" value="<?= htmlspecialchars($state['peer_port'], ENT_QUOTES, 'UTF-8') ?>" required/>
<label>peer_ek (hex from atnnode listen)</label>
<textarea name="peer_ek" spellcheck="false"><?= htmlspecialchars($state['peer_ek'], ENT_QUOTES, 'UTF-8') ?></textarea>
<label><input type="checkbox" name="diag" value="1" <?= $state['diag'] === '1' ? 'checked' : '' ?>/> diag=1 (lab keep keys)</label>
<label>flush_mode</label>
<input type="text" name="flush_mode" value="<?= htmlspecialchars($state['flush_mode'], ENT_QUOTES, 'UTF-8') ?>"/>
<button type="submit">Save domain + keys</button>
</form>
<p>DNS A (may be LAN): <strong><?= $resolvedDns !== '' ? htmlspecialchars($resolvedDns, ENT_QUOTES, 'UTF-8') : '(unresolved)' ?></strong>
· Phone peer_ipv4: <strong><?= $resolved !== '' ? htmlspecialchars($resolved, ENT_QUOTES, 'UTF-8') : '(unset)' ?></strong>
<?php if (!empty($state['updated'])): ?> · updated <?= htmlspecialchars($state['updated'], ENT_QUOTES, 'UTF-8') ?><?php endif; ?></p>
<?php if ($conf !== ''): ?>
<h2>phone atn-node.conf</h2>
<pre><?= htmlspecialchars($conf, ENT_QUOTES, 'UTF-8') ?></pre>
<p class="note">Push via USB enroll / adb. Phone on 5G must reach UDP
<?= htmlspecialchars($resolved . ':' . $state['peer_port'], ENT_QUOTES, 'UTF-8') ?>.</p>
<?php else: ?>
<p class="note">Save a valid peer_ek and public_ipv4 (or resolvable domain) to emit conf.</p>
<?php endif; ?>
</body>
</html>
