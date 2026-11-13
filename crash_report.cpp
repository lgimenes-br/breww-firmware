// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "crash_report.h"
#include "platform.h"
#include "net_manager.h"
#include "version.h"
#if !defined(ESP8266)
#include <esp_core_dump.h>
#endif

void crash_report_check() {
    char ev[240];
#if defined(ESP8266)
    const rst_info* r = ESP.getResetInfoPtr();
    if (!r || (r->reason != REASON_EXCEPTION_RST && r->reason != REASON_SOFT_WDT_RST && r->reason != REASON_WDT_RST)) return;
    snprintf_P(ev, sizeof(ev), PSTR("{\"type\":\"crash\",\"fw\":\"%s\",\"reason\":%u,\"cause\":%u,\"pc\":\"%08x\",\"addr\":\"%08x\",\"epc2\":\"%08x\",\"epc3\":\"%08x\"}"),
               FW_VERSION, (unsigned)r->reason, (unsigned)r->exccause, (unsigned)r->epc1, (unsigned)r->excvaddr, (unsigned)r->epc2, (unsigned)r->epc3);
#else
    if (esp_core_dump_image_check() != ESP_OK) return; // sem travamento gravado
    esp_core_dump_summary_t* s = (esp_core_dump_summary_t*)calloc(1, sizeof(esp_core_dump_summary_t));
    if (!s) return;
    if (esp_core_dump_get_summary(s) != ESP_OK) { free(s); esp_core_dump_image_erase(); return; }
    char task[17];
    size_t t = 0;
    for (; t < sizeof(task) - 1 && s->exc_task[t]; t++) task[t] = isprint((unsigned char)s->exc_task[t]) && s->exc_task[t] != '"' && s->exc_task[t] != '\\' ? s->exc_task[t] : '?';
    task[t] = '\0';
    char bt[100] = "";
#if CONFIG_IDF_TARGET_ARCH_XTENSA
    // Pilha de chamadas (ate 7 enderecos, sem "0x" para caber no evento)
    size_t len = 0;
    for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 7; i++) len += snprintf(bt + len, sizeof(bt) - len, "%s%08lx", i ? " " : "", (unsigned long)s->exc_bt_info.bt[i]);
    const unsigned cause = s->ex_info.exc_cause;
    const unsigned long addr = s->ex_info.exc_vaddr;
    const int corrupted = s->exc_bt_info.corrupted ? 1 : 0;
#else // RISC-V (C3): sem pilha de chamadas no resumo; vai o endereco de retorno
    snprintf(bt, sizeof(bt), "%08lx", (unsigned long)s->ex_info.ra);
    const unsigned cause = s->ex_info.mcause;
    const unsigned long addr = s->ex_info.mtval;
    const int corrupted = 0;
#endif
    snprintf_P(ev, sizeof(ev), PSTR("{\"type\":\"crash\",\"fw\":\"%s\",\"sha\":\"%.12s\",\"task\":\"%s\",\"pc\":\"%08lx\",\"cause\":%u,\"addr\":\"%08lx\",\"bt\":\"%s\",\"btBad\":%d}"),
               FW_VERSION, (const char*)s->app_elf_sha256, task, (unsigned long)s->exc_pc, cause, addr, bt, corrupted);
    free(s);
    esp_core_dump_image_erase(); // um relato por travamento
#endif
    Serial.printf_P(PSTR("[Travamento] Ultimo erro fatal: %s\n"), ev);
    diag_report(ev);
}
