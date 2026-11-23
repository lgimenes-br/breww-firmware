#!/usr/bin/env python3
"""OneWire 2.3.8 nao conhece os registradores do ESP32-P4 (placa p4-7). Acrescenta um ramo
CONFIG_IDF_TARGET_ESP32P4 (registradores com .val, pinos 0-31 e 32-54) sem mudar as outras placas.
Uso: python3 ci/patch_onewire_p4.py <pasta da biblioteca OneWire>. Idempotente."""
import sys, os
path = os.path.join(sys.argv[1], 'util', 'OneWire_direct_gpio.h')
s = open(path).read()
if 'CONFIG_IDF_TARGET_ESP32P4' in s:
    print('OneWire: ja tem suporte ao P4'); sys.exit(0)
P4 = {
    '    return (GPIO.in.val >> pin) & 0x1;': '    return pin < 32 ? (GPIO.in.val >> pin) & 0x1 : (GPIO.in1.val >> (pin - 32)) & 0x1;',
    '    GPIO.out_w1tc.val = ((uint32_t)1 << pin);': '    if (pin < 32) GPIO.out_w1tc.val = ((uint32_t)1 << pin); else GPIO.out1_w1tc.val = ((uint32_t)1 << (pin - 32));',
    '    GPIO.out_w1ts.val = ((uint32_t)1 << pin);': '    if (pin < 32) GPIO.out_w1ts.val = ((uint32_t)1 << pin); else GPIO.out1_w1ts.val = ((uint32_t)1 << (pin - 32));',
    '    GPIO.enable_w1tc.val = ((uint32_t)1 << (pin));': '    if (pin < 32) GPIO.enable_w1tc.val = ((uint32_t)1 << pin); else GPIO.enable1_w1tc.val = ((uint32_t)1 << (pin - 32));',
    '    GPIO.enable_w1ts.val = ((uint32_t)1 << (pin));': '    if (pin < 32) GPIO.enable_w1ts.val = ((uint32_t)1 << pin); else GPIO.enable1_w1ts.val = ((uint32_t)1 << (pin - 32));',
}
n = 0
for c3, p4 in P4.items():
    old = '#if CONFIG_IDF_TARGET_ESP32C3\n' + c3
    if old not in s:
        sys.exit('OneWire: formato inesperado (' + c3.strip() + ')')
    s = s.replace(old, '#if CONFIG_IDF_TARGET_ESP32P4\n' + p4 + '\n#elif CONFIG_IDF_TARGET_ESP32C3\n' + c3)
    n += 1
open(path, 'w').write(s)
print('OneWire: suporte ao P4 acrescentado (%d funcoes)' % n)
