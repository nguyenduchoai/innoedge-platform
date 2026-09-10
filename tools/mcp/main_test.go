// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// Test: chạy binary MCP thật, nói JSON-RPC qua stdio đúng như Claude Code /
// Cursor sẽ làm, và kiểm tra nó trả lời đúng.
package main

import (
	"bufio"
	"encoding/json"
	"os/exec"
	"strings"
	"testing"
)

// session gửi một loạt request tới server và trả về các response.
func session(t *testing.T, reqs ...string) []map[string]any {
	t.Helper()
	cmd := exec.Command("go", "run", ".", "-root", "../..")
	cmd.Stdin = strings.NewReader(strings.Join(reqs, "\n") + "\n")
	out, err := cmd.Output()
	if err != nil {
		t.Fatalf("chạy server lỗi: %v", err)
	}
	var got []map[string]any
	sc := bufio.NewScanner(strings.NewReader(string(out)))
	sc.Buffer(make([]byte, 0, 64*1024), 8*1024*1024)
	for sc.Scan() {
		var m map[string]any
		if err := json.Unmarshal(sc.Bytes(), &m); err != nil {
			t.Fatalf("server trả dòng không phải JSON: %s", sc.Text())
		}
		got = append(got, m)
	}
	return got
}

func callText(t *testing.T, resp map[string]any) string {
	t.Helper()
	res, ok := resp["result"].(map[string]any)
	if !ok {
		t.Fatalf("thiếu result: %v", resp)
	}
	if res["isError"] == true {
		t.Fatalf("tool báo lỗi: %v", res["content"])
	}
	c, _ := res["content"].([]any)
	if len(c) == 0 {
		t.Fatalf("content rỗng: %v", res)
	}
	txt, _ := c[0].(map[string]any)["text"].(string)
	return txt
}

func call(name, args string) string {
	return `{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"` +
		name + `","arguments":` + args + `}}`
}

const initReq = `{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}`

func TestInitializeVongLaiProtocolVersion(t *testing.T) {
	r := session(t, initReq)
	if len(r) != 1 {
		t.Fatalf("mong 1 response, nhận %d", len(r))
	}
	res := r[0]["result"].(map[string]any)
	// Vọng lại phiên bản client yêu cầu — client mới từ chối nếu server ép bản cũ.
	if res["protocolVersion"] != "2025-06-18" {
		t.Errorf("protocolVersion = %v, mong vọng lại 2025-06-18", res["protocolVersion"])
	}
	if _, ok := res["capabilities"].(map[string]any)["tools"]; !ok {
		t.Error("phải khai capability tools")
	}
	instr, _ := res["instructions"].(string)
	if !strings.Contains(instr, "WebSocket") || !strings.Contains(instr, "MQTT") {
		t.Error("instructions phải nói rõ WebSocket chứ không phải MQTT — đây là thứ AI hay bịa nhất")
	}
}

func TestNotificationKhongDuocTraLoi(t *testing.T) {
	// Request không có "id" là notification. Trả lời nó là sai spec JSON-RPC và
	// làm client treo.
	r := session(t, initReq, `{"jsonrpc":"2.0","method":"notifications/initialized"}`)
	if len(r) != 1 {
		t.Fatalf("mong đúng 1 response (initialize), nhận %d", len(r))
	}
}

func TestToolsList(t *testing.T) {
	r := session(t, initReq, `{"jsonrpc":"2.0","id":2,"method":"tools/list"}`)
	list := r[1]["result"].(map[string]any)["tools"].([]any)
	want := map[string]bool{"innoedge_api": false, "innoedge_protocol": false,
		"innoedge_example": false, "innoedge_rules": false}
	for _, it := range list {
		m := it.(map[string]any)
		name := m["name"].(string)
		if _, ok := want[name]; ok {
			want[name] = true
		}
		// Thiếu inputSchema là client không gọi được tool.
		if _, ok := m["inputSchema"].(map[string]any)["type"]; !ok {
			t.Errorf("%s thiếu inputSchema.type", name)
		}
	}
	for n, found := range want {
		if !found {
			t.Errorf("thiếu tool %s", n)
		}
	}
}

func TestApiTraVeHamThat(t *testing.T) {
	r := session(t, initReq, call("innoedge_api", "{}"))
	txt := callText(t, r[1])
	for _, fn := range []string{"innoedge_init", "innoedge_start", "innoedge_publish_payment",
		"innoedge_register_command", "innoedge_reboot_after_ack"} {
		if !strings.Contains(txt, fn) {
			t.Errorf("innoedge_api thiếu %s — AI sẽ không biết hàm này tồn tại", fn)
		}
	}
}

func TestProtocolCatDungMuc(t *testing.T) {
	full := callText(t, session(t, initReq, call("innoedge_protocol", "{}"))[1])
	part := callText(t, session(t, initReq, call("innoedge_protocol", `{"section":"OTA"}`))[1])
	if len(part) >= len(full) {
		t.Error("lọc section phải trả ít hơn toàn bộ")
	}
	if !strings.Contains(part, "OTA") {
		t.Error("mục OTA phải chứa chữ OTA")
	}
	// Section không tồn tại → hướng dẫn, không phải lỗi câm.
	miss := callText(t, session(t, initReq, call("innoedge_protocol", `{"section":"khong-co-muc-nay"}`))[1])
	if !strings.Contains(miss, "Không thấy") {
		t.Errorf("section sai phải báo rõ, đang trả: %.80s", miss)
	}
}

func TestExampleLietKeVaLay(t *testing.T) {
	list := callText(t, session(t, initReq, call("innoedge_example", "{}"))[1])
	for _, n := range []string{"01-hello-device", "06-coin-relay", "08-carwash"} {
		if !strings.Contains(list, n) {
			t.Errorf("danh sách example thiếu %s", n)
		}
	}
	// Khớp tên gần đúng — AI sẽ gõ "coin" chứ ít khi gõ đủ "06-coin-relay".
	one := callText(t, session(t, initReq, call("innoedge_example", `{"name":"coin"}`))[1])
	if !strings.Contains(one, "app_main.c") || !strings.Contains(one, "innoedge_publish_payment") {
		t.Error("example phải kèm mã nguồn app_main.c")
	}
	if !strings.Contains(one, "sdkconfig.defaults") || !strings.Contains(one, "COIN_PULSE_GPIO") {
		t.Error("example dùng phần cứng phải kèm chân cắm (sdkconfig.defaults)")
	}
}

func TestRulesCoDuLuatChetNguoi(t *testing.T) {
	txt := callText(t, session(t, initReq, call("innoedge_rules", "{}"))[1])
	for _, k := range []string{"MQTT", "chống trùng", "esp_restart", "NGUYÊN VĂN",
		"mark_app_valid", "device_token"} {
		if !strings.Contains(txt, k) {
			t.Errorf("bộ luật thiếu mục về %q", k)
		}
	}
}

func TestToolLaBaoLoiChoAIDocDuoc(t *testing.T) {
	r := session(t, initReq, call("innoedge_example", `{"name":"khong-ton-tai"}`))
	res := r[1]["result"].(map[string]any)
	if res["isError"] != true {
		t.Error("tên example sai phải trả isError=true")
	}
	// Phải là result.isError chứ KHÔNG phải lỗi JSON-RPC — để AI đọc và tự sửa.
	if _, isRPCErr := r[1]["error"]; isRPCErr {
		t.Error("lỗi của tool không được trả thành lỗi JSON-RPC")
	}
	txt, _ := res["content"].([]any)[0].(map[string]any)["text"].(string)
	if !strings.Contains(txt, "01-hello-device") {
		t.Error("báo lỗi phải liệt kê các tên hợp lệ để AI thử lại")
	}
}

func TestMethodLaTraLoiDungChuan(t *testing.T) {
	r := session(t, initReq, `{"jsonrpc":"2.0","id":7,"method":"khong/co"}`)
	e, ok := r[1]["error"].(map[string]any)
	if !ok {
		t.Fatal("method lạ phải trả error")
	}
	if int(e["code"].(float64)) != -32601 {
		t.Errorf("code = %v, chuẩn JSON-RPC là -32601", e["code"])
	}
}
