// node test.mjs: the Worker against a fake Discord.
import worker from './src/index.js';

let posted = null;
globalThis.fetch = async (url, init) => {
  posted = { url, form: init.body };
  return new Response('{}', { status: 200 });
};
const recent = [];
const env = {
  DISCORD_WEBHOOK_URL: 'https://discord.example/api/webhooks/1/abc',
  RECENT: { put: async (key, value, opts) => { recent.push({ key, opts }); } },
};
const pending = [];
const ctx = { waitUntil(p) { pending.push(p); } };
const log = console.log;
console.log = () => {};
const report = 'PS5SX2 session report\nBuild: vk-285-112\n';
function req(headers, body = report, path = '/v1/logs', method = 'POST') {
  const h = { 'content-type': 'text/plain; charset=utf-8', 'x-ps5sx2-console': '0123456789abcdef', ...headers };
  if (body !== null) h['content-length'] = String(new TextEncoder().encode(body).length);
  return new Request('https://relay.example' + path, { method, headers: h, body: method === 'POST' ? body : undefined });
}
const results = [];
async function expect(name, r, status) {
  const res = await worker.fetch(r, env, ctx);
  results.push([name, res.status === status ? 'PASS' : 'FAIL ' + res.status + ' ' + (await res.text())]);
}
await expect('good report', req({ 'x-ps5sx2-build': 'vk-285-112', 'x-ps5sx2-end': 'crash+note',
  'x-ps5sx2-game': 'Black (USA).iso', 'x-ps5sx2-tester': '@everyone *bold*', 'cf-connecting-ip': '203.0.113.7' }), 201);
await Promise.all(pending);
{
  const m = recent.length === 1 ? recent[0].opts.metadata : {};
  results.push(['attempt recorded', m.s === 201 && m.con === '012345' && m.build === 'vk-285-112' && m.end === 'crash+note'
    && m.why === '' && recent[0].opts.expirationTtl === 7 * 24 * 3600 && / 201 /.test(recent[0].key) ? 'PASS' : 'FAIL ' + JSON.stringify(recent)]);
  results.push(['no address in the record', !JSON.stringify(recent).includes('203.0.113.7') ? 'PASS' : 'FAIL']);
}
const fields = Object.fromEntries([...posted.form.entries()].map(([k, v]) => [k, typeof v === 'string' ? v : v.name]));
const payload = JSON.parse(fields.payload_json);
results.push(['summary line', payload.content.includes('**crash+note**') && payload.content.includes('`Black (USA).iso`')
  && payload.content.includes('`@everyone *bold*`') && payload.allowed_mentions.parse.length === 0 ? 'PASS' : 'FAIL ' + payload.content]);
results.push(['file name', /^PS5SX2-vk-285-112-012345-\d{4}-\d\d-\d\d_\d{4}-crash\+note\.txt$/.test(fields['files[0]']) ? 'PASS' : 'FAIL ' + fields['files[0]']]);
await expect('url in game name stays text', req({ 'x-ps5sx2-game': 'https://phish.example/login`x' }), 201);
{
  const f2 = Object.fromEntries([...posted.form.entries()].map(([k, v]) => [k, typeof v === 'string' ? v : v.name]));
  const c2 = JSON.parse(f2.payload_json).content;
  results.push(['url in a code span', c2.includes("`https://phish.example/login'x`") ? 'PASS' : 'FAIL ' + c2]);
}
let hits = 0;
env.IP_LIMITER = { limit: async () => ({ success: ++hits <= 2 }) };
await expect('ip limit 1', req({ 'x-ps5sx2-console': '1111111111111111' }), 201);
await expect('ip limit 2', req({ 'x-ps5sx2-console': '2222222222222222' }), 201);
await expect('ip limit 3 (new id, same ip)', req({ 'x-ps5sx2-console': '3333333333333333' }), 429);
delete env.IP_LIMITER;
await expect('bad console id', req({ 'x-ps5sx2-console': 'nope' }), 400);
await Promise.all(pending);
{
  const m = recent[recent.length - 1].opts.metadata;
  results.push(['refusal recorded with its reason', m.s === 400 && m.why === 'bad console id' && m.con === 'nope' ? 'PASS' : 'FAIL ' + JSON.stringify(m)]);
}
await expect('not a report', req({}, 'hello'), 400);
await expect('wrong type', req({ 'content-type': 'application/zip' }), 415);
await expect('GET', req({}, null, '/v1/logs', 'GET'), 405);
await expect('other path', req({}, report, '/x'), 404);
globalThis.fetch = async () => new Response('slow', { status: 429 });
await expect('discord busy', req({}), 429);
globalThis.fetch = async () => { throw new Error('network down'); };
await expect('discord unreachable: a 500 the installer retries', req({}), 500);
await Promise.all(pending);
results.push(['every POST recorded', recent.length === 10 ? 'PASS' : 'FAIL ' + recent.length]);
{
  const before = recent.length;
  await worker.fetch(req({}, null, '/v1/logs', 'GET'), env, ctx);
  await worker.fetch(req({}, report, '/x'), env, ctx);
  results.push(['GET and other paths not recorded', recent.length === before ? 'PASS' : 'FAIL']);
}
env.RECENT = { put: async () => { throw new Error('kv limit'); } };
globalThis.fetch = async () => new Response('{}', { status: 200 });
await expect('a failing record write changes nothing', req({}), 201);
await Promise.all(pending.map((p) => p.catch(() => 'x')));
for (const [n, r] of results) log(r.padEnd(6), n);
process.exit(results.every(([, r]) => r === 'PASS') ? 0 : 1);
