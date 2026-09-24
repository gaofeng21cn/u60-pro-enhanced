// Headless render of the enhanced Clash page: no browser, no network, stubbed API.
const assert = require('assert');
const model = require('../web/factory/u60-web-model.js');

function makeEl(tag, cls) {
  const e = { tag, cls: cls || '', kids: [], attrs: {}, handlers: {}, value: '', _text: '' };
  Object.defineProperty(e, 'length', { get: () => e.kids.length });
  e.append = function () { for (const child of arguments) if (child != null) e.kids.push(child); return e; };
  e.addClass = function (c) { e.cls = (e.cls + ' ' + c).trim(); return e; };
  e.toggleClass = function (c, on) { if (on !== false) e.addClass(c); return e; };
  e.text = function (v) { if (v === undefined) return e._text; e._text = String(v); return e; };
  e.val = function (v) { if (v === undefined) return e.value; e.value = v; return e; };
  e.attr = function (a, b) {
    if (typeof a === 'object') Object.assign(e.attrs, a);
    else if (b === undefined) return e.attrs[a];
    else e.attrs[a] = b;
    return e;
  };
  e.on = function (name, fn) { e.handlers[name] = fn; return e; };
  e.empty = function () { e.kids.length = 0; return e; };
  e.appendTo = function (parent) { parent.append(e); return e; };
  e.prepend = function (child) { e.kids.unshift(child); return e; };
  e.children = function () { return e; };
  e.each = function (fn) { e.kids.forEach((c, i) => fn.call(c, i, c)); return e; };
  e.find = function () { return makeEl('matches'); };
  e.prop = function () { return e; };
  e.filter = function () { return e; };
  return e;
}

const $ = (arg) => (typeof arg === 'string' && arg.startsWith('<') ? makeEl(arg.slice(1, -1)) : arg);
let advanced = null;
global.define = (deps, factory) => { advanced = factory($, model); };
require('../web/factory/u60-web-advanced.js');

function texts(node, out = []) {
  if (!node || typeof node !== 'object') return out;
  if (node._text) out.push(node._text);
  (node.kids || []).forEach((c) => texts(c, out));
  return out;
}

function findButton(node, label) {
  let hit = null;
  (function walk(n) {
    if (!n || typeof n !== 'object') return;
    if (n.tag === 'button' && n._text === label) hit = hit || n;
    (n.kids || []).forEach(walk);
  })(node);
  return hit;
}

const base = {
  revision: 'rev', mode: 'rule', online: true, core_online: true, takeover: true, profile: 'clash',
  active_group: '订阅 · 示例', active_path: ['选择节点', '订阅 · 示例'], active_node: '示例节点 A01',
  verdict: 'takeover', verdict_text: '规则代理已接管 IPv4 TCP 与 DNS', active_path_label: '默认代理路径',
  coverage_checked: true, coverage_tcp: true, coverage_dns: true,
  providers: [{
    name: '示例订阅', host: 'provider.invalid', interval: 3600, count: 30,
    updated: '2026-09-23T23:32:17Z',
    usage: { Total: 10995116277760, Upload: 228719654416, Download: 661701784348, Expire: 1900000000 },
  }],
  groups: [
    { name: '选择节点', type: 'Selector', selected: '订阅 · 示例', nodes: ['订阅 · 示例'], selectable_nodes: [] },
    { name: '订阅 · 示例', type: 'Selector', selected: '示例节点 A01', nodes: ['示例节点 A01', '示例节点 B02', 'DIRECT'], selectable_nodes: ['示例节点 A01', '示例节点 B02'] },
  ],
  destination_groups: ['选择节点'], rules: [], local_rules: [], connections: [], connection_count: 0,
};

const opened = [];
const api = { open: (item) => opened.push(item), call: () => Promise.resolve({}), status: () => {}, refresh: () => {} };

function subtabButton(label) {
  return findButton(advanced.render('clash', base, api), label);
}
const view = texts(advanced.render('clash', base, api)).join('\n');
for (const want of [
  'Mihomo 核心', '运行中',
  '当前结论', '规则代理已接管 IPv4 TCP 与 DNS',
  '转发规则', 'TCP 已生效', 'DNS 已生效',
  '分流模式', '规则分流',
  '默认代理路径', 'MATCH → 选择节点 → 订阅 · 示例 → 示例节点 A01',
  'UDP 443 被拒绝',
]) {
  assert(view.includes(want), `missing ${want} in\n${view}`);
}
assert(view.includes('全部节点（按订阅顺序）'), `node list should be the default subtab in\n${view}`);
subtabButton('订阅').handlers.click();
const subs = texts(advanced.render('clash', base, api)).join('\n');
for (const want of ['30 个节点', 'provider.invalid', '到期', '用量']) {
  assert(subs.includes(want), `missing ${want} in\n${subs}`);
}

const diagnose = findButton(advanced.render('clash', base, api), '代理覆盖自检');
assert(diagnose, 'diagnose button missing');
diagnose.handlers.click();
assert.deepStrictEqual(opened.pop(), { label: '代理覆盖自检', type: 'action', action: 'web.clash.diagnose', args: {}, confirm: false, enabled: true });

const direct = texts(advanced.render('clash', Object.assign({}, base, { verdict: 'direct', verdict_text: '未启用代理，当前直连', takeover: false, profile: 'direct', active_path: [], active_node: '', core_online: false, online: false }), api)).join('\n');
assert(direct.includes('未运行'), direct);
assert(direct.includes('未启用代理，当前直连'), direct);

const partial = texts(advanced.render('clash', Object.assign({}, base, { verdict: 'partial', verdict_text: '转发不完整：TCP 或 DNS 未生效', coverage_dns: false }), api)).join('\n');
assert(partial.includes('转发不完整'), partial);
assert(partial.includes('DNS 未生效'), partial);
assert(partial.includes('客户端可能直接暴露'), partial);

subtabButton('订阅').handlers.click();
const stale = texts(advanced.render('clash', Object.assign({}, base, {
  providers: [Object.assign({}, base.providers[0], { updated: '2020-01-01T00:00:00Z' })],
}), api)).join('\n');
assert(stale.includes('已超过两个更新周期'), stale);

subtabButton('状态与节点').handlers.click();
const withPrefs = Object.assign({}, base, {
  prefs: { favorites: ['示例节点 A01'], recents: ['示例节点 B02', '示例节点 A01'], delays: { '示例节点 A01': { ms: 82, at: 1 } } },
});
const prefView = texts(advanced.render('clash', withPrefs, api)).join('\n');
for (const want of ['收藏', '最近使用', '82 ms', '使用', '延迟']) {
  assert(prefView.includes(want), `missing ${want} in\n${prefView}`);
}
const fav = findButton(advanced.render('clash', withPrefs, api), '取消收藏');
assert(fav, 'favorite toggle missing for an already-favorited node');
fav.handlers.click();
assert.deepStrictEqual(opened.pop(), { label: '取消收藏 示例节点 A01', type: 'action', action: 'web.clash.favorite', args: { name: '示例节点 A01' }, confirm: false, enabled: true });

// Auto-select: offer enable per provider, or disable when a url-test group exists.
const enableAuto = findButton(advanced.render('clash', base, api), '对「示例订阅」启用');
assert(enableAuto, 'auto-select enable button missing');
enableAuto.handlers.click();
assert.deepStrictEqual(opened.pop(), { label: '启用自动选择', type: 'action', action: 'web.clash.autoselect', args: { group: '自动选择', provider: '示例订阅', enabled: true }, confirm: true, enabled: true });
const autoBase = Object.assign({}, base, { groups: base.groups.concat([{ name: '自动选择', type: 'URLTest', selected: '示例节点 A01', nodes: [], selectable_nodes: [] }]) });
const disableAuto = findButton(advanced.render('clash', autoBase, api), '关闭自动选择');
assert(disableAuto, 'auto-select disable button missing');
disableAuto.handlers.click();
assert.deepStrictEqual(opened.pop(), { label: '关闭自动选择', type: 'action', action: 'web.clash.autoselect', args: { group: '自动选择', enabled: false }, confirm: true, enabled: true });

assert.strictEqual(advanced.intervalFor('clash'), 20000, 'non-connections views keep the slow interval');
subtabButton('连接与诊断').handlers.click();
advanced.render('clash', base, api);
assert.strictEqual(advanced.intervalFor('clash'), 5000, 'connections view polls faster');
subtabButton('状态与节点').handlers.click();

console.log('web advanced: passed; verdicts, default node view, favorites/recents, auto-select, refresh cadence');
