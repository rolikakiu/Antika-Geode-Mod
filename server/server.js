require('dotenv').config();
const express = require('express');
const cors = require('cors');
const crypto = require('crypto');
const { DatabaseSync } = require('node:sqlite');
const { exec } = require('child_process');
const nodemailer = require('nodemailer');
const OpenAI = require('openai');
const WebSocket = require('ws');

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

db.exec(`
CREATE TABLE IF NOT EXISTS game_saves (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  game TEXT NOT NULL DEFAULT 'text-adventure',
  slot TEXT NOT NULL DEFAULT 'default',
  state TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  UNIQUE(user_id, game, slot)
);
CREATE TABLE IF NOT EXISTS bot_conversations (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  title TEXT NOT NULL DEFAULT 'New chat',
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS bot_messages (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  conversation_id INTEGER NOT NULL,
  role TEXT NOT NULL,
  content TEXT NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS replays (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER,
  username TEXT NOT NULL,
  game TEXT NOT NULL,
  level TEXT NOT NULL DEFAULT '',
  duration_ms INTEGER NOT NULL DEFAULT 0,
  score REAL NOT NULL DEFAULT 0,
  complete INTEGER NOT NULL DEFAULT 0,
  samples TEXT NOT NULL DEFAULT '[]',
  created_at INTEGER NOT NULL
);
`);
db.exec('CREATE INDEX IF NOT EXISTS idx_replays_pick ON replays(game, level, complete DESC, score DESC)');
db.exec('CREATE INDEX IF NOT EXISTS idx_bot_conv_user ON bot_conversations(user_id, updated_at DESC)');
db.exec('CREATE INDEX IF NOT EXISTS idx_bot_msg_conv ON bot_messages(conversation_id, id ASC)');

db.exec(`
CREATE TABLE IF NOT EXISTS bans (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  username TEXT NOT NULL,
  reason TEXT NOT NULL DEFAULT '',
  banned_by TEXT NOT NULL,
  created_at INTEGER NOT NULL
);
`);


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

/* ---------------- OpenAI / OpenRouter (textwithbots) ---------------- */
let openai = null;
let provider = 'openai';
if (process.env.OPENAI_API_KEY) {
  const key = process.env.OPENAI_API_KEY.trim();
  if (key.startsWith('sk-or-v1-')) {
    provider = 'openrouter';
    openai = new OpenAI({
      apiKey: key,
      baseURL: 'https://openrouter.ai/api/v1',
      defaultHeaders: {
        'HTTP-Referer': process.env.SITE_URL || 'https://geometry-extra.onrender.com',
        'X-Title': process.env.APP_NAME || 'Geometry Extra'
      }
    });
  } else {
    openai = new OpenAI({ apiKey: key });
  }
}
const BOT_MODEL = process.env.OPENAI_MODEL || (provider === 'openrouter' ? 'openai/gpt-4o-mini' : 'gpt-4o-mini');
const BOT_SYSTEM_PROMPT =
  'You are a friendly, helpful chatbot called Echo. ' +
  'You chat with users casually and helpfully. ' +
  'Keep responses concise and conversational, like a text message. ' +
  'Use markdown sparingly for formatting.';
function botMessages(rows) {
  return rows.map(r => ({ role: r.role, content: r.content }));
}
async function askBot(history) {
  if (!openai) throw new Error('Bot is not configured on the server yet');
  const res = await openai.chat.completions.create({
    model: BOT_MODEL,
    messages: [{ role: 'system', content: BOT_SYSTEM_PROMPT }, ...history],
    temperature: 0.7,
    max_tokens: 1024
  });
  const text = res.choices?.[0]?.message?.content;
  if (!text) throw new Error('Bot returned an empty response');
  return text;
}

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
  const banned = db.prepare('SELECT id FROM bans WHERE user_id = ?').get(row.id);
  if (banned) return res.status(403).json({ error: 'Account is banned' });
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

/* --- Game saves --- */
app.get('/api/saves', auth, (req, res) => {
  const game = String(req.query.game || 'text-adventure');
  const rows = db.prepare('SELECT id, slot, state, created_at, updated_at FROM game_saves WHERE user_id = ? AND game = ? ORDER BY updated_at DESC')
    .all(req.user.id, game);
  res.json({ saves: rows.map(r => ({ id: r.id, slot: r.slot, state: JSON.parse(r.state), createdAt: r.created_at, updatedAt: r.updated_at })) });
});
app.post('/api/saves', auth, (req, res) => {
  const game = String(req.body?.game || 'text-adventure');
  const slot = String(req.body?.slot || 'default');
  const state = req.body?.state;
  if (!state || typeof state !== 'object') return res.status(400).json({ error: 'Invalid state' });
  const now = Date.now();
  const json = JSON.stringify(state);
  db.prepare(`INSERT INTO game_saves (user_id, game, slot, state, created_at, updated_at)
              VALUES (?, ?, ?, ?, ?, ?)
              ON CONFLICT(user_id, game, slot) DO UPDATE SET state = excluded.state, updated_at = excluded.updated_at`)
    .run(req.user.id, game, slot, json, now, now);
  res.json({ ok: true, updatedAt: now });
});
app.delete('/api/saves/:id', auth, (req, res) => {
  const id = parseInt(req.params.id, 10);
  if (!Number.isFinite(id)) return res.status(400).json({ error: 'Bad id' });
  db.prepare('DELETE FROM game_saves WHERE id = ? AND user_id = ?').run(id, req.user.id);
  res.json({ ok: true });
});

/* --- Textwithbots --- */
function convJSON(r) {
  return { id: r.id, title: r.title, createdAt: r.created_at, updatedAt: r.updated_at };
}
app.get('/api/bots/conversations', auth, (req, res) => {
  const rows = db.prepare('SELECT id, title, created_at, updated_at FROM bot_conversations WHERE user_id = ? ORDER BY updated_at DESC')
    .all(req.user.id);
  res.json({ conversations: rows.map(convJSON) });
});
app.post('/api/bots/conversations', auth, (req, res) => {
  const now = Date.now();
  const info = db.prepare('INSERT INTO bot_conversations (user_id, title, created_at, updated_at) VALUES (?, ?, ?, ?)')
    .run(req.user.id, 'New chat', now, now);
  const row = db.prepare('SELECT id, title, created_at, updated_at FROM bot_conversations WHERE id = ?').get(info.lastInsertRowid);
  res.json({ conversation: convJSON(row) });
});
app.delete('/api/bots/conversations/:id', auth, (req, res) => {
  const id = parseInt(req.params.id, 10);
  if (!Number.isFinite(id)) return res.status(400).json({ error: 'Bad id' });
  db.prepare('DELETE FROM bot_messages WHERE conversation_id = ?').run(id);
  db.prepare('DELETE FROM bot_conversations WHERE id = ? AND user_id = ?').run(id, req.user.id);
  res.json({ ok: true });
});
function getOwnedConversation(userId, id) {
  return db.prepare('SELECT id, title FROM bot_conversations WHERE id = ? AND user_id = ?').get(id, userId);
}
app.get('/api/bots/conversations/:id/messages', auth, (req, res) => {
  const id = parseInt(req.params.id, 10);
  if (!Number.isFinite(id)) return res.status(400).json({ error: 'Bad id' });
  const conv = getOwnedConversation(req.user.id, id);
  if (!conv) return res.status(404).json({ error: 'Conversation not found' });
  const rows = db.prepare('SELECT id, role, content, created_at FROM bot_messages WHERE conversation_id = ? ORDER BY id ASC').all(id);
  res.json({ conversation: { id: conv.id, title: conv.title }, messages: rows.map(r => ({ id: r.id, role: r.role, content: r.content, createdAt: r.created_at })) });
});
app.post('/api/bots/conversations/:id/messages', auth, async (req, res) => {
  const id = parseInt(req.params.id, 10);
  if (!Number.isFinite(id)) return res.status(400).json({ error: 'Bad id' });
  const conv = getOwnedConversation(req.user.id, id);
  if (!conv) return res.status(404).json({ error: 'Conversation not found' });
  const content = String(req.body?.content || '').trim();
  if (!content) return res.status(400).json({ error: 'Message is empty' });
  if (content.length > 4000) return res.status(400).json({ error: 'Message too long (4000 chars max)' });

  const now = Date.now();
  const userMsg = db.prepare('INSERT INTO bot_messages (conversation_id, role, content, created_at) VALUES (?, ?, ?, ?)')
    .run(id, 'user', content, now);

  const historyRows = db.prepare('SELECT role, content FROM bot_messages WHERE conversation_id = ? ORDER BY id ASC').all(id);
  let replyText;
  try {
    replyText = await askBot(botMessages(historyRows));
  } catch (e) {
    console.error('Bot error:', e.message);
    db.prepare('DELETE FROM bot_messages WHERE id = ?').run(userMsg.lastInsertRowid);
    return res.status(502).json({ error: 'The bot could not respond right now: ' + e.message });
  }

  const botMsg = db.prepare('INSERT INTO bot_messages (conversation_id, role, content, created_at) VALUES (?, ?, ?, ?)')
    .run(id, 'assistant', replyText, now);
  db.prepare('UPDATE bot_conversations SET updated_at = ? WHERE id = ?').run(now, id);

  let title = conv.title;
  if (title === 'New chat') {
    title = content.replace(/\s+/g, ' ').trim().slice(0, 40) + (content.length > 40 ? '…' : '');
    if (!title) title = 'New chat';
    db.prepare('UPDATE bot_conversations SET title = ? WHERE id = ?').run(title, id);
  }

  const userRow = db.prepare('SELECT id, role, content, created_at FROM bot_messages WHERE id = ?').get(userMsg.lastInsertRowid);
  const botRow = db.prepare('SELECT id, role, content, created_at FROM bot_messages WHERE id = ?').get(botMsg.lastInsertRowid);
  res.json({
    userMessage: { id: userRow.id, role: userRow.role, content: userRow.content, createdAt: userRow.created_at },
    botMessage: { id: botRow.id, role: botRow.role, content: botRow.content, createdAt: botRow.created_at },
    title
  });
});

/* --- Replays (multiplayer ghosts) --- */
function softUser(req) {
  const h = req.headers.authorization || '';
  const token = h.startsWith('Bearer ') ? h.slice(7) : null;
  if (!token) return { id: null, name: 'Anonymous' };
  const row = db.prepare('SELECT u.id, u.username FROM sessions s JOIN users u ON u.id = s.user_id WHERE s.token = ?').get(token);
  return row ? { id: row.id, name: row.username } : { id: null, name: 'Anonymous' };
}
function replayJSON(r, withSamples) {
  const j = { id: r.id, username: r.username, game: r.game, level: r.level, durationMs: r.duration_ms, score: r.score, complete: !!r.complete, createdAt: r.created_at };
  if (withSamples) j.samples = JSON.parse(r.samples);
  return j;
}
app.post('/api/replays', (req, res) => {
  const who = softUser(req);
  const game = String(req.body?.game || '').trim();
  const level = String(req.body?.level || '').trim();
  const samples = req.body?.samples;
  const durationMs = Math.max(0, Math.round(Number(req.body?.durationMs) || 0));
  const score = Math.max(0, Math.round(Number(req.body?.score) || 0));
  const complete = req.body?.complete ? 1 : 0;
  if (!game || game.length > 40) return res.status(400).json({ error: 'Bad game' });
  if (level.length > 40) return res.status(400).json({ error: 'Bad level' });
  if (!Array.isArray(samples) || samples.length < 2 || samples.length > 30000) return res.status(400).json({ error: 'Bad or empty samples' });
  const info = db.prepare('INSERT INTO replays (user_id, username, game, level, duration_ms, score, complete, samples, created_at) VALUES (?,?,?,?,?,?,?,?,?)')
    .run(who.id, who.name.slice(0, 16), game, level, durationMs, score, complete, JSON.stringify(samples), Date.now());
  wsBroadcastRoom(game, level, { type: 'record', game, level, username: who.name.slice(0, 16), score, complete: !!complete });
  res.json({ ok: true, id: Number(info.lastInsertRowid) });
});
app.get('/api/replays', (req, res) => {
  const game = String(req.query.game || '').trim();
  const level = String(req.query.level || '').trim();
  const limit = Math.max(1, Math.min(20, parseInt(req.query.limit, 10) || 5));
  if (!game || game.length > 40) return res.status(400).json({ error: 'Bad game' });
  if (level.length > 40) return res.status(400).json({ error: 'Bad level' });
  const rows = level
    ? db.prepare('SELECT id, username, game, level, duration_ms, score, complete, samples, created_at FROM replays WHERE game = ? AND level = ? ORDER BY complete DESC, score DESC, duration_ms ASC LIMIT ?').all(game, level, limit)
    : db.prepare('SELECT id, username, game, level, duration_ms, score, complete, samples, created_at FROM replays WHERE game = ? ORDER BY complete DESC, score DESC, duration_ms ASC LIMIT ?').all(game, limit);
  res.json({ replays: rows.map(r => replayJSON(r, true)) });
});
app.delete('/api/replays/:id', (req, res) => {
  const who = softUser(req);
  const id = parseInt(req.params.id, 10);
  if (!Number.isFinite(id)) return res.status(400).json({ error: 'Bad id' });
  const row = db.prepare('SELECT id, user_id FROM replays WHERE id = ?').get(id);
  if (!row) return res.status(404).json({ error: 'Not found' });
  if (who.id === null || (row.user_id !== null && row.user_id !== who.id)) return res.status(403).json({ error: 'Not your replay' });
  db.prepare('DELETE FROM replays WHERE id = ?').run(id);
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

/* --- Ban / Unban --- */
app.post('/api/ban', auth, (req, res) => {
  if (req.user.role !== 'admin') return res.status(403).json({ error: 'Admin only' });
  const { username, reason } = req.body || {};
  if (!username) return res.status(400).json({ error: 'username required' });
  const u = db.prepare('SELECT id FROM users WHERE username = ?').get(username);
  if (!u) return res.status(404).json({ error: 'User not found' });
  if (u.id === req.user.id) return res.status(400).json({ error: 'Cannot ban yourself' });
  const existing = db.prepare('SELECT id FROM bans WHERE user_id = ?').get(u.id);
  if (existing) return res.status(400).json({ error: 'User already banned' });
  db.prepare('INSERT INTO bans (user_id, username, reason, banned_by, created_at) VALUES (?, ?, ?, ?, ?)')
    .run(u.id, username, String(reason || ''), req.user.name, Date.now());
  db.prepare('DELETE FROM sessions WHERE user_id = ?').run(u.id);
  kickUserById(u.id);
  res.json({ ok: true, user: username });
});
app.post('/api/unban', auth, (req, res) => {
  if (req.user.role !== 'admin') return res.status(403).json({ error: 'Admin only' });
  const { username } = req.body || {};
  if (!username) return res.status(400).json({ error: 'username required' });
  const u = db.prepare('SELECT id FROM users WHERE username = ?').get(username);
  if (!u) return res.status(404).json({ error: 'User not found' });
  db.prepare('DELETE FROM bans WHERE user_id = ?').run(u.id);
  res.json({ ok: true, user: username });
});
app.get('/api/bans', auth, (req, res) => {
  if (req.user.role !== 'admin') return res.status(403).json({ error: 'Admin only' });
  const rows = db.prepare('SELECT id, username, reason, banned_by, created_at FROM bans ORDER BY created_at DESC').all();
  res.json({ bans: rows.map(r => ({ id: r.id, username: r.username, reason: r.reason, bannedBy: r.banned_by, createdAt: r.created_at })) });
});

/* --- Kick (disconnect WebSocket) --- */
const wsClients = new Map(); // user_id -> Set of WebSocket connections
app.post('/api/kick', auth, (req, res) => {
  if (req.user.role !== 'admin') return res.status(403).json({ error: 'Admin only' });
  const { username } = req.body || {};
  if (!username) return res.status(400).json({ error: 'username required' });
  const u = db.prepare('SELECT id FROM users WHERE username = ?').get(username);
  if (!u) return res.status(404).json({ error: 'User not found' });
  const kicked = kickUserById(u.id);
  res.json({ ok: true, user: username, kicked });
});
function kickUserById(userId) {
  const conns = wsClients.get(userId);
  if (!conns || conns.size === 0) return 0;
  let count = 0;
  for (const ws of conns) {
    try { wsSend(ws, { type: 'kicked', reason: 'Kicked by admin' }); } catch (e) {}
    try { ws.close(4001, 'Kicked by admin'); } catch (e) {}
    count++;
  }
  wsClients.delete(userId);
  return count;
}

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

const server = app.listen(PORT, () => {
  console.log('Geometry Extra server running at http://localhost:' + PORT);
  console.log('Game: http://localhost:' + PORT + '/geometry-dash.html');
  console.log(mailDev
    ? 'EMAIL STATUS: dev mode — codes are printed to this console'
    : 'EMAIL STATUS: real SMTP configured');
});

/* ---------------- Multiplayer WebSocket (/ws) ---------------- */
const liveRooms = new Map(); // key "game|level" -> Map(client, state)

function wsSend(ws, obj) {
  if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}
function wsBroadcastRoom(game, level, obj) {
  const key = game + '|' + (level || '');
  const room = liveRooms.get(key);
  if (!room) return;
  for (const [ws, st] of room) wsSend(ws, obj);
}
function roomSnapshot(key) {
  const room = liveRooms.get(key);
  if (!room) return null;
  const players = [];
  let count = 0;
  for (const [, st] of room) {
    count++;
    if (st.x != null) players.push({ id: st.id, name: st.name, x: st.x, y: st.y, status: st.status, progress: st.progress });
  }
  return { type: 'room', count, players };
}
function pushRoom(key) {
  const snap = roomSnapshot(key);
  if (!snap) return;
  const room = liveRooms.get(key);
  const [game, level] = key.split('|');
  snap.game = game;
  snap.level = level;
  for (const ws of room.keys()) wsSend(ws, snap);
}
function liveRoomKey(st) {
  return st.game && st.level != null ? st.game + '|' + st.level : null;
}

const wss = new WebSocket.Server({ server, path: '/ws' });
wss.on('connection', (ws, req) => {
  const st = { id: crypto.randomBytes(3).toString('hex'), userId: null, game: null, level: null, name: 'Unknown', x: null, y: null, status: 'playing', progress: 0, lastSent: 0, lastJoined: 0 };
  ws.on('message', (buf) => {
    let msg;
    try { msg = JSON.parse(buf.toString()); } catch (e) { return; }
    if (msg.type === 'auth') {
      const token = String(msg.token || '');
      const row = db.prepare('SELECT u.id, u.username FROM sessions s JOIN users u ON u.id = s.user_id WHERE s.token = ?').get(token);
      if (row) {
        st.userId = row.id;
        st.name = row.username;
        if (!wsClients.has(row.id)) wsClients.set(row.id, new Set());
        wsClients.get(row.id).add(ws);
      }
      return;
    }
    if (msg.type === 'join') {
      const game = String(msg.game || '').trim();
      const level = (msg.level == null ? '' : String(msg.level)).trim();
      if (!game || game.length > 40 || level.length > 40) return;
      const oldKey = liveRoomKey(st);
      if (oldKey) {
        const oldRoom = liveRooms.get(oldKey);
        if (oldRoom) { oldRoom.delete(ws); if (oldRoom.size === 0) liveRooms.delete(oldKey); else pushRoom(oldKey); }
      }
      st.game = game;
      st.level = level;
      st.name = String(msg.name || st.name || 'Anonymous').slice(0, 16);
      st.x = null; st.y = null; st.status = 'playing'; st.progress = 0;
      const key = game + '|' + level;
      if (!liveRooms.has(key)) liveRooms.set(key, new Map());
      liveRooms.get(key).set(ws, st);
      wsSend(ws, { type: 'joined', game, level, yourName: st.name });
      pushRoom(key);
    } else if (msg.type === 'pos') {
      if (!st.game) return;
      const now = Date.now();
      if (now - st.lastSent < 120) return;
      st.lastSent = now;
      st.name = String(st.name || 'Anonymous').slice(0, 16);
      if (typeof msg.x === 'number' && typeof msg.y === 'number') { st.x = Math.round(msg.x * 10) / 10; st.y = Math.round(msg.y * 10) / 10; }
      st.status = ['playing', 'dead', 'won'].includes(msg.status) ? msg.status : 'playing';
      st.progress = Math.max(0, Math.min(100, Math.round(Number(msg.progress) || 0)));
      const key = liveRoomKey(st);
      if (key) wsBroadcastRoom(st.game, st.level, {
        type: 'pos', id: st.id, name: st.name, x: st.x, y: st.y, status: st.status, progress: st.progress
      });
    }
  });
  ws.on('close', () => {
    if (st.userId && wsClients.has(st.userId)) {
      wsClients.get(st.userId).delete(ws);
      if (wsClients.get(st.userId).size === 0) wsClients.delete(st.userId);
    }
    const key = liveRoomKey(st);
    if (!key) return;
    const room = liveRooms.get(key);
    if (!room) return;
    room.delete(ws);
    if (room.size === 0) liveRooms.delete(key);
    else pushRoom(key);
  });
  ws.on('error', () => {});
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