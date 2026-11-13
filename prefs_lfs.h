// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// Preferences (mesma interface da biblioteca do ESP32) sobre LittleFS, para o ESP8266.
// Cada namespace e um arquivo /prefs/<ns> com registros [tam. chave][chave][tam. valor (2)][valor].
// begin() carrega o arquivo na RAM (poucas centenas de bytes) e end() grava se algo mudou: a
// escrita e atomica (arquivo .tmp + rename), para uma queda de energia nao corromper o Wi-Fi salvo.
#pragma once
#if defined(ESP8266)
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false) {
        end();
        if (!fsReady()) return false;
        path_ = String("/prefs/") + name;
        readOnly_ = readOnly;
        load();
        open_ = true;
        return true;
    }
    void end() {
        if (open_ && dirty_ && !readOnly_) save();
        open_ = false; dirty_ = false;
        count_ = 0;
        buf_.clear();
    }
    bool isKey(const char* key) { return find(key) >= 0; }
    bool remove(const char* key) {
        int i = find(key);
        if (i < 0 || readOnly_) return false;
        for (int k = i; k < count_ - 1; k++) entries_[k] = entries_[k + 1];
        count_--; dirty_ = true;
        return true;
    }
    bool clear() { if (readOnly_) return false; count_ = 0; dirty_ = true; return true; }

    // Numeros: guardados com o tamanho do tipo (como na NVS); a leitura aceita qualquer tamanho salvo
    size_t putBool(const char* k, bool v) { uint8_t b = v; return putRaw(k, &b, 1); }
    size_t putChar(const char* k, int8_t v) { return putRaw(k, &v, 1); }
    size_t putUChar(const char* k, uint8_t v) { return putRaw(k, &v, 1); }
    size_t putShort(const char* k, int16_t v) { return putRaw(k, &v, 2); }
    size_t putInt(const char* k, int32_t v) { return putRaw(k, &v, 4); }
    size_t putUInt(const char* k, uint32_t v) { return putRaw(k, &v, 4); }
    size_t putLong(const char* k, int32_t v) { return putRaw(k, &v, 4); }
    size_t putULong(const char* k, uint32_t v) { return putRaw(k, &v, 4); }
    size_t putFloat(const char* k, float v) { return putRaw(k, &v, 4); }
    size_t putDouble(const char* k, double v) { return putRaw(k, &v, 8); }
    size_t putString(const char* k, const String& v) { return putRaw(k, v.c_str(), v.length()); }
    size_t putString(const char* k, const char* v) { return putRaw(k, v, strlen(v)); }
    size_t putBytes(const char* k, const void* v, size_t n) { return putRaw(k, v, n); }

    bool getBool(const char* k, bool d = false) { uint8_t b; return getRaw(k, &b, 1) ? b != 0 : d; }
    int8_t getChar(const char* k, int8_t d = 0) { int8_t v; return getRaw(k, &v, 1) ? v : d; }
    uint8_t getUChar(const char* k, uint8_t d = 0) { uint8_t v; return getRaw(k, &v, 1) ? v : d; }
    int16_t getShort(const char* k, int16_t d = 0) { int16_t v; return getRaw(k, &v, 2) ? v : d; }
    int32_t getInt(const char* k, int32_t d = 0) { int32_t v; return getRaw(k, &v, 4) ? v : d; }
    uint32_t getUInt(const char* k, uint32_t d = 0) { uint32_t v; return getRaw(k, &v, 4) ? v : d; }
    int32_t getLong(const char* k, int32_t d = 0) { int32_t v; return getRaw(k, &v, 4) ? v : d; }
    uint32_t getULong(const char* k, uint32_t d = 0) { uint32_t v; return getRaw(k, &v, 4) ? v : d; }
    float getFloat(const char* k, float d = NAN) { float v; return getRaw(k, &v, 4) ? v : d; }
    double getDouble(const char* k, double d = NAN) { double v; return getRaw(k, &v, 8) ? v : d; }
    String getString(const char* k, const String& d = String()) {
        int i = find(k);
        if (i < 0) return d;
        String out;
        out.reserve(entries_[i].valLen);
        for (uint16_t j = 0; j < entries_[i].valLen; j++) out += (char)buf_[entries_[i].valOff + j];
        return out;
    }
    size_t getBytesLength(const char* k) { int i = find(k); return i < 0 ? 0 : entries_[i].valLen; }
    size_t getBytes(const char* k, void* buf, size_t maxLen) {
        int i = find(k);
        if (i < 0) return 0;
        size_t n = min((size_t)entries_[i].valLen, maxLen);
        memcpy(buf, buf_.data() + entries_[i].valOff, n);
        return n;
    }

private:
    struct Entry { uint16_t keyOff; uint8_t keyLen; uint16_t valOff; uint16_t valLen; };
    static const int MAX_ENTRIES = 48;
    Entry entries_[MAX_ENTRIES];
    int count_ = 0;
    std::vector<uint8_t> buf_; // conteudo do arquivo (chaves e valores), referenciado pelos offsets
    String path_;
    bool open_ = false, readOnly_ = false, dirty_ = false;

    static bool fsReady() {
        static int8_t ok = -1;
        if (ok < 0) {
            ok = LittleFS.begin() ? 1 : 0;
            if (!ok) { // flash nova ou corrompida: formata uma vez
                LittleFS.format();
                ok = LittleFS.begin() ? 1 : 0;
            }
            if (ok && !LittleFS.exists("/prefs")) LittleFS.mkdir("/prefs");
        }
        return ok == 1;
    }

    int find(const char* key) const {
        size_t kl = strlen(key);
        for (int i = 0; i < count_; i++) {
            if (entries_[i].keyLen == kl && memcmp(buf_.data() + entries_[i].keyOff, key, kl) == 0) return i;
        }
        return -1;
    }

    bool getRaw(const char* k, void* out, size_t n) {
        int i = find(k);
        if (i < 0) return false;
        if (entries_[i].valLen < n) { // salvo com tipo menor (ex.: char lido como int): estende o sinal
            memset(out, (entries_[i].valLen && ((int8_t)buf_[entries_[i].valOff + entries_[i].valLen - 1]) < 0) ? 0xFF : 0, n);
            memcpy(out, buf_.data() + entries_[i].valOff, entries_[i].valLen);
        } else {
            memcpy(out, buf_.data() + entries_[i].valOff, n);
        }
        return true;
    }

    size_t putRaw(const char* k, const void* v, size_t n) {
        if (!open_ || readOnly_ || n > 1024) return 0;
        size_t kl = strlen(k);
        if (kl == 0 || kl > 15) return 0; // mesmo limite de chave da NVS
        int i = find(k);
        if (i >= 0 && entries_[i].valLen == n && memcmp(buf_.data() + entries_[i].valOff, v, n) == 0) return n; // igual: nao grava
        if (i >= 0) remove(k);
        if (count_ >= MAX_ENTRIES) return 0;
        Entry e;
        e.keyOff = buf_.size(); e.keyLen = kl;
        buf_.insert(buf_.end(), (const uint8_t*)k, (const uint8_t*)k + kl);
        e.valOff = buf_.size(); e.valLen = n;
        buf_.insert(buf_.end(), (const uint8_t*)v, (const uint8_t*)v + n);
        entries_[count_++] = e;
        dirty_ = true;
        return n;
    }

    void load() {
        count_ = 0; buf_.clear();
        File f = LittleFS.open(path_, "r");
        if (!f) return;
        while (f.available() && count_ < MAX_ENTRIES) {
            int kl = f.read();
            if (kl <= 0 || kl > 15) break;
            char key[16];
            if (f.readBytes(key, kl) != (size_t)kl) break;
            uint8_t lb[2];
            if (f.readBytes((char*)lb, 2) != 2) break;
            uint16_t vl = lb[0] | (lb[1] << 8);
            if (vl > 1024) break;
            Entry e;
            e.keyOff = buf_.size(); e.keyLen = kl;
            buf_.insert(buf_.end(), (const uint8_t*)key, (const uint8_t*)key + kl);
            e.valOff = buf_.size(); e.valLen = vl;
            for (uint16_t j = 0; j < vl; j++) { int c = f.read(); if (c < 0) { f.close(); return; } buf_.push_back((uint8_t)c); }
            entries_[count_++] = e;
        }
        f.close();
    }

    void save() {
        String tmp = path_ + ".tmp";
        File f = LittleFS.open(tmp, "w");
        if (!f) return;
        for (int i = 0; i < count_; i++) {
            const Entry& e = entries_[i];
            f.write((uint8_t)e.keyLen);
            f.write(buf_.data() + e.keyOff, e.keyLen);
            uint8_t lb[2] = {(uint8_t)(e.valLen & 0xFF), (uint8_t)(e.valLen >> 8)};
            f.write(lb, 2);
            f.write(buf_.data() + e.valOff, e.valLen);
        }
        f.close();
        LittleFS.remove(path_);
        LittleFS.rename(tmp, path_);
    }
};
#endif
