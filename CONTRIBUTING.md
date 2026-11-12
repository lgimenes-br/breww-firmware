# Como contribuir

Obrigado por querer ajudar! Correções, novas placas, novos sensores e traduções são bem-vindos.

## Antes de começar

- **Bug:** abra uma issue com a placa, a versão do firmware (aparece no app, em *Hardware*) e a
  saída da serial (115200), se tiver.
- **Recurso novo ou placa nova:** abra uma issue antes para combinarmos o caminho. Isso evita
  trabalho perdido.
- **Falha de segurança:** não abra issue. Siga o [SECURITY.md](SECURITY.md).

## Pull requests

1. Um assunto por PR, com a descrição do que muda e de como você testou (em qual placa).
2. Compile pelo menos a placa que você mexeu. Se mexeu em código comum, compile as quatro
   (comandos no [README](README.md#compilar)).
3. Siga o estilo do código ao redor. Comentários em português.
4. **Compatibilidade:** aparelhos em campo atualizam pelo ar. Mudanças no protocolo (MQTT,
   telemetria, comandos) precisam continuar funcionando com o servidor e o app atuais. Campo
   novo pode ser adicionado; campo existente não muda de significado.
5. **Segurança elétrica:** nada pode deixar um relé ligado sem controle. Em caso de falha
   (sensor, Wi-Fi, memória), o caminho seguro é desligar.
6. Não inclua senhas, chaves, tokens nem dados pessoais (nem os seus de teste).

## Licença das contribuições

O projeto é distribuído sob a GPL-3.0-or-later. Ao enviar uma contribuição, você declara que:

1. o trabalho é seu, ou você tem o direito de enviá-lo (*Developer Certificate of Origin*,
   [developercertificate.org](https://developercertificate.org)); assine seus commits com
   `git commit -s`;
2. a contribuição é licenciada sob a GPL-3.0-or-later; e
3. você concede aos mantenedores do Breww uma licença perpétua, mundial, gratuita e irrevogável
   para usar, modificar e redistribuir a contribuição também sob outras licenças. Isso permite,
   por exemplo, atualizar a licença do projeto no futuro. Você continua dono do que escreveu e
   pode usar seu código como quiser.

Se não concordar com o item 3, diga no PR antes do merge.
