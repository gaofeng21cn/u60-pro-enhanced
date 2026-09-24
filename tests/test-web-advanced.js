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
  providers: [{
    name: '示例订阅', host: 'provider.invalid', interval: 3600, count: 30,
    updated: '2026-09-23T23:32:17Z',
    usage: { Total: 10995116277760, Upload: 228719654416, Download: 661701784348, Expire: 1900000000 },
  }],
  groups: [
    { name: '选择节点', type: 'Selector', selected: '订阅 · 示例', nodes: ['订阅 · 示例'], selectable_nodes: [] },
    { name: '订阅 · 示例', type: 'Selector', selected: '示例节点 A01', nodes: ['示例节点 A01', 'DIRECT'], selectable_nodes: ['示例节点 A01'] },
  ],
  destination_groups: ['选择节点'], rules: [], local_rules: [], connections: [], connection_count: 0,
};

const opened = [];
const api = { open: (item) => opened.push(item), call: () => Promise.resolve({}), status: () => {}, refresh: () => {} };

const view = texts(advanced.render('clash', base, api)).join('\n');
for (const want of [
  'Mihomo 核心', '运行中',
  '上网接管', '已接管 IPv4 TCP 与 DNS',
  '分流模式', '规则分流',
  '流量路径', 'MATCH → 选择节点 → 订阅 · 示例 → 示例节点 A01',
  'UDP 443 被拒绝',
  '30 个节点', 'provider.invalid', '到期', '用量',
]) {
  assert(view.includes(want), `missing ${want} in\n${view}`);
}

advanced.render('clash', base, api);
const diagnose = findButton(advanced.render('clash', base, api), '代理覆盖自检');
assert(diagnose, 'diagnose button missing');
diagnose.handlers.click();
assert.deepStrictEqual(opened.pop(), { label: '代理覆盖自检', type: 'action', action: 'web.clash.diagnose', args: {}, confirm: false, enabled: true });

const direct = texts(advanced.render('clash', Object.assign({}, base, { takeover: false, profile: 'direct', active_path: [], active_node: '', core_online: false, online: false }), api)).join('\n');
assert(direct.includes('未运行'), direct);
assert(direct.includes('未接管 · 当前出口 直连'), direct);

const stale = texts(advanced.render('clash', Object.assign({}, base, {
  providers: [Object.assign({}, base.providers[0], { updated: '2020-01-01T00:00:00Z' })],
}), api)).join('\n');
assert(stale.includes('已超过两个更新周期'), stale);

console.log('web advanced: passed; coverage card, diagnose action, profile and subscription states');
