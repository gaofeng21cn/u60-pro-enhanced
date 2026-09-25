// Exercise the real page module with a local DOM and asynchronous RPC replies.
const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const model = require('../web/factory/u60-web-model.js');

function element(tag, cls = '', text = '') {
  const e = {tag, cls, kids: [], handlers: {}, attrs: {}, _text: text, length: 1, open: false};
  e[0] = e;
  e.addClass = c => { e.cls += ' ' + c; return e; };
  e.toggleClass = () => e;
  e.text = v => { if (v === undefined) return e._text; e._text = String(v); return e; };
  e.append = (...children) => { e.kids.push(...children.filter(Boolean)); return e; };
  e.prepend = child => { e.kids.unshift(child); return e; };
  e.appendTo = parent => { parent.append(e); return e; };
  e.empty = () => { e.kids = []; e._text = ''; return e; };
  e.children = () => ({length: e.kids.length});
  e.each = fn => {if(e.length)fn.call(e);return e;};
  e.attr = (a, b) => { if (typeof a === 'object') Object.assign(e.attrs, a); else e.attrs[a] = b; return e; };
  e.prop = (a, b) => { e[a] = b; return e; };
  e.on = (names, fn) => { names.split(' ').forEach(name => { e.handlers[name] = fn; }); return e; };
  e.off = name => { delete e.handlers[name]; return e; };
  e.showModal = () => { e.open = true; };
  e.close = () => { e.open = false; if (e.handlers.close) e.handlers.close(); };
  e.scrollIntoView = e.focus = () => {};
  e.find = selector => {
    const part = selector.split(' ').pop();
    const found = walk(e).find(n => part[0] === '.' ? n.cls.split(' ').includes(part.slice(1)) : n.tag === part);
    return found || Object.assign(element('missing'), {length: 0});
  };
  return e;
}
function walk(e) { return [e, ...e.kids.flatMap(walk)]; }
function rootPage() {
  return element('div').append(element('header', 'u60-head').append(element('h1')),
    element('button', 'u60-refresh'), element('p', 'u60-sync'), element('p', 'u60-notice', '正在读取设备状态…'),
    element('main', 'u60-content'), element('dialog'));
}
function jq(p) {
  const then = p.then.bind(p);
  p.then = (...args) => jq(then(...args));
  p.always = fn => p.then(v => { fn(); return v; }, e => { fn(); throw e; });
  return p;
}
function deferred() {
  let resolve, reject;
  const promise = jq(new Promise((yes, no) => { resolve = yes; reject = no; }));
  const d = {resolve, reject: e => { reject(e); return d; }, promise: () => promise};
  return d;
}
const source = fs.readFileSync(require.resolve('../web/factory/u60-enhanced.js'), 'utf8');
const window = {}, windowEvents = element('window');
const config = {isLogin: true};
const location = {hash: '#u60_enhanced_clash'};
let root = rootPage(), session = 'session-a', page, pendingState = null, denied = false;
const requests = [], jobs = new Map();
const snapshot = {ok: true, data: {clash: {mode: 'rule', verdict: 'takeover'}}, sections: []};
const $ = arg => arg === window ? windowEvents : arg === '#u60-enhanced' ? root : typeof arg === 'string' && arg.startsWith('<') ? element(arg.slice(1, -1)) : arg;
$.Deferred = deferred;
$.ajax = options => {
  if (denied) return jq(Promise.resolve([{result: [6]}]));
  const request = JSON.parse(options.data)[0], method = request.params[2], args = request.params[3];
  const response = value => [{result: [0, value]}];
  if (method === 'start') {
    const job = JSON.parse(args.request); requests.push(job);
    const id = String(requests.length); jobs.set(id, job);
    return jq(Promise.resolve(response({ok: true, id})));
  }
  const job = jobs.get(args.id);
  if (job.action === 'state' && pendingState) return pendingState.promise().then(payload => response({ok: true, done: true, payload}));
  const payload = job.action === 'state' ? snapshot : job.action === 'web.clash.state' ? {ok: true, providers: [{name: 'example'}]} : {ok: true};
  return jq(Promise.resolve(response({ok: true, done: true, payload})));
};
const context = {window, location, document: {documentElement: {contains: e => e === root}, activeElement: {tagName: 'BODY', focus() {}}, hidden: false},
  Date, setTimeout: () => {}, addInterval: () => {},
  define: (deps, factory) => { page = factory($, {createRequest: (object, method, args) => ({params: [session, object, method, args]})}, config, model,
    {intervalFor: () => 20000, render: (tab, data, api) => element('div', 'u60-advanced', 'advanced state').append(element('button', '', '全局代理').on('click', () => api.open({label:'切换为全局代理',type:'action',action:'web.clash.mode',args:{mode:'global'},confirm:true,enabled:true})))}); }};
vm.createContext(context);
const flush = async () => { for (let i = 0; i < 8; i++) await new Promise(setImmediate); };
const load = () => { vm.runInContext(source, context); page.init(); };
const button = text => walk(root).find(n => n.tag === 'button' && n._text === text);

(async () => {
  load(); await flush();
  assert.strictEqual(root.find('h1').text(), '代理');
  assert(root.find('.u60-advanced').length, 'advanced state loads on the first visit');
  assert.strictEqual(root.find('.u60-notice').text(), '');

  // The mode button must open confirmation and send nothing until accepted.
  button('全局代理').handlers.click();
  assert(root.find('dialog')[0].open, 'mode selection opens confirmation');
  assert(!requests.some(r => r.action === 'web.clash.mode'));
  button('应用设置').handlers.click(); await flush();
  assert.deepStrictEqual(requests.find(r => r.action === 'web.clash.mode').args, {mode: 'global'});

  // A newly loaded AMD instance immediately reuses data for the same login.
  const advancedReads = requests.filter(r => r.action === 'web.clash.state').length;
  pendingState = deferred(); root = rootPage(); load();
  assert(root.find('.u60-advanced').length, 'navigation renders cached state before RPC finishes');
  assert.strictEqual(root.find('.u60-notice').text(), '', 'cache navigation must not retain the initial loading message');
  await flush(); pendingState.resolve(snapshot); pendingState = null; await flush();
  assert(root.find('.u60-sync').text().startsWith('已同步 '));
  // The preceding setting change invalidates advanced data exactly once.
  assert.strictEqual(requests.filter(r => r.action === 'web.clash.state').length, advancedReads + 1);
  root = rootPage(); load(); await flush();
  assert.strictEqual(requests.filter(r => r.action === 'web.clash.state').length, advancedReads + 1, 'fresh advanced cache avoids another read');

  // Navigation must consume the old job before starting a new one. The backend
  // has one job slot, shared by all AMD page instances.
  pendingState = deferred(); root.find('.u60-refresh').handlers.click(); await flush();
  const readsBeforeNavigation = requests.length;
  root = rootPage(); load(); await flush();
  assert.strictEqual(requests.length, readsBeforeNavigation, 'new page waits for the in-flight backend job');
  const oldRead = pendingState; pendingState = null; oldRead.resolve(snapshot); await flush();
  assert(requests.length > readsBeforeNavigation, 'current page resumes once the old result has been consumed');
  assert(root.find('.u60-sync').text().startsWith('已同步 '));

  // A new login must not render the prior login's cached values.
  session = 'session-b'; pendingState = deferred(); root = rootPage(); load();
  assert.strictEqual(root.find('.u60-content').kids.length, 0);
  assert.strictEqual(window.__u60EnhancedCache.snapshot, null);
  await flush(); pendingState.resolve(snapshot); pendingState = null; await flush();
  assert(root.find('.u60-advanced').length);

  denied = true; root.find('.u60-refresh').handlers.click(); await flush();
  assert.strictEqual(window.__u60EnhancedCache.snapshot, null);
  assert.strictEqual(root.find('.u60-content').kids.length, 0, 'server-side expiry clears the visible cached state');
  assert(root.find('.u60-notice').text().includes('会话无效'));

  config.isLogin = false; root = rootPage(); load(); await flush();
  assert.strictEqual(window.__u60EnhancedCache.snapshot, null);
  assert.strictEqual(root.find('.u60-content').kids.length, 0);
  assert(root.find('.u60-notice').text().includes('登录已失效'));
  console.log('web enhanced: passed; confirmed mode action, navigation cache, refresh, session change and logout');
})().catch(error => { console.error(error); process.exitCode = 1; });
