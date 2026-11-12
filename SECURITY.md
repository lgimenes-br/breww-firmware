# Segurança

O Breww liga aparelhos na tomada e conversa com contas de usuários, então levamos falhas de
segurança a sério.

## Como reportar

**Não abra issue pública.** Use o
[aviso privado de vulnerabilidade do GitHub](../../security/advisories/new) deste repositório.

Inclua, se puder: versão do firmware, placa, como reproduzir e o impacto (o que alguém conseguiria
fazer). Respondemos em até 7 dias e combinamos com você quando divulgar, depois da correção.

## O que está no escopo

- Firmware deste repositório (vínculo com a conta, MQTT, atualização pelo ar, portal Wi-Fi,
  página local de atualização, Improv Serial).
- Qualquer forma de controlar ou atualizar um aparelho de outra pessoa.

O servidor e o app ([breww.live](https://breww.live)) não são abertos, mas falhas que você achar
neles a partir do firmware também podem ser reportadas pelo mesmo canal. Não teste contra contas
ou aparelhos que não são seus.

## Como o aparelho se protege (resumo)

- Cada placa gera uma chave secreta própria (256 bits) que nunca sai dela; o código de
  confirmação e a senha MQTT são derivados dessa chave.
- MQTT só com TLS e validação do certificado de `breww.live`; cada aparelho só acessa os
  próprios tópicos.
- Atualização pelo ar só de `https://breww.live/`. A página local `/update` e a atualização pela
  IDE pedem o código de confirmação do aparelho.
