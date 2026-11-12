// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "config.h"
#if !BREWW_HAS_LVGL // a placa com tela configura o Wi-Fi pelo assistente da tela

#include "wifi_portal.h"
#include "net_manager.h"
#include "wifi_setup.h"
#include "cloud_pairing.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

extern WebServer server; // net_manager.cpp (porta 80)

static const unsigned long OPEN_AFTER_DISCONNECTED_MS = 3UL * 60UL * 1000UL;
static const unsigned long CLOSE_AFTER_CONNECTED_MS = 60UL * 1000UL;

static DNSServer dns;
static bool active = false;
static bool routesAdded = false;
static unsigned long disconnectedSince = 0;
static unsigned long connectedSince = 0;
static String lastSsidTried;

String wifi_portal_ssid() {
    String id = get_device_id();
    return "Breww-" + (id.length() >= 4 ? id.substring(id.length() - 4) : id);
}

bool wifi_portal_active() { return active; }

// ---------------------------------------------------------------------------
// Pagina (tema do app: fundo escuro, verde #71FC8F)
// ---------------------------------------------------------------------------
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Breww - Wi-Fi</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#04070B;color:#fff;font-family:-apple-system,Segoe UI,Roboto,sans-serif;padding:24px 16px}
.c{max-width:420px;margin:0 auto}h1{font-size:22px;margin:0 0 4px}p{color:#898E99;font-size:14px;margin:0 0 16px}
.card{background:#181C25;border-radius:16px;padding:16px;margin-bottom:12px}
.net{display:flex;justify-content:space-between;padding:12px 4px;border-bottom:1px solid #262b36;cursor:pointer}
.net:last-child{border-bottom:0}.net.sel{color:#71FC8F}.sig{color:#6B6F78;font-size:13px}
label{display:block;font-size:13px;color:#898E99;margin:12px 0 6px}
input{width:100%;padding:12px;border-radius:10px;border:1px solid #2a2f3a;background:#0d1117;color:#fff;font-size:16px}
button{width:100%;margin-top:16px;padding:14px;border:0;border-radius:12px;background:#71FC8F;color:#04070B;font-size:16px;font-weight:600}
button:disabled{opacity:.5}.msg{margin-top:12px;font-size:14px;color:#898E99}.ok{color:#71FC8F}.err{color:#FF5A6E}
.code{font-size:28px;letter-spacing:4px;color:#71FC8F;font-family:ui-monospace,monospace;margin:8px 0}
a{color:#71FC8F}
</style></head><body><div class="c">
<h1>Breww</h1><p>Conecte o controlador ao Wi-Fi da sua casa.</p>
<div id="form"><div class="card" id="list"><p>Procurando redes...</p></div>
<div class="card"><label>Rede</label><input id="ssid" autocomplete="off" placeholder="Nome da rede">
<label>Senha</label><input id="pass" type="password" placeholder="Senha do Wi-Fi">
<button id="go" onclick="save()">Conectar</button><div class="msg" id="msg"></div></div></div>
<div class="card" id="done" style="display:none"></div>
</div><script>
const $=i=>document.getElementById(i);
function bars(r){return r>-60?'forte':r>-72?'bom':'fraco'}
function scan(){fetch('/wifi/scan').then(r=>r.json()).then(d=>{
 if(d.scanning){setTimeout(scan,1500);return}
 if(!d.nets.length){$('list').innerHTML='<p>Nenhuma rede encontrada. Digite o nome abaixo.</p>';return}
 $('list').innerHTML=d.nets.map(n=>'<div class="net" data-s="'+encodeURIComponent(n.s)+'"><span>'+n.s.replace(/</g,'&lt;')+(n.l?' 🔒':'')+'</span><span class="sig">'+bars(n.r)+'</span></div>').join('');
 document.querySelectorAll('.net').forEach(e=>e.onclick=()=>{document.querySelectorAll('.net').forEach(x=>x.classList.remove('sel'));e.classList.add('sel');$('ssid').value=decodeURIComponent(e.dataset.s);$('pass').focus()});
}).catch(()=>setTimeout(scan,2000))}
function save(){const s=$('ssid').value.trim();if(!s){$('msg').textContent='Escolha ou digite a rede.';return}
 $('go').disabled=true;$('msg').className='msg';$('msg').textContent='Conectando...';
 const b=new URLSearchParams();b.set('ssid',s);b.set('pass',$('pass').value);
 fetch('/wifi/save',{method:'POST',body:b}).then(()=>setTimeout(status,2000)).catch(()=>setTimeout(status,2000))}
function status(){fetch('/wifi/status').then(r=>r.json()).then(d=>{
 if(d.state==='connected'){$('form').style.display='none';$('done').style.display='block';
  $('done').innerHTML='<h1 class="ok">Conectado!</h1><p>Rede: '+d.ssid.replace(/</g,'&lt;')+'</p>'+(d.code?'<p>Código do aparelho:</p><div class="code">'+d.code+'</div>':'')+
  '<p>Volte para o Wi-Fi da sua casa e, no app Breww, toque em <b>Adicionar aparelho</b>: digite o código acima ou escolha o aparelho em <b>Buscar aparelhos na rede</b>. Pronto!</p>';return}
 if(d.state==='failed'){$('go').disabled=false;$('msg').className='msg err';$('msg').textContent='Não conectou. Confira a senha e tente de novo.';scan();return}
 setTimeout(status,2000)}).catch(()=>setTimeout(status,2000))}
scan();
</script></body></html>)HTML";

void wifi_portal_handle_root() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", PORTAL_HTML);
}

// Celulares testam a internet em URLs conhecidas; responder com a pagina faz o sistema abrir o portal
void wifi_portal_redirect() {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
}

static void handle_scan() {
    int n = WiFi.scanComplete();
    StaticJsonDocument<2048> doc;
    if (n == WIFI_SCAN_RUNNING) {
        doc["scanning"] = true;
    } else {
        JsonArray nets = doc.createNestedArray("nets");
        if (n > 0) {
            for (int i = 0; i < n && i < 20; i++) {
                String s = WiFi.SSID(i);
                if (!s.length()) continue;
                bool dup = false; // mesma rede em varios nos (mesh): mostra uma vez
                for (JsonObject o : nets) if (s == o["s"].as<const char*>()) { dup = true; break; }
                if (dup) continue;
                JsonObject o = nets.createNestedObject();
                o["s"] = s;
                o["r"] = WiFi.RSSI(i);
                o["l"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            }
        }
        if (n != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); WiFi.scanNetworks(true); } // ja prepara a proxima
    }
    String out; serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
}

static void handle_save() {
    String ssid = server.arg("ssid"); ssid.trim();
    String pass = server.arg("pass");
    if (!ssid.length() || ssid.length() > 32 || pass.length() > 64) { server.send(400, "text/plain", "dados invalidos"); return; }
    server.send(200, "application/json", "{\"ok\":true}");
    lastSsidTried = ssid;
    Serial.printf("[Portal] Conectando em \"%s\"...\n", ssid.c_str());
    wifi_setup_connect_new(ssid, pass); // grava e conecta; rede nova que falha tem a senha apagada
}

static void handle_status() {
    StaticJsonDocument<256> doc;
    if (WiFi.status() == WL_CONNECTED) {
        doc["state"] = "connected";
        doc["ssid"] = WiFi.SSID();
        String code;
        if (cloud_pair_code(code)) doc["code"] = code;
    } else if (lastSsidTried.length() && !has_wifi_credentials()) {
        doc["state"] = "failed"; // wifi_setup apagou a senha da rede nova que nao conectou
    } else {
        doc["state"] = "connecting";
    }
    String out; serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
}

static void add_routes() {
    if (routesAdded) return;
    routesAdded = true;
    server.on("/wifi/scan", HTTP_GET, handle_scan);
    server.on("/wifi/save", HTTP_POST, handle_save);
    server.on("/wifi/status", HTTP_GET, handle_status);
    // Testes de "tem internet?" de Android, iOS e Windows
    for (const char* p : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                          "/connecttest.txt", "/ncsi.txt", "/fwlink", "/redirect", "/canonical.html"}) {
        server.on(p, HTTP_GET, wifi_portal_handle_root);
    }
}

static void portal_open() {
    String ssid = wifi_portal_ssid();
    WiFi.mode(WIFI_AP_STA); // mantem o cliente tentando a rede salva enquanto o portal esta aberto
    WiFi.softAP(ssid.c_str());
    delay(100);
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP()); // qualquer nome -> o aparelho (portal cativo)
    add_routes();
    WiFi.scanNetworks(true);
    active = true;
    connectedSince = 0;
    lastSsidTried = "";
    Serial.printf("[Portal] Rede \"%s\" aberta. Conecte o celular nela e abra http://%s\n",
                  ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

static void portal_close() {
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    active = false;
    Serial.println("[Portal] Fechado (Wi-Fi conectado)");
}

void wifi_portal_loop() {
    bool connected = WiFi.status() == WL_CONNECTED;
    unsigned long now = millis();

    if (!active) {
        if (connected) { disconnectedSince = 0; return; }
        if (disconnectedSince == 0) disconnectedSince = now ? now : 1;
        bool noCredentials = !has_wifi_credentials();
        if (noCredentials || now - disconnectedSince > OPEN_AFTER_DISCONNECTED_MS) portal_open();
        return;
    }

    dns.processNextRequest();
    if (!connected) server.handleClient(); // conectado, o net_manager ja atende o servidor
    if (connected) {
        if (connectedSince == 0) connectedSince = now ? now : 1;
        if (now - connectedSince > CLOSE_AFTER_CONNECTED_MS) { portal_close(); disconnectedSince = 0; }
    } else {
        connectedSince = 0;
    }
}

#endif // !BREWW_HAS_LVGL
