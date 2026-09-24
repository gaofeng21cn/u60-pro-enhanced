package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"gopkg.in/yaml.v3"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

const fixture = `mixed-port: 17890
secret: "private-test-secret"
mode: rule
proxy-providers:
  old:
    type: http
    url: "https://provider.invalid/sub?token=private-test-token"
    interval: 1800
    path: ./proxy_provider/old.yaml
proxy-groups:
  - name: Main
    type: select
    proxies: [Old, DIRECT]
  - name: Old
    type: select
    use: [old]
rules:
  # U60-PANEL-RULES-BEGIN
  - DOMAIN-SUFFIX,example.org,DIRECT
  # U60-PANEL-RULES-END
  - MATCH,Main
`

func testApp(t *testing.T) *App {
	t.Helper()
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "config.yaml"), []byte(fixture), 0600)
	a := &App{root: dir}
	a.validate = func(p string) error {
		b, e := os.ReadFile(p)
		if e != nil {
			return e
		}
		var root yaml.Node
		return yaml.Unmarshal(b, &root)
	}
	a.reload = func() error { return nil }
	a.testAPI = func(method, path string, d any) (M, error) {
		if path == "/proxies" {
			return M{"proxies": M{"Main": M{"type": "Selector", "all": []any{"Old", "DIRECT"}, "now": "Old"}, "Old": M{"type": "Selector", "all": []any{"leaf"}, "now": "leaf"}}}, nil
		}
		if strings.HasPrefix(path, "/providers/proxies/") {
			return M{"proxies": []any{M{"name": "leaf"}}}, nil
		}
		return M{}, nil
	}
	return a
}
func TestSubscriptionAddPreservesUnrelatedConfig(t *testing.T) {
	a := testApp(t)
	old, _, _ := a.config()
	r := a.saveSubscription(M{"name": "New provider", "url": "https://new.invalid/sub?token=new-test-token", "interval": "3600", "destination": "Main", "revision": sha(old)})
	if !boolv(r["ok"]) {
		t.Fatal(r)
	}
	b, root, e := a.config()
	if e != nil {
		t.Fatal(e)
	}
	if nstr(named(named(root, "proxy-providers"), "New provider"), "url") != "https://new.invalid/sub?token=new-test-token" {
		t.Fatal("provider missing")
	}
	if !bytes.Equal(bytes.SplitN(old, []byte("rules:\n"), 2)[1], bytes.SplitN(b, []byte("rules:\n"), 2)[1]) {
		t.Fatal("rules or screen markers changed")
	}
	if nstr(root, "secret") != "private-test-secret" || nstr(root, "mode") != "rule" {
		t.Fatal("unrelated config changed")
	}
	if len(localRules(b)) != 1 {
		t.Fatal("screen local rules lost")
	}
	st, _ := os.Stat(filepath.Join(a.root, "config.yaml"))
	if st.Mode().Perm() != 0600 {
		t.Fatal("credential config permissions")
	}
}
func TestRejectedSubscriptionDoesNotWrite(t *testing.T) {
	for _, url := range []string{"file:///etc/passwd", "https://user:pass@example.com/x", "https://example.com/x\nheaders: x", "javascript:alert(1)"} {
		a := testApp(t)
		old, _, _ := a.config()
		r := a.saveSubscription(M{"name": "New", "url": url, "interval": "3600", "destination": "Main", "revision": sha(old)})
		if boolv(r["ok"]) {
			t.Fatal("accepted", url)
		}
		b, _, _ := a.config()
		if !bytes.Equal(old, b) {
			t.Fatal("invalid request changed config")
		}
	}
}
func TestStaleRevisionAndValidationFailure(t *testing.T) {
	a := testApp(t)
	old, _, _ := a.config()
	args := M{"name": "New", "url": "https://new.invalid/sub", "interval": "3600", "destination": "Main", "revision": "stale"}
	if boolv(a.saveSubscription(args)["ok"]) {
		t.Fatal("stale accepted")
	}
	args["revision"] = sha(old)
	a.validate = func(string) error { return errors.New("secret-bearing compiler error must not leak") }
	r := a.saveSubscription(args)
	if boolv(r["ok"]) || strings.Contains(text(r["message"]), "secret-bearing") {
		t.Fatal(r)
	}
	b, _, _ := a.config()
	if !bytes.Equal(old, b) {
		t.Fatal("validation did not preserve config")
	}
}
func TestReloadFailureRollsBack(t *testing.T) {
	a := testApp(t)
	old, _, _ := a.config()
	calls := 0
	a.reload = func() error {
		calls++
		if calls == 1 {
			return errors.New("failed")
		}
		return nil
	}
	r := a.saveSubscription(M{"name": "New", "url": "https://new.invalid/sub", "interval": "3600", "destination": "Main", "revision": sha(old)})
	if boolv(r["ok"]) || calls != 2 {
		t.Fatal(r, calls)
	}
	b, _, _ := a.config()
	if !bytes.Equal(old, b) {
		t.Fatal("rollback did not restore bytes")
	}
}
func TestActiveSubscriptionCannotBeDeleted(t *testing.T) {
	a := testApp(t)
	old, _, _ := a.config()
	r := a.deleteSubscription(M{"name": "old", "revision": sha(old)})
	if boolv(r["ok"]) {
		t.Fatal("active subscription deleted")
	}
	b, _, _ := a.config()
	if !bytes.Equal(b, old) {
		t.Fatal("active config changed")
	}
}
func TestUnselectedSubscriptionCanBeDeleted(t *testing.T) {
	a := testApp(t)
	original := a.testAPI
	a.testAPI = func(m, p string, d any) (M, error) {
		if p == "/proxies" {
			return M{"proxies": M{"Main": M{"type": "Selector", "all": []any{"Old", "DIRECT"}, "now": "DIRECT"}, "Old": M{"type": "Selector", "all": []any{"leaf"}, "now": "leaf"}}}, nil
		}
		return original(m, p, d)
	}
	old, _, _ := a.config()
	r := a.deleteSubscription(M{"name": "old", "revision": sha(old)})
	if !boolv(r["ok"]) {
		t.Fatal(r)
	}
	_, root, _ := a.config()
	if named(named(root, "proxy-providers"), "old") != nil {
		t.Fatal("provider retained")
	}
}
func TestStateNeverReturnsSubscriptionCredentials(t *testing.T) {
	a := testApp(t)
	s := a.clashState()
	for _, v := range arr(s["providers"]) {
		p := obj(v)
		if _, ok := p["url"]; ok {
			t.Fatal("url exposed")
		}
		if text(p["host"]) != "provider.invalid" {
			t.Fatal(p)
		}
	}
}
func TestNodeStateFollowsNestedActiveSelector(t *testing.T) {
	a := testApp(t)
	a.testAPI = func(method, path string, data any) (M, error) {
		switch path {
		case "/proxies":
			return M{"proxies": M{
				"GLOBAL":  M{"type": "Selector", "all": []any{"DIRECT", "Main"}, "now": "DIRECT"},
				"Main":    M{"type": "Selector", "all": []any{"DIRECT", "Mojie"}, "now": "Mojie"},
				"Mojie":   M{"type": "Selector", "all": []any{"Leaf 01", "Leaf 02"}, "now": "Leaf 02"},
				"Leaf 01": M{"type": "Vless"}, "Leaf 02": M{"type": "Vless"},
			}}, nil
		case "/configs":
			return M{"mode": "rule"}, nil
		case "/rules":
			return M{"rules": []any{M{"type": "Match", "proxy": "Main"}}}, nil
		case "/providers/proxies":
			return M{"providers": M{}}, nil
		case "/providers/rules":
			return M{"providers": M{}}, nil
		case "/connections":
			return M{"connections": []any{}}, nil
		default:
			return M{}, nil
		}
	}
	s := a.clashState()
	if text(s["active_group"]) != "Mojie" {
		t.Fatalf("active leaf group = %v", s["active_group"])
	}
	if got := arr(s["active_path"]); len(got) != 2 || got[0] != "Main" || got[1] != "Mojie" {
		t.Fatalf("active path = %v", got)
	}
	if text(s["active_node"]) != "Leaf 02" || !boolv(s["core_online"]) || text(s["mode"]) != "rule" {
		t.Fatalf("state = %v", s)
	}
	groups := arr(s["groups"])
	var main, mojie M
	for _, raw := range groups {
		g := obj(raw)
		switch text(g["name"]) {
		case "Main":
			main = g
		case "Mojie":
			mojie = g
		}
	}
	if got := arr(main["selectable_nodes"]); len(got) != 0 {
		t.Fatalf("outer selector exposed as leaf choices: %v", got)
	}
	if got := arr(mojie["selectable_nodes"]); len(got) != 2 || got[0] != "Leaf 01" || got[1] != "Leaf 02" {
		t.Fatalf("leaf selector choices = %v", got)
	}
}

func TestActiveSelectorGroupStopsAtCurrentLeafSelector(t *testing.T) {
	proxies := M{
		"Main":  M{"type": "Selector", "all": []any{"DIRECT", "Mojie"}, "now": "Mojie"},
		"Mojie": M{"type": "Selector", "all": []any{"Leaf"}, "now": "Leaf"},
		"Leaf":  M{"type": "Vless"},
	}
	if got := activeSelectorGroup(proxies, "Main"); got != "Mojie" {
		t.Fatalf("nested selector = %q", got)
	}
	proxies["Mojie"] = M{"type": "Selector", "all": []any{"Leaf"}, "now": ""}
	if got := activeSelectorGroup(proxies, "Main"); got != "Mojie" {
		t.Fatalf("unselected leaf selector = %q", got)
	}
	proxies["Mojie"] = M{"type": "Selector", "all": []any{"Main"}, "now": "Main"}
	if got := activeSelectorGroup(proxies, "Main"); got != "" {
		t.Fatalf("selector cycle accepted as %q", got)
	}
}

func TestSelectProxyValidatesMembershipAndReadsBack(t *testing.T) {
	a := testApp(t)
	selected := "Old"
	calls := []string{}
	a.testAPI = func(method, path string, data any) (M, error) {
		calls = append(calls, method+" "+path)
		if path != "/proxies/Main" {
			t.Fatalf("unexpected selector path %q", path)
		}
		switch method {
		case "GET":
			return M{"type": "Selector", "all": []any{"Old", "Leaf 01"}, "now": selected}, nil
		case "PUT":
			selected = text(obj(data)["name"])
			return M{}, nil
		default:
			t.Fatalf("unexpected method %q", method)
			return nil, errors.New("unexpected method")
		}
	}
	r := a.dispatch(Request{Action: "web.clash.select", Args: M{"group": "Main", "name": "Leaf 01"}})
	if !boolv(r["ok"]) || selected != "Leaf 01" {
		t.Fatalf("selection was not confirmed: %v", r)
	}
	if strings.Join(calls, ",") != "GET /proxies/Main,PUT /proxies/Main,GET /proxies/Main" {
		t.Fatalf("unexpected selection flow: %v", calls)
	}

	calls = nil
	r = a.dispatch(Request{Action: "web.clash.select", Args: M{"group": "Main", "name": "Missing"}})
	if boolv(r["ok"]) || len(calls) != 1 || calls[0] != "GET /proxies/Main" {
		t.Fatalf("unknown node reached a write: result=%v calls=%v", r, calls)
	}
}

func TestClashStateReportsProfileTakeover(t *testing.T) {
	a := testApp(t)
	dir := t.TempDir()
	a.panelDir = dir
	if s := a.clashState(); text(s["profile"]) != "direct" || boolv(s["takeover"]) {
		t.Fatalf("default profile = %v", s)
	}
	os.WriteFile(filepath.Join(dir, "network-profile"), []byte("clash\n"), 0600)
	if s := a.clashState(); !boolv(s["takeover"]) || text(s["profile"]) != "clash" {
		t.Fatalf("takeover not reported: %v", s)
	}
	os.WriteFile(filepath.Join(dir, "network-profile"), []byte("nonsense\n"), 0600)
	if s := a.clashState(); boolv(s["takeover"]) || text(s["profile"]) != "direct" {
		t.Fatalf("unknown profile must not claim takeover: %v", s["profile"])
	}
}

func diagnoseLines(t *testing.T, r M) string {
	t.Helper()
	if !boolv(r["ok"]) {
		t.Fatalf("diagnose failed: %v", r)
	}
	report := obj(r["report"])
	if text(report["title"]) != "代理覆盖自检" {
		t.Fatalf("report = %v", report)
	}
	parts := []string{}
	for _, line := range arr(report["lines"]) {
		parts = append(parts, text(line))
	}
	return strings.Join(parts, "\n")
}

func TestClashDiagnoseReportsActualCoverage(t *testing.T) {
	a := testApp(t)
	dir := t.TempDir()
	a.panelDir = dir
	a.testAPI = func(method, path string, data any) (M, error) {
		switch path {
		case "/version":
			return M{"version": "v1.19.31"}, nil
		case "/configs":
			return M{"mode": "rule"}, nil
		case "/rules":
			return M{"rules": []any{M{"type": "Match", "proxy": "Main"}}}, nil
		case "/proxies":
			return M{"proxies": M{
				"Main":    M{"type": "Selector", "all": []any{"Mojie"}, "now": "Mojie"},
				"Mojie":   M{"type": "Selector", "all": []any{"Leaf 01"}, "now": "Leaf 01"},
				"Leaf 01": M{"type": "Vless"},
			}}, nil
		}
		return M{}, nil
	}
	script := filepath.Join(dir, "network-profile.sh")
	os.WriteFile(filepath.Join(dir, "network-profile"), []byte("clash\n"), 0600)
	os.WriteFile(script, []byte(`#!/bin/sh
printf '%s\n' '{"ok":true,"profile":"clash","redirect":{"prerouting_tcp":true,"dns":true,"udp443_reject":true,"guard":false,"ipv6_redirect":false},"ipv6_default_route":true}'
`), 0700)
	lines := diagnoseLines(t, a.clashDiagnose())
	for _, want := range []string{
		"核心：运行中 · v1.19.31",
		"接管：已启用 Clash 出口",
		"转发规则：TCP 已生效 · DNS 已生效 · UDP 443 拒绝 已生效",
		"IPv6：未代理，但存在 IPv6 默认路由",
		"UDP：未代理",
		"当前路径：MATCH → Main → Mojie → Leaf 01",
	} {
		if !strings.Contains(lines, want) {
			t.Fatalf("missing %q in\n%s", want, lines)
		}
	}
	if strings.Contains(lines, "失败保护") {
		t.Fatal("guard reported without evidence")
	}
	os.WriteFile(script, []byte(`#!/bin/sh
printf '%s\n' '{"ok":true,"profile":"clash","redirect":{"prerouting_tcp":true,"dns":false,"udp443_reject":true,"guard":true,"ipv6_redirect":false},"ipv6_default_route":false}'
`), 0700)
	lines = diagnoseLines(t, a.clashDiagnose())
	if !strings.Contains(lines, "失败保护：正在阻断经过本机的转发") {
		t.Fatalf("fail-closed guard not reported:\n%s", lines)
	}
	if !strings.Contains(lines, "DNS 未确认") || !strings.Contains(lines, "\nIPv6：未代理\n") {
		t.Fatalf("partial coverage not reported:\n%s", lines)
	}
	os.WriteFile(script, []byte("#!/bin/sh\nexit 3\n"), 0700)
	lines = diagnoseLines(t, a.clashDiagnose())
	if !strings.Contains(lines, "转发规则：无法核验") || strings.Contains(lines, "IPv6：未代理，但存在") {
		t.Fatalf("missing verifier not reported honestly:\n%s", lines)
	}
}

func TestClashDiagnoseSeparatesCoreFromDirectProfile(t *testing.T) {
	a := testApp(t)
	a.panelDir = t.TempDir()
	a.testAPI = func(method, path string, data any) (M, error) {
		return M{}, errors.New("core down")
	}
	lines := diagnoseLines(t, a.clashDiagnose())
	if !strings.Contains(lines, "核心：未运行") || !strings.Contains(lines, "接管：未启用，当前直连") {
		t.Fatalf("stopped core reported as takeover:\n%s", lines)
	}
	if !strings.Contains(lines, "当前路径：未确认") {
		t.Fatalf("unknown path reported as known:\n%s", lines)
	}
}

func TestSelectProxyRejectsNonSelectorAndPathInjection(t *testing.T) {
	a := testApp(t)
	puts := 0
	a.testAPI = func(method, path string, data any) (M, error) {
		if method == "PUT" {
			puts++
		}
		return M{"type": "URLTest", "all": []any{"Leaf"}, "now": "Leaf"}, nil
	}
	if boolv(a.selectProxy(M{"group": "Main", "name": "Leaf"})["ok"]) || puts != 0 {
		t.Fatal("non-manual strategy was selected")
	}
	if boolv(a.selectProxy(M{"group": "../configs", "name": "global"})["ok"]) || puts != 0 {
		t.Fatal("path injection was selected")
	}
}

func TestRoutesValidation(t *testing.T) {
	got, e := parseRoutes("192.168.1.7/24,192.168.1.0/24\n10.0.0.0/24")
	if e != nil || len(got) != 2 || got[1] != "192.168.1.0/24" {
		t.Fatal(got, e)
	}
	for _, v := range []string{"0.0.0.0/0", "::/0", "127.0.0.0/8", "224.0.0.0/4", "garbage"} {
		if _, e = parseRoutes(v); e == nil {
			t.Fatal("accepted invalid route", v)
		}
	}
}

// An isolated real core validates provider parsing, reloads, selection retention,
// failed downloads and deletion without touching the router configuration.
func TestRealMihomoSubscriptionLifecycle(t *testing.T) {
	bin := os.Getenv("TEST_MIHOMO")
	if bin == "" {
		t.Skip("TEST_MIHOMO not supplied")
	}
	bin, _ = filepath.Abs(bin)
	sub := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/bad" {
			w.WriteHeader(503)
			return
		}
		fmt.Fprintln(w, "proxies:\n  - name: Test leaf\n    type: socks5\n    server: 127.0.0.1\n    port: 9")
	}))
	defer sub.Close()
	l, _ := net.Listen("tcp", "127.0.0.1:0")
	addr := l.Addr().String()
	l.Close()
	a := &App{root: t.TempDir(), apiBase: "http://" + addr, http: &http.Client{Timeout: 20 * time.Second}}
	initial := []byte("external-controller: " + addr + "\nsecret: isolated-test\nmode: rule\nlog-level: silent\nproxy-providers: {}\nproxy-groups:\n  - name: Main\n    type: select\n    proxies: [DIRECT, REJECT]\nrules:\n  # U60-PANEL-RULES-BEGIN\n  # U60-PANEL-RULES-END\n  - MATCH,Main\n")
	os.WriteFile(filepath.Join(a.root, "config.yaml"), initial, 0600)
	cmd := exec.Command(bin, "-d", a.root)
	if e := cmd.Start(); e != nil {
		t.Fatal(e)
	}
	defer func() { cmd.Process.Kill(); cmd.Wait() }()
	until := time.Now().Add(10 * time.Second)
	for {
		if _, e := a.api("GET", "/version", nil); e == nil {
			break
		}
		if time.Now().After(until) {
			t.Fatal("core did not start")
		}
		time.Sleep(100 * time.Millisecond)
	}
	a.validate = func(p string) error { return exec.Command(bin, "-t", "-d", a.root, "-f", p).Run() }
	a.reload = func() error {
		_, e := a.api("PUT", "/configs?force=true", M{"path": filepath.Join(a.root, "config.yaml")})
		return e
	}
	args := M{"name": "Test sub", "url": sub.URL + "/good", "interval": "3600", "destination": "Main", "revision": sha(initial)}
	if r := a.saveSubscription(args); !boolv(r["ok"]) {
		t.Fatal(r)
	}
	proxies, e := a.api("GET", "/proxies", nil)
	if e != nil || text(obj(obj(proxies["proxies"])["Main"])["now"]) != "DIRECT" {
		t.Fatal("selection changed", e)
	}
	good, _, _ := a.config()
	args["existing"] = "Test sub"
	args["revision"] = sha(good)
	args["url"] = sub.URL + "/bad"
	if r := a.saveSubscription(args); boolv(r["ok"]) {
		t.Fatal("failed download accepted")
	}
	got, _, _ := a.config()
	if !bytes.Equal(good, got) {
		t.Fatal("failed URL did not restore exact config")
	}
	args["url"] = ""
	args["interval"] = "1800"
	if r := a.saveSubscription(args); !boolv(r["ok"]) {
		t.Fatal("edit", r)
	}
	got, _, _ = a.config()
	if r := a.deleteSubscription(M{"name": "Test sub", "revision": sha(got)}); !boolv(r["ok"]) {
		t.Fatal("delete", r)
	}
	_, root, _ := a.config()
	if named(named(root, "proxy-providers"), "Test sub") != nil {
		t.Fatal("delete retained provider")
	}
}

func TestTailscaleHostnameChangesOnlyMaskedPreference(t *testing.T) {
	dir, e := os.MkdirTemp("/tmp", "u60-ts-")
	if e != nil {
		t.Fatal(e)
	}
	defer os.RemoveAll(dir)
	sock := filepath.Join(dir, "ts.sock")
	l, e := net.Listen("unix", sock)
	if e != nil {
		t.Fatal(e)
	}
	prefs := M{"Hostname": "old-name", "RouteAll": true, "CorpDNS": false, "AdvertiseRoutes": []any{"192.168.0.0/24"}, "ExitNodeID": "keep-exit"}
	server := &http.Server{Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method == "PATCH" {
			var p M
			json.NewDecoder(r.Body).Decode(&p)
			if len(p) != 2 || !boolv(p["HostnameSet"]) {
				t.Error("unrelated preferences modified")
			}
			prefs["Hostname"] = p["Hostname"]
		}
		json.NewEncoder(w).Encode(prefs)
	})}
	go server.Serve(l)
	defer server.Close()
	a := &App{tsSock: sock}
	if r := a.tailscaleSet("web.tailscale.hostname", M{"hostname": "new-name"}); !boolv(r["ok"]) {
		t.Fatal(r)
	}
	if text(prefs["Hostname"]) != "new-name" || !boolv(prefs["RouteAll"]) || text(prefs["ExitNodeID"]) != "keep-exit" {
		t.Fatal("preferences not preserved")
	}
}
