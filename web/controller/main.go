// Authenticated web-only controller. Called by panel-web through stdin.
// Credentials stay in device memory/config; no arbitrary shell or URL proxy API.
package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/netip"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"syscall"
	"time"

	"gopkg.in/yaml.v3"
)

type M = map[string]any
type Request struct {
	Action string `json:"action"`
	Args   M      `json:"args"`
}
type App struct {
	root, tsSock string
	apiBase      string
	http         *http.Client
	validate     func(string) error
	reload       func() error
	testAPI      func(string, string, any) (M, error)
}

func fail(s string) M    { return M{"ok": false, "message": s} }
func success(s string) M { return M{"ok": true, "message": s} }
func text(v any) string {
	if s, ok := v.(string); ok {
		return s
	}
	return ""
}
func obj(v any) M {
	if m, ok := v.(map[string]any); ok {
		return m
	}
	return M{}
}
func arr(v any) []any {
	if a, ok := v.([]any); ok {
		return a
	}
	return nil
}
func selectableNodes(proxies M, group M) []any {
	selectable := make([]any, 0, len(arr(group["all"])))
	for _, candidate := range arr(group["all"]) {
		name, ok := candidate.(string)
		if !ok {
			continue
		}
		if name == "DIRECT" || name == "REJECT" || name == "REJECT-DROP" || name == "PASS" || name == "PASS-RULE" || name == "COMPATIBLE" {
			continue
		}
		member := obj(proxies[name])
		if _, nested := member["all"]; nested {
			continue
		}
		selectable = append(selectable, name)
	}
	return selectable
}
func activeSelectorGroup(proxies M, root string) string {
	seen := map[string]bool{}
	for depth := 0; depth < 16 && root != ""; depth++ {
		if seen[root] {
			return ""
		}
		seen[root] = true
		group := obj(proxies[root])
		if !strings.EqualFold(text(group["type"]), "Selector") || group["all"] == nil {
			return ""
		}
		selected := text(group["now"])
		if selected == "" {
			return root
		}
		nested := obj(proxies[selected])
		if !strings.EqualFold(text(nested["type"]), "Selector") || nested["all"] == nil {
			return root
		}
		root = selected
	}
	return ""
}
func number(v any) int {
	switch n := v.(type) {
	case float64:
		return int(n)
	case int:
		return n
	case string:
		i, _ := strconv.Atoi(n)
		return i
	}
	return 0
}
func boolv(v any) bool    { b, _ := v.(bool); return b }
func sha(b []byte) string { h := sha256.Sum256(b); return hex.EncodeToString(h[:]) }
func named(n *yaml.Node, key string) *yaml.Node {
	if n == nil || n.Kind != yaml.MappingNode {
		return nil
	}
	for i := 0; i < len(n.Content)-1; i += 2 {
		if n.Content[i].Value == key {
			return n.Content[i+1]
		}
	}
	return nil
}
func nstr(n *yaml.Node, key string) string {
	v := named(n, key)
	if v == nil {
		return ""
	}
	return v.Value
}
func nset(n *yaml.Node, key string, value any) {
	v := &yaml.Node{}
	v.Encode(value)
	for i := 0; i < len(n.Content)-1; i += 2 {
		if n.Content[i].Value == key {
			n.Content[i+1] = v
			return
		}
	}
	n.Content = append(n.Content, &yaml.Node{Kind: yaml.ScalarNode, Tag: "!!str", Value: key}, v)
}
func ndel(n *yaml.Node, key string) {
	for i := 0; i < len(n.Content)-1; i += 2 {
		if n.Content[i].Value == key {
			n.Content = append(n.Content[:i], n.Content[i+2:]...)
			return
		}
	}
}
func stringsOf(n *yaml.Node) []string {
	var a []string
	if n != nil && n.Kind == yaml.SequenceNode {
		for _, v := range n.Content {
			if v.Kind == yaml.ScalarNode {
				a = append(a, v.Value)
			}
		}
	}
	return a
}
func has(a []string, s string) bool {
	for _, v := range a {
		if v == s {
			return true
		}
	}
	return false
}
func names(m M) []string {
	a := make([]string, 0, len(m))
	for n := range m {
		a = append(a, n)
	}
	sort.Strings(a)
	return a
}
func (a *App) config() ([]byte, *yaml.Node, error) {
	b, e := os.ReadFile(filepath.Join(a.root, "config.yaml"))
	if e != nil || len(b) > 2<<20 {
		return nil, nil, errors.New("无法读取当前配置")
	}
	d := &yaml.Node{}
	dec := yaml.NewDecoder(bytes.NewReader(b))
	if dec.Decode(d) != nil || len(d.Content) != 1 || d.Content[0].Kind != yaml.MappingNode {
		return nil, nil, errors.New("当前配置格式无效")
	}
	var more yaml.Node
	if dec.Decode(&more) != io.EOF {
		return nil, nil, errors.New("不支持多文档配置")
	}
	return b, d.Content[0], nil
}
func (a *App) api(method, path string, data any) (M, error) {
	if a.testAPI != nil {
		return a.testAPI(method, path, data)
	}
	_, root, e := a.config()
	if e != nil {
		return nil, e
	}
	secret := nstr(root, "secret")
	var body io.Reader
	if data != nil {
		b, _ := json.Marshal(data)
		body = bytes.NewReader(b)
	}
	base := a.apiBase
	if base == "" {
		base = "http://127.0.0.1:19090"
	}
	req, e := http.NewRequest(method, base+path, body)
	if e != nil {
		return nil, errors.New("请求格式无效")
	}
	req.Header.Set("Authorization", "Bearer "+secret)
	req.Header.Set("Content-Type", "application/json")
	r, e := a.http.Do(req)
	if e != nil {
		return nil, errors.New("Clash 接口暂不可用")
	}
	defer r.Body.Close()
	if r.StatusCode < 200 || r.StatusCode >= 300 {
		return nil, errors.New("Clash 拒绝请求")
	}
	b, e := io.ReadAll(io.LimitReader(r.Body, 4<<20))
	if e != nil || len(b) >= 4<<20 {
		return nil, errors.New("Clash 响应过大")
	}
	if len(b) == 0 {
		return M{}, nil
	}
	var out M
	if json.Unmarshal(b, &out) != nil {
		return nil, errors.New("Clash 响应无效")
	}
	return out, nil
}
func (a *App) ts(method, path string, data any) (M, error) {
	tr := &http.Transport{DialContext: func(ctx context.Context, _, _ string) (net.Conn, error) {
		var d net.Dialer
		return d.DialContext(ctx, "unix", a.tsSock)
	}}
	defer tr.CloseIdleConnections()
	c := &http.Client{Transport: tr, Timeout: 8 * time.Second}
	var body io.Reader
	if data != nil {
		b, _ := json.Marshal(data)
		body = bytes.NewReader(b)
	}
	r, e := http.NewRequest(method, "http://local-tailscaled.sock/localapi/v0/"+path, body)
	if e != nil {
		return nil, e
	}
	r.Header.Set("Content-Type", "application/json")
	res, e := c.Do(r)
	if e != nil {
		return nil, errors.New("Tailscale 本地接口暂不可用")
	}
	defer res.Body.Close()
	if res.StatusCode != 200 {
		return nil, errors.New("Tailscale 拒绝请求")
	}
	b, e := io.ReadAll(io.LimitReader(res.Body, 2<<20))
	if e != nil || len(b) >= 2<<20 {
		return nil, errors.New("Tailscale 响应过大")
	}
	var result M
	if json.Unmarshal(b, &result) != nil {
		return nil, errors.New("Tailscale 响应无效")
	}
	return result, nil
}
func (a *App) clashState() M {
	b, root, e := a.config()
	if e != nil {
		return fail(e.Error())
	}
	out := success("")
	out["revision"] = sha(b)
	pp := named(root, "proxy-providers")
	runtime, _ := a.api("GET", "/providers/proxies", nil)
	live := obj(runtime["providers"])
	providers := []any{}
	if pp != nil {
		for i := 0; i < len(pp.Content)-1; i += 2 {
			n := pp.Content[i].Value
			p := pp.Content[i+1]
			if nstr(p, "type") != "http" {
				continue
			}
			u, _ := url.Parse(nstr(p, "url"))
			host := ""
			if u != nil {
				host = u.Hostname()
			}
			pLive := obj(live[n])
			providers = append(providers, M{"name": n, "host": host, "interval": number(nstr(p, "interval")), "count": len(arr(pLive["proxies"])), "updated": pLive["updatedAt"], "usage": pLive["subscriptionInfo"], "download_proxy": nstr(p, "proxy")})
		}
	}
	out["providers"] = providers
	groups := []any{}
	groupNames := []string{}
	all, err := a.api("GET", "/proxies", nil)
	out["online"] = err == nil
	proxies := obj(all["proxies"])
	for _, n := range names(proxies) {
		p := obj(proxies[n])
		if _, ok := p["all"]; ok {
			groups = append(groups, M{"name": n, "type": p["type"], "selected": p["now"], "nodes": p["all"], "selectable_nodes": selectableNodes(proxies, p)})
		}
	}
	// Prefer the selector currently reached by the active rule/global root.
	// This keeps nested subscription selectors out of the initial leaf list.
	activeGroup := ""
	if cfg, e := a.api("GET", "/configs", nil); e == nil {
		root := ""
		if text(cfg["mode"]) == "global" {
			root = "GLOBAL"
		} else if text(cfg["mode"]) == "rule" {
			if liveRules, e := a.api("GET", "/rules", nil); e == nil {
				for _, rule := range arr(liveRules["rules"]) {
					entry := obj(rule)
					if strings.EqualFold(text(entry["type"]), "Match") {
						root = text(entry["proxy"])
						break
					}
				}
			}
		}
		activeGroup = activeSelectorGroup(proxies, root)
	}
	out["active_group"] = activeGroup
	gn := named(root, "proxy-groups")
	if gn != nil {
		for _, g := range gn.Content {
			if nstr(g, "type") == "select" {
				groupNames = append(groupNames, nstr(g, "name"))
			}
		}
	}
	out["groups"] = groups
	out["destination_groups"] = groupNames
	rules, _ := a.api("GET", "/rules", nil)
	out["rules"] = rules["rules"]
	out["local_rules"] = localRules(b)
	cs, _ := a.api("GET", "/connections", nil)
	connections := []any{}
	allCon := arr(cs["connections"])
	for i, v := range allCon {
		if i >= 200 {
			break
		}
		c := obj(v)
		md := obj(c["metadata"])
		connections = append(connections, M{"id": c["id"], "host": md["host"], "source": md["sourceIP"], "destination": md["destinationIP"], "port": md["destinationPort"], "network": md["network"], "chains": c["chains"], "rule": c["rule"], "rule_payload": c["rulePayload"], "download": c["download"], "upload": c["upload"]})
	}
	out["connections"] = connections
	out["connection_count"] = len(allCon)
	return out
}
func (a *App) selectProxy(args M) M {
	group, node := text(args["group"]), text(args["name"])
	if group == "" || node == "" || len(group) > 512 || len(node) > 512 {
		return fail("请选择有效的策略组和节点")
	}
	path := "/proxies/" + url.PathEscape(group)
	before, err := a.api("GET", path, nil)
	if err != nil {
		return fail(err.Error())
	}
	if !strings.EqualFold(text(before["type"]), "Selector") {
		return fail("目标不是可手动切换的策略组")
	}
	found := false
	for _, candidate := range arr(before["all"]) {
		if text(candidate) == node {
			found = true
			break
		}
	}
	if !found {
		return fail("节点已变化，请刷新后重试")
	}
	if _, err = a.api("PUT", path, M{"name": node}); err != nil {
		return fail(err.Error())
	}
	after, err := a.api("GET", path, nil)
	if err != nil || text(after["now"]) != node {
		return fail("切换结果未回读确认，请刷新状态后核对")
	}
	return success("节点已切换并回读确认")
}
func localRules(b []byte) []any {
	out := []any{}
	active := false
	for _, line := range strings.Split(string(b), "\n") {
		if line == "  # U60-PANEL-RULES-BEGIN" {
			active = true
			continue
		}
		if line == "  # U60-PANEL-RULES-END" {
			break
		}
		if !active {
			continue
		}
		on := strings.HasPrefix(line, "  - ")
		off := strings.HasPrefix(line, "  # off - ")
		if on || off {
			prefix := 4
			if off {
				prefix = 10
			}
			out = append(out, M{"index": len(out), "rule": line[prefix:], "enabled": on})
		}
	}
	return out
}

// Replace only the selected top-level blocks, preserving every other byte,
// including proxy credentials, controller secret and screen rule comments.
func replaceBlocks(old []byte, root *yaml.Node, keys []string) ([]byte, error) {
	lines := bytes.SplitAfter(old, []byte("\n"))
	type change struct {
		start, end int
		data       []byte
	}
	changes := []change{}
	for _, key := range keys {
		v := named(root, key)
		if v == nil {
			return nil, errors.New("配置缺少 " + key)
		}
		start := -1
		end := len(lines)
		for i := 0; i < len(root.Content)-1; i += 2 {
			if root.Content[i].Value == key {
				start = root.Content[i].Line - 1
				if i+2 < len(root.Content) {
					end = root.Content[i+2].Line - 1
				}
				break
			}
		}
		wrapper := &yaml.Node{Kind: yaml.MappingNode, Tag: "!!map", Content: []*yaml.Node{{Kind: yaml.ScalarNode, Tag: "!!str", Value: key}, v}}
		var buf bytes.Buffer
		enc := yaml.NewEncoder(&buf)
		enc.SetIndent(2)
		if enc.Encode(wrapper) != nil {
			return nil, errors.New("配置生成失败")
		}
		enc.Close()
		if start < 0 {
			return nil, errors.New("缺少可替换的配置段")
		}
		changes = append(changes, change{start, end, buf.Bytes()})
	}
	sort.Slice(changes, func(i, j int) bool { return changes[i].start > changes[j].start })
	out := append([]byte(nil), old...)
	for _, c := range changes {
		begin, end := 0, 0
		for i, line := range lines {
			if i < c.start {
				begin += len(line)
			}
			if i < c.end {
				end += len(line)
			}
		}
		next := make([]byte, 0, len(out)+len(c.data))
		next = append(next, out[:begin]...)
		next = append(next, c.data...)
		next = append(next, out[end:]...)
		out = next
	}
	return out, nil
}
func atomicWrite(path string, b []byte, mode os.FileMode) error {
	f, e := os.CreateTemp(filepath.Dir(path), ".u60-web-*")
	if e != nil {
		return e
	}
	tmp := f.Name()
	defer os.Remove(tmp)
	if e = f.Chmod(mode); e == nil {
		_, e = f.Write(b)
	}
	if e == nil {
		e = f.Sync()
	}
	ce := f.Close()
	if e == nil {
		e = ce
	}
	if e == nil {
		e = os.Rename(tmp, path)
	}
	return e
}
func (a *App) apply(old, next []byte, verify func() error) error {
	current, e := os.ReadFile(filepath.Join(a.root, "config.yaml"))
	if e != nil || !bytes.Equal(current, old) {
		return errors.New("配置已变化，请刷新后重试")
	}
	stage := filepath.Join(a.root, ".web-check.yaml")
	if e = atomicWrite(stage, next, 0600); e != nil {
		return errors.New("无法写入候选配置")
	}
	defer os.Remove(stage)
	if e = a.validate(stage); e != nil {
		return errors.New("候选配置未通过 Clash 校验，原配置未改动")
	}
	backupDir := filepath.Join(a.root, "web-backups")
	if e = os.MkdirAll(backupDir, 0700); e != nil {
		return errors.New("无法创建回退目录")
	}
	os.Chmod(backupDir, 0700)
	backup := filepath.Join(backupDir, time.Now().UTC().Format("20060102T150405.000000000")+".yaml")
	if e = atomicWrite(backup, old, 0600); e != nil {
		return errors.New("无法保存回退版本")
	}
	before, _ := a.api("GET", "/proxies", nil)
	selections := map[string]string{}
	for name, p := range obj(before["proxies"]) {
		x := obj(p)
		if text(x["type"]) == "Selector" {
			selections[name] = text(x["now"])
		}
	}
	current, e = os.ReadFile(filepath.Join(a.root, "config.yaml"))
	if e != nil || !bytes.Equal(current, old) {
		return errors.New("校验期间配置已变化，请刷新后重试")
	}
	if atomicWrite(filepath.Join(a.root, "config.yaml"), next, 0600) != nil {
		return errors.New("配置写入失败")
	}
	restoreSelection := func() error {
		after, e := a.api("GET", "/proxies", nil)
		if e != nil {
			return e
		}
		for group, node := range selections {
			p := obj(obj(after["proxies"])[group])
			if len(p) == 0 {
				continue
			}
			found := false
			for _, v := range arr(p["all"]) {
				if text(v) == node {
					found = true
					break
				}
			}
			if found && text(p["now"]) != node {
				if _, e = a.api("PUT", "/proxies/"+url.PathEscape(group), M{"name": node}); e != nil {
					return e
				}
			}
		}
		return nil
	}
	e = a.reload()
	if e == nil && verify != nil {
		e = verify()
	}
	if e == nil {
		e = restoreSelection()
	}
	if e != nil {
		if atomicWrite(filepath.Join(a.root, "config.yaml"), old, 0600) != nil {
			return errors.New("热加载失败且回退写入失败，请立即检查设备")
		}
		if a.reload() != nil || restoreSelection() != nil {
			return errors.New("配置已恢复，但运行状态回退未确认")
		}
		return errors.New("订阅应用未通过验证，已恢复原配置和节点选择")
	}
	files, _ := filepath.Glob(filepath.Join(backupDir, "*.yaml"))
	sort.Strings(files)
	for len(files) > 5 {
		os.Remove(files[0])
		files = files[1:]
	}
	return nil
}
func validName(s string) bool {
	return len(s) > 0 && len(s) <= 80 && !strings.ContainsAny(s, "\r\n\x00") && s != "DIRECT" && s != "REJECT" && s != "GLOBAL"
}
func validURL(s string) bool {
	u, e := url.Parse(s)
	return e == nil && len(s) <= 4096 && (u.Scheme == "https" || u.Scheme == "http") && u.Hostname() != "" && u.User == nil && u.Fragment == "" && !strings.ContainsAny(s, "\r\n\x00")
}
func (a *App) saveSubscription(args M) M {
	old, root, e := a.config()
	if e != nil {
		return fail(e.Error())
	}
	if text(args["revision"]) != sha(old) {
		return fail("配置已变化，请刷新后重试")
	}
	name := text(args["name"])
	existing := text(args["existing"])
	if !validName(name) || existing != "" && existing != name {
		return fail("订阅名称无效；已有订阅名称不可更改")
	}
	pp := named(root, "proxy-providers")
	groups := named(root, "proxy-groups")
	if pp == nil || pp.Kind != yaml.MappingNode || groups == nil || groups.Kind != yaml.SequenceNode {
		return fail("当前配置缺少标准订阅或策略组结构")
	}
	p := named(pp, name)
	if existing == "" && p != nil {
		return fail("同名订阅已存在")
	}
	if existing != "" && (p == nil || nstr(p, "type") != "http") {
		return fail("原订阅已变化")
	}
	originalURL := nstr(p, "url")
	subURL := text(args["url"])
	if subURL == "" && p != nil {
		subURL = nstr(p, "url")
	}
	if !validURL(subURL) {
		return fail("请输入有效的 HTTP(S) Clash/Mihomo 订阅链接")
	}
	interval := number(args["interval"])
	if interval < 300 || interval > 604800 {
		return fail("更新间隔须为 300–604800 秒")
	}
	if p == nil {
		p = &yaml.Node{Kind: yaml.MappingNode, Tag: "!!map"}
		nset(pp, name, p)
		p = named(pp, name)
		nset(p, "type", "http")
		nset(p, "path", "./proxy_provider/web-"+sha([]byte(name))[:16]+".yaml")
		nset(p, "health-check", M{"enable": true, "url": "https://www.gstatic.com/generate_204", "interval": 600, "lazy": true})
	}
	changedURL := originalURL != subURL
	if changedURL {
		nset(p, "path", "./proxy_provider/web-"+sha([]byte(name + "\x00" + subURL))[:24]+".yaml")
	}
	nset(p, "url", subURL)
	nset(p, "interval", interval)
	if existing == "" {
		destination := text(args["destination"])
		var dest *yaml.Node
		for _, g := range groups.Content {
			if nstr(g, "name") == destination && nstr(g, "type") == "select" {
				dest = g
			}
		}
		if dest == nil {
			return fail("请选择现有的手动策略组")
		}
		groupName := "订阅 · " + name
		for _, g := range groups.Content {
			if nstr(g, "name") == groupName {
				return fail("订阅策略组名称冲突")
			}
		}
		g := &yaml.Node{}
		g.Encode(M{"name": groupName, "type": "select", "use": []string{name}})
		groups.Content = append(groups.Content, g)
		edges := stringsOf(named(dest, "proxies"))
		if !has(edges, groupName) {
			edges = append(edges, groupName)
		}
		nset(dest, "proxies", edges)
		for _, g := range groups.Content {
			if nstr(g, "name") == "GLOBAL" && g != dest {
				edges = stringsOf(named(g, "proxies"))
				if !has(edges, groupName) {
					nset(g, "proxies", append(edges, groupName))
				}
			}
		}
	}
	next, e := replaceBlocks(old, root, []string{"proxy-providers", "proxy-groups"})
	if e != nil {
		return fail(e.Error())
	}
	verify := func() error {
		if changedURL {
			if _, err := a.api("PUT", "/providers/proxies/"+url.PathEscape(name), nil); err != nil {
				return err
			}
		}
		until := time.Now().Add(12 * time.Second)
		for {
			p, e := a.api("GET", "/providers/proxies/"+url.PathEscape(name), nil)
			if e == nil && len(arr(p["proxies"])) > 0 {
				return nil
			}
			if time.Now().After(until) {
				return errors.New("订阅未加载节点")
			}
			time.Sleep(300 * time.Millisecond)
		}
	}
	if e = a.apply(old, next, verify); e != nil {
		return fail(e.Error())
	}
	if existing != "" {
		return success("订阅已保存并校验；节点名称若随订阅变化，请核对策略组选择")
	}
	return success("订阅已保存并校验，原节点选择保持不变；可在策略组中自行切换")
}
func (a *App) deleteSubscription(args M) M {
	old, root, e := a.config()
	if e != nil {
		return fail(e.Error())
	}
	if text(args["revision"]) != sha(old) {
		return fail("配置已变化，请刷新后重试")
	}
	name := text(args["name"])
	pp := named(root, "proxy-providers")
	if pp == nil || named(pp, name) == nil {
		return fail("订阅不存在")
	}
	runtime, e := a.api("GET", "/proxies", nil)
	if e != nil {
		return fail("需要 Clash 运行时确认当前节点，未删除")
	}
	provider, e := a.api("GET", "/providers/proxies/"+url.PathEscape(name), nil)
	if e != nil {
		return fail("无法确认订阅节点，未删除")
	}
	leaves := map[string]bool{}
	for _, v := range arr(provider["proxies"]) {
		leaves[text(obj(v)["name"])] = true
	}
	groups := named(root, "proxy-groups")
	if groups == nil {
		return fail("策略组结构无效")
	}
	removed := map[string]bool{}
	for _, g := range groups.Content {
		use := stringsOf(named(g, "use"))
		filtered := []string{}
		for _, v := range use {
			if v != name {
				filtered = append(filtered, v)
			}
		}
		if len(filtered) != len(use) {
			nset(g, "use", filtered)
			if len(filtered) == 0 && len(stringsOf(named(g, "proxies"))) == 0 {
				removed[nstr(g, "name")] = true
			}
		}
	}
	for groupName, v := range obj(runtime["proxies"]) {
		selected := text(obj(v)["now"])
		if removed[selected] || (leaves[selected] && !removed[groupName]) {
			return fail("该订阅仍被策略组选中，请先切换到其他节点")
		}
	}
	for _, rule := range stringsOf(named(root, "rules")) {
		for n := range removed {
			parts := strings.Split(rule, ",")
			for _, part := range parts[1:] {
				if part == n {
					return fail("分流规则仍引用此订阅策略组，请先调整规则")
				}
			}
		}
	}
	kept := []*yaml.Node{}
	for _, g := range groups.Content {
		if removed[nstr(g, "name")] {
			continue
		}
		edges := stringsOf(named(g, "proxies"))
		filtered := []string{}
		for _, v := range edges {
			if !removed[v] {
				filtered = append(filtered, v)
			}
		}
		if len(filtered) != len(edges) {
			nset(g, "proxies", filtered)
		}
		kept = append(kept, g)
	}
	groups.Content = kept
	ndel(pp, name)
	next, e := replaceBlocks(old, root, []string{"proxy-providers", "proxy-groups"})
	if e != nil {
		return fail(e.Error())
	}
	if e = a.apply(old, next, nil); e != nil {
		return fail(e.Error())
	}
	return success("订阅已删除，配置已验证；最近的原配置保留在设备本机")
}
func (a *App) tailscaleState() M {
	st, e := a.ts("GET", "status", nil)
	if e != nil {
		return fail(e.Error())
	}
	prefs, e := a.ts("GET", "prefs", nil)
	if e != nil {
		return fail(e.Error())
	}
	out := success("")
	out["status"] = st["BackendState"]
	out["version"] = st["Version"]
	out["health"] = st["Health"]
	out["ips"] = st["TailscaleIPs"]
	self := obj(st["Self"])
	out["hostname"] = self["HostName"]
	out["dns_name"] = self["DNSName"]
	out["key_expiry"] = self["KeyExpiry"]
	out["tailnet"] = obj(st["CurrentTailnet"])["Name"]
	filtered := M{}
	for _, k := range []string{"Hostname", "RouteAll", "CorpDNS", "ShieldsUp", "WantRunning", "AdvertiseRoutes", "ExitNodeID", "ExitNodeAllowLANAccess"} {
		filtered[k] = prefs[k]
	}
	out["prefs"] = filtered
	peers := []any{}
	pmap := obj(st["Peer"])
	for _, k := range names(pmap) {
		p := obj(pmap[k])
		peers = append(peers, M{"name": p["HostName"], "dns_name": p["DNSName"], "ips": p["TailscaleIPs"], "os": p["OS"], "online": p["Online"], "active": p["Active"], "exit_node": p["ExitNodeOption"], "last_seen": p["LastSeen"], "relay": p["Relay"], "direct": text(p["CurAddr"]) != "", "rx": p["RxBytes"], "tx": p["TxBytes"], "routes": p["PrimaryRoutes"]})
	}
	out["peers"] = peers
	return out
}
func parseRoutes(s string) ([]string, error) {
	result := []string{}
	for _, v := range strings.FieldsFunc(s, func(r rune) bool { return r == ',' || r == '\n' || r == '\r' || r == ' ' }) {
		p, e := netip.ParsePrefix(v)
		if e != nil || p.Bits() == 0 || p.Addr().IsLoopback() || p.Addr().IsMulticast() || p.Addr().IsUnspecified() {
			return nil, errors.New("请填写合法的内网 CIDR；出口节点请使用专用开关")
		}
		canonical := p.Masked().String()
		if !has(result, canonical) {
			result = append(result, canonical)
		}
	}
	if len(result) > 16 {
		return nil, errors.New("最多发布 16 个子网")
	}
	sort.Strings(result)
	return result, nil
}
func (a *App) tailscaleSet(action string, args M) M {
	prefs, e := a.ts("GET", "prefs", nil)
	if e != nil {
		return fail(e.Error())
	}
	patch, restore := M{}, M{}
	keys := []string{}
	if action == "web.tailscale.hostname" {
		name := text(args["hostname"])
		if len(name) < 1 || len(name) > 63 || strings.HasPrefix(name, "-") || strings.HasSuffix(name, "-") {
			return fail("主机名须为 1–63 位字母、数字或连字符")
		}
		for _, r := range name {
			if !(r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '-') {
				return fail("主机名包含不支持的字符")
			}
		}
		patch["Hostname"] = name
		keys = append(keys, "Hostname")
	} else if action == "web.tailscale.routes" {
		if _, e = os.Stat("/sys/class/net/tailscale0"); e != nil {
			return fail("请先使用 TUN 转发模式")
		}
		routes, e := parseRoutes(text(args["routes"]))
		if e != nil {
			return fail(e.Error())
		}
		for _, v := range arr(prefs["AdvertiseRoutes"]) {
			if text(v) == "0.0.0.0/0" || text(v) == "::/0" {
				routes = append(routes, text(v))
			}
		}
		patch["AdvertiseRoutes"] = routes
		keys = append(keys, "AdvertiseRoutes")
	} else {
		return fail("不支持的偏好修改")
	}
	for _, k := range keys {
		patch[k+"Set"] = true
		restore[k] = prefs[k]
		restore[k+"Set"] = true
	}
	if _, e = a.ts("PATCH", "prefs", patch); e != nil {
		return fail(e.Error())
	}
	after, e := a.ts("GET", "prefs", nil)
	verified := e == nil
	for _, k := range keys {
		want, _ := json.Marshal(patch[k])
		got, _ := json.Marshal(after[k])
		if !bytes.Equal(want, got) {
			verified = false
		}
	}
	if !verified {
		_, e = a.ts("PATCH", "prefs", restore)
		if e != nil {
			return fail("偏好回读不一致，回退未确认，请检查组网")
		}
		return fail("偏好回读不一致，已请求恢复原设置")
	}
	return success("Tailscale 设置已保存并回读；新增子网仍需管理后台批准和访问规则允许")
}
func (a *App) dispatch(r Request) M {
	switch r.Action {
	case "web.clash.state":
		return a.clashState()
	case "web.clash.select":
		return a.selectProxy(r.Args)
	case "web.clash.subscription_save":
		return a.saveSubscription(r.Args)
	case "web.clash.subscription_delete":
		return a.deleteSubscription(r.Args)
	case "web.clash.connection_close":
		id := text(r.Args["id"])
		if len(id) != 36 {
			return fail("连接 ID 无效")
		}
		for _, c := range id {
			if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f' || c == '-') {
				return fail("连接 ID 无效")
			}
		}
		_, e := a.api("DELETE", "/connections/"+id, nil)
		if e != nil {
			return fail(e.Error())
		}
		return success("该连接已关闭")
	case "web.tailscale.state":
		return a.tailscaleState()
	case "web.tailscale.hostname", "web.tailscale.routes":
		return a.tailscaleSet(r.Action, r.Args)
	case "web.tailscale.netcheck":
		ctx, cancel := context.WithTimeout(context.Background(), 25*time.Second)
		defer cancel()
		c := exec.CommandContext(ctx, "/data/tailscale/bin/tailscale", "--socket=/tmp/tailscale/tailscaled.sock", "netcheck", "--format=json")
		b, e := c.Output()
		if e != nil || len(b) > 65536 {
			return fail("Tailscale 网络诊断未完成")
		}
		var d M
		if json.Unmarshal(b, &d) != nil {
			return fail("网络诊断响应无法解析")
		}
		out := success("网络诊断完成")
		lines := []string{}
		for _, k := range []string{"UDP", "IPv4", "IPv6", "MappingVariesByDestIP", "HairPinning", "UPnP", "PMP", "PCP", "PreferredDERP"} {
			if v, ok := d[k]; ok {
				lines = append(lines, fmt.Sprintf("%s: %v", k, v))
			}
		}
		out["report"] = M{"title": "Tailscale 网络诊断", "lines": lines}
		return out
	}
	return fail("不支持的网页操作")
}
func main() {
	result := func() M {
		if len(os.Args) != 1 {
			return fail("仅支持标准输入请求")
		}
		b, e := io.ReadAll(io.LimitReader(os.Stdin, 16385))
		if e != nil || len(b) > 16384 {
			return fail("请求过大")
		}
		var r Request
		dec := json.NewDecoder(bytes.NewReader(b))
		dec.DisallowUnknownFields()
		if dec.Decode(&r) != nil || r.Args == nil {
			return fail("请求格式无效")
		}
		var extra any
		if dec.Decode(&extra) != io.EOF {
			return fail("请求格式无效")
		}
		read := r.Action == "web.clash.state" || r.Action == "web.tailscale.state" || r.Action == "web.tailscale.netcheck"
		if !read {
			f, e := os.OpenFile("/tmp/u60-control.lock", os.O_CREATE|os.O_RDWR|syscall.O_NOFOLLOW, 0600)
			if e != nil {
				return fail("控制锁不可用")
			}
			defer f.Close()
			end := time.Now().Add(3 * time.Second)
			for syscall.Flock(int(f.Fd()), syscall.LOCK_EX|syscall.LOCK_NB) != nil {
				if time.Now().After(end) {
					return fail("另一个设置正在执行，请稍后重试")
				}
				time.Sleep(50 * time.Millisecond)
			}
			defer syscall.Flock(int(f.Fd()), syscall.LOCK_UN)
			if _, e = os.Stat("/tmp/u60-standby/active"); e == nil {
				return fail("正在恢复休眠前的服务，请稍后重试")
			}
		}
		a := &App{root: "/data/u60-clash", tsSock: "/tmp/tailscale/tailscaled.sock", http: &http.Client{Timeout: 15 * time.Second}}
		a.validate = func(path string) error {
			ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
			defer cancel()
			c := exec.CommandContext(ctx, filepath.Join(a.root, "mihomo"), "-t", "-d", a.root, "-f", path)
			c.Stdout = io.Discard
			c.Stderr = io.Discard
			return c.Run()
		}
		a.reload = func() error {
			_, e := a.api("PUT", "/configs?force=true", M{"path": filepath.Join(a.root, "config.yaml")})
			return e
		}
		return a.dispatch(r)
	}()
	enc := json.NewEncoder(os.Stdout)
	enc.SetEscapeHTML(true)
	enc.Encode(result)
}
