require('dotenv').config();
const express = require('express');
const cors = require('cors');
const crypto = require('crypto');
const { DatabaseSync } = require('node:sqlite');
const { exec } = require('child_process');
const nodemailer = require('nodemailer');

const PORT = parseInt(process.env.PORT || '3000', 10);

const db = new DatabaseSync(process.env.DB_PATH || './gd.db');
db.exec(`
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  username TEXT NOT NULL UNIQUE,
  email TEXT NOT NULL UNIQUE,
  pass_hash TEXT NOT NULL,
  role TEXT NOT NULL DEFAULT 'player',
  verified INTEGER NOT NULL DEFAULT 0,
  code TEXT,
  code_created INTEGER,
  created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions (
  token TEXT PRIMARY KEY,
  user_id INTEGER NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS saved_accounts (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  device TEXT NOT NULL,
  token TEXT NOT NULL,
  username TEXT NOT NULL,
  role TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  UNIQUE(device, username)
);
`);
function ensureColumn(table, name, def) {
  const found = db.prepare('PRAGMA table_info(' + table + ')').all().some(c => c.name === name);
  if (!found) db.exec('ALTER TABLE ' + table + ' ADD COLUMN ' + name + ' ' + def);
}
ensureColumn('users', 'reset_code', 'TEXT');
ensureColumn('users', 'reset_created', 'INTEGER');
ensureColumn('users', 'pending_email', 'TEXT');
ensureColumn('users', 'pending_code', 'TEXT');
ensureColumn('users', 'pending_created', 'INTEGER');

/* ---------------- Password hashing ---------------- */
function hashPass(p) {
  const salt = crypto.randomBytes(16).toString('hex');
  return salt + ':' + crypto.scryptSync(p, salt, 64).toString('hex');
}
function checkPass(p, stored) {
  const [salt, hash] = String(stored).split(':');
  const calc = crypto.scryptSync(p, salt, 64);
  return crypto.timingSafeEqual(Buffer.from(hash, 'hex'), calc);
}
function newCode() {
  return String(Math.floor(100000 + Math.random() * 900000));
}
function newToken() {
  return crypto.randomBytes(32).toString('hex');
}

/* ---------------- Email ---------------- */
let mailDev = false;
let transporter = null;
const IS_PROD = process.env.NODE_ENV === 'production';
function buildTransporter() {
  if (process.env.SMTP_HOST) {
    transporter = nodemailer.createTransport({
      host: process.env.SMTP_HOST,
      port: parseInt(process.env.SMTP_PORT || '587', 10),
      secure: process.env.SMTP_SECURE === 'true',
      auth: process.env.SMTP_USER && process.env.SMTP_PASS
        ? { user: process.env.SMTP_USER, pass: process.env.SMTP_PASS }
        : undefined
    });
  } else {
    mailDev = true;
  }
}
async function sendMessage(username, email, subject, text) {
  if (IS_PROD && (mailDev || !transporter)) {
    throw new Error('SMTP not configured — set SMTP_HOST, SMTP_USER, SMTP_PASS on the server');
  }
  if (mailDev || !transporter) {
    console.log('[DEV EMAIL] To %s <%s>: %s', username, email, text.replace(/\n/g, ' | '));
    return;
  }
  await transporter.sendMail({
    from: process.env.MAIL_FROM || 'Geometry Extra <noreply@example.com>',
    to: email,
    subject,
    text
  });
}
async function sendCode(username, email, code) {
  await sendMessage(username, email, 'Geometry Extra — verify your account',
    'Hi ' + username + ',\n\nYour Geometry Extra verification code is ' + code + '.\nIt expires in 15 minutes.\n\n— Geometry Extra');
}
buildTransporter();

/* ---------------- App ---------------- */
const app = express();
app.use(cors());
app.use(express.json());
app.use(express.static(__dirname + '/..'));
app.get('/', (req, res) => res.redirect('/geometry-dash.html'));

function publicUser(u) {
  return { name: u.username, role: u.role };
}

function auth(req, res, next) {
  const h = req.headers.authorization || '';
  const token = h.startsWith('Bearer ') ? h.slice(7) : null;
  if (!token) return res.status(401).json({ error: 'Not logged in' });
  const row = db.prepare(
    'SELECT u.id, u.username, u.role FROM sessions s JOIN users u ON u.id = s.user_id WHERE s.token = ?'
  ).get(token);
  if (!row) return res.status(401).json({ error: 'Invalid session' });
  req.user = { id: row.id, name: row.username, role: row.role };
  req.token = token;
  next();
}

function parseCode(str) {
  return str != null ? String(str).trim() : '';
}

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

/* --- Register --- */
app.post('/api/register', async (req, res) => {
  const username = String(req.body?.username || '').trim();
  const email = String(req.body?.email || '').trim().toLowerCase();
  const password = String(req.body?.password || '');
  if (username.length < 2 || username.length > 16) return res.status(400).json({ error: 'Username 2–16 chars' });
  if (!EMAIL_RE.test(email)) return res.status(400).json({ error: 'Enter a valid email' });
  if (password.length < 4) return res.status(400).json({ error: 'Password too short' });
  if (db.prepare('SELECT id FROM users WHERE username = ?').get(username)) return res.status(400).json({ error: 'Username taken' });
  if (db.prepare('SELECT id FROM users WHERE email = ?').get(email)) return res.status(400).json({ error: 'Email already registered' });

  const code = newCode();
  const info = db.prepare('INSERT INTO users (username, email, pass_hash, role, verified, code, code_created, created_at) VALUES (?, ?, ?, ?, 0, ?, ?, ?)')
    .run(username, email, hashPass(password), 'player', code, Date.now(), Date.now());

  try {
    await sendCode(username, email, code);
  } catch (e) {
    console.error('Email send failed:', e.message);
    db.prepare('DELETE FROM users WHERE id = ?').run(Number(info.lastInsertRowid));
    return res.status(500).json({ error: 'Verification email failed: ' + e.message });
  }
  res.json({ ok: true, dev: mailDev });
});

/* --- Resend code --- */
app.post('/api/resend', async (req, res) => {
  const username = String(req.body?.username || '').trim();
  const user = db.prepare('SELECT * FROM users WHERE username = ?').get(username);
  if (!user) return res.status(404).json({ error: 'Account not found' });
  const code = newCode();
  db.prepare('UPDATE users SET code = ?, code_created = ? WHERE id = ?').run(code, Date.now(), user.id);
  try {
    await sendCode(user.username, user.email, code);
  } catch (e) {
    console.error('Email send failed:', e.message);
    if (IS_PROD) return res.status(500).json({ error: 'Email could not be sent' });
  }
  res.json({ ok: true, dev: mailDev });
});

/* --- Verify --- */
app.post('/api/verify', (req, res) => {
  const username = String(req.body?.username || '').trim();
  const code = parseCode(req.body?.code);
  const user = db.prepare('SELECT * FROM users WHERE username = ?').get(username);
  if (!user) return res.status(404).json({ error: 'Account not found' });
  if (user.verified) return res.status(400).json({ error: 'Already verified — just log in' });
  const exp = Date.now() - (user.code_created || 0) > 15 * 60 * 1000;
  if (!user.code || code !== user.code || exp) return res.status(400).json({ error: 'Wrong or expired code' });

  db.prepare('UPDATE users SET verified = 1, code = NULL WHERE id = ?').run(user.id);
  const token = newToken();
  db.prepare('INSERT INTO sessions (token, user_id, created_at) VALUES (?, ?, ?)').run(token, user.id, Date.now());
  res.json({ ok: true, token, user: publicUser(user) });
});

/* --- Login --- */
app.post('/api/login', (req, res) => {
  const username = String(req.body?.username || '').trim();
  const password = String(req.body?.password || '');
  const user = db.prepare('SELECT * FROM users WHERE username = ?').get(username);
  if (!user || !checkPass(password, user.pass_hash)) return res.status(401).json({ error: 'Wrong username or password' });
  if (!user.verified) return res.status(403).json({ error: 'Account not verified — check your email' });

  const token = newToken();
  db.prepare('INSERT INTO sessions (token, user_id, created_at) VALUES (?, ?, ?)').run(token, user.id, Date.now());
  res.json({ ok: true, token, user: publicUser(user) });
});

/* --- Forgot password --- */
app.post('/api/forgot-password', async (req, res) => {
  const email = String(req.body?.email || '').trim().toLowerCase();
  if (!EMAIL_RE.test(email)) return res.status(400).json({ error: 'Enter a valid email' });
  const user = db.prepare('SELECT * FROM users WHERE email = ?').get(email);
  if (user) {
    const code = newCode();
    db.prepare('UPDATE users SET reset_code = ?, reset_created = ? WHERE id = ?').run(code, Date.now(), user.id);
    try {
      await sendMessage(user.username, user.email, 'Geometry Extra — reset your password',
        'Hi ' + user.username + ',\n\nYour password reset code is ' + code + '.\nIt expires in 15 minutes.\n\n— Geometry Extra');
    } catch (e) {
      console.error('Email send failed:', e.message);
      if (IS_PROD) return res.status(500).json({ error: 'Email could not be sent' });
    }
  }
  res.json({ ok: true, dev: mailDev });
});

/* --- Reset password --- */
app.post('/api/reset-password', (req, res) => {
  const email = String(req.body?.email || '').trim().toLowerCase();
  const code = parseCode(req.body?.code);
  const newPass = String(req.body?.newPassword || '');
  if (!EMAIL_RE.test(email)) return res.status(400).json({ error: 'Enter a valid email' });
  const user = db.prepare('SELECT * FROM users WHERE email = ?').get(email);
  if (!user) return res.status(404).json({ error: 'Account not found' });
  const expired = Date.now() - (user.reset_created || 0) > 15 * 60 * 1000;
  if (!user.reset_code || code !== user.reset_code || expired) return res.status(400).json({ error: 'Wrong or expired code' });
  if (newPass.length < 4) return res.status(400).json({ error: 'Password too short' });
  db.prepare('UPDATE users SET pass_hash = ?, reset_code = NULL, reset_created = NULL WHERE id = ?').run(hashPass(newPass), user.id);
  db.prepare('DELETE FROM sessions WHERE user_id = ?').run(user.id);
  res.json({ ok: true });
});

/* --- Forgot username --- */
app.post('/api/forgot-username', async (req, res) => {
  const email = String(req.body?.email || '').trim().toLowerCase();
  if (!EMAIL_RE.test(email)) return res.status(400).json({ error: 'Enter a valid email' });
  const user = db.prepare('SELECT * FROM users WHERE email = ?').get(email);
  if (user) {
    try {
      await sendMessage(user.username, user.email, 'Geometry Extra — your username',
        'Hi,\n\nYour Geometry Extra username is: ' + user.username + '\n\n— Geometry Extra');
    } catch (e) {
      console.error('Email send failed:', e.message);
      if (IS_PROD) return res.status(500).json({ error: 'Email could not be sent' });
    }
  }
  res.json({ ok: true, dev: mailDev });
});

/* --- Logout --- */
app.post('/api/logout', auth, (req, res) => {
  db.prepare('DELETE FROM sessions WHERE token = ?').run(req.token);
  res.json({ ok: true });
});

/* --- Me --- */
app.get('/api/me', auth, (req, res) => {
  res.json({ user: req.user });
});

/* --- Profile --- */
function myProfile(req) {
  const u = db.prepare('SELECT username, email, role, created_at, verified FROM users WHERE id = ?').get(req.user.id);
  return {
    username: u.username,
    email: u.email,
    role: u.role,
    createdAt: u.created_at,
    verified: !!u.verified
  };
}
app.get('/api/profile', auth, (req, res) => {
  res.json({ profile: myProfile(req) });
});
app.post('/api/profile/username', auth, (req, res) => {
  const username = String(req.body?.username || '').trim();
  if (username.length < 2 || username.length > 16) return res.status(400).json({ error: 'Username 2–16 chars' });
  if (db.prepare('SELECT id FROM users WHERE username = ? AND id != ?').get(username, req.user.id)) return res.status(400).json({ error: 'Username taken' });
  const old = db.prepare('SELECT username FROM users WHERE id = ?').get(req.user.id);
  db.prepare('UPDATE users SET username = ? WHERE id = ?').run(username, req.user.id);
  if (old) db.prepare('UPDATE saved_accounts SET username = ? WHERE username = ?').run(username, old.username);
  res.json({ ok: true, profile: myProfile(req) });
});
app.post('/api/profile/email', auth, async (req, res) => {
  const email = String(req.body?.email || '').trim().toLowerCase();
  if (!EMAIL_RE.test(email)) return res.status(400).json({ error: 'Enter a valid email' });
  if (db.prepare('SELECT id FROM users WHERE email = ? AND id != ?').get(email, req.user.id)) return res.status(400).json({ error: 'Email already registered' });
  const code = newCode();
  db.prepare('UPDATE users SET pending_email = ?, pending_code = ?, pending_created = ? WHERE id = ?').run(email, code, Date.now(), req.user.id);
  try {
    await sendMessage(req.user.name, email, 'Geometry Extra — confirm your new email',
      'Hi ' + req.user.name + ',\n\nYour Geometry Extra email-change code is ' + code + '.\nIt expires in 15 minutes.\n\n— Geometry Extra');
  } catch (e) {
    console.error('Email send failed:', e.message);
    if (IS_PROD) return res.status(500).json({ error: 'Email could not be sent' });
  }
  res.json({ ok: true, dev: mailDev });
});
app.post('/api/profile/email/confirm', auth, (req, res) => {
  const code = parseCode(req.body?.code);
  const u = db.prepare('SELECT pending_email, pending_code, pending_created FROM users WHERE id = ?').get(req.user.id);
  if (!u || !u.pending_email || !u.pending_code || code !== u.pending_code) return res.status(400).json({ error: 'Wrong or expired code' });
  if (Date.now() - (u.pending_created || 0) > 15 * 60 * 1000) return res.status(400).json({ error: 'Code expired — request a new one' });
  db.prepare('UPDATE users SET email = ?, pending_email = NULL, pending_code = NULL, pending_created = NULL WHERE id = ?').run(u.pending_email, req.user.id);
  res.json({ ok: true, profile: myProfile(req) });
});
app.post('/api/profile/password', auth, (req, res) => {
  const current = String(req.body?.currentPassword || '');
  const next = String(req.body?.newPassword || '');
  if (next.length < 4) return res.status(400).json({ error: 'Password too short' });
  const u = db.prepare('SELECT pass_hash FROM users WHERE id = ?').get(req.user.id);
  if (!checkPass(current, u.pass_hash)) return res.status(401).json({ error: 'Current password is wrong' });
  db.prepare('UPDATE users SET pass_hash = ? WHERE id = ?').run(hashPass(next), req.user.id);
  db.prepare('DELETE FROM sessions WHERE user_id = ? AND token != ?').run(req.user.id, req.token);
  res.json({ ok: true });
});

/* --- Saved accounts (device keychain) --- */
function deviceOf(req) {
  return String(req.headers['x-device-id'] || '').trim().slice(0, 64) || 'unknown';
}
function accountsOf(device) {
  return db.prepare('SELECT id, username, role, token FROM saved_accounts WHERE device = ? ORDER BY created_at DESC').all(device);
}
app.get('/api/accounts', (req, res) => {
  res.json({ accounts: accountsOf(deviceOf(req)) });
});
app.post('/api/accounts', (req, res) => {
  const token = String(req.body?.token || '');
  const row = db.prepare(
    'SELECT u.username, u.role FROM sessions s JOIN users u ON u.id = s.user_id WHERE s.token = ?'
  ).get(token);
  if (!row) return res.status(400).json({ error: 'Invalid token' });
  const device = deviceOf(req);
  db.prepare(`INSERT INTO saved_accounts (device, token, username, role, created_at) VALUES (?, ?, ?, ?, ?)
              ON CONFLICT(device, username) DO UPDATE SET token = excluded.token, role = excluded.role`)
    .run(device, token, row.username, row.role, Date.now());
  res.json({ accounts: accountsOf(device) });
});
app.delete('/api/accounts/:id', (req, res) => {
  const device = deviceOf(req);
  const id = parseInt(req.params.id, 10);
  if (!Number.isFinite(id)) return res.status(400).json({ error: 'Bad id' });
  db.prepare('DELETE FROM saved_accounts WHERE id = ? AND device = ?').run(id, device);
  res.json({ accounts: accountsOf(device) });
});

/* --- Promote (role management) --- */
function isLoopbackRequest(req) {
  const ip = req.ip || req.socket.remoteAddress || '';
  return ip === '127.0.0.1' || ip === '::1' || ip === '::ffff:127.0.0.1';
}
app.post('/api/promote', auth, (req, res) => {
  const { username, role } = req.body || {};
  if (!username || (role !== 'player' && role !== 'mod' && role !== 'admin')) {
    return res.status(400).json({ error: 'usage: { username, role } where role is player|mod|admin' });
  }
  const allowed = req.user.role === 'admin' || (!IS_PROD && mailDev && isLoopbackRequest(req));
  if (!allowed) return res.status(403).json({ error: 'Admin only' });
  const u = db.prepare('SELECT id FROM users WHERE username = ?').get(username);
  if (!u) return res.status(404).json({ error: 'User not found' });
  db.prepare('UPDATE users SET role = ? WHERE id = ?').run(role, u.id);
  res.json({ ok: true, user: username, role: role });
});

/* --- Power actions (SecretOS, local machine only) --- */
app.post('/api/power-action', (req, res) => {
  if (IS_PROD) return res.status(403).json({ error: 'Power actions are disabled on this server' });
  const ip = req.ip || req.socket.remoteAddress || '';
  const loop = ip === '127.0.0.1' || ip === '::1' || ip === '::ffff:127.0.0.1';
  if (!loop) return res.status(403).json({ error: 'Loopback only' });
  const action = req.body && req.body.action;
  const map = { restart: '/r', shutdown: '/s', recovery: '/r /o' };
  if (!map[action]) return res.status(400).json({ error: 'Unsupported action' });
  exec('shutdown.exe ' + map[action] + ' /t 3 /c "SecretOS ' + action + '"', (err) => {
    if (err) return res.status(500).json({ error: err.message });
    res.json({ ok: true });
  });
});

app.use((err, req, res, next) => {
  console.error(err);
  res.status(500).json({ error: 'Server error' });
});

app.listen(PORT, () => {
  console.log('Geometry Extra server running at http://localhost:' + PORT);
  console.log('Game: http://localhost:' + PORT + '/geometry-dash.html');
  console.log(mailDev
    ? 'EMAIL STATUS: dev mode — codes are printed to this console'
    : 'EMAIL STATUS: real SMTP configured');
});

/* ---------------- Optional admin seed ---------------- */
if (process.env.ADMIN_PASSWORD) {
  try {
    const exists = db.prepare("SELECT id FROM users WHERE username = 'admin'").get();
    if (!exists) {
      db.prepare("INSERT INTO users (username, email, pass_hash, role, verified, created_at) VALUES ('admin', 'admin@localhost', ?, 'admin', 1, ?)")
        .run(hashPass(process.env.ADMIN_PASSWORD), Date.now());
      console.log('Seeded admin account (username: admin).');
    }
  } catch (e) {
    console.error('Admin seed failed:', e.message);
  }
}