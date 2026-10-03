<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Processamento de imagem na GPU — comparação OBS

Estado: implementação/entrega e aceitação de hardware pendentes. Não existe ainda uma medição aceite de redução de CPU do OBS. O source/artifact final, reviews, Windows CI e hashes serão preenchidos apenas depois dos gates exatos.

Esta comparação mantém MediaPipe e GPU - DirectML nos dois modos. A checkbox **Processamento de imagem na GPU** controla redução da imagem antes da leitura CPU e processamento espacial da máscara na GPU; a opção desligada mantém o processamento CPU anterior. O resultado mede CPU do processo OBS completo, incluindo o resto da cena.

## Uma transferência e dois comandos

Descarrega o único ZIP de entrega indicado na confirmação final; extrai o conteúdo para uma pasta **obs-br-processamento-gpu** em Downloads. Fecha OBS. Abre PowerShell como administrador e executa:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\Downloads\obs-br-processamento-gpu\instalar-processamento-gpu.ps1"
~~~

Abre OBS normalmente com a mesma cena/câmera. No mesmo PowerShell:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\Downloads\obs-br-processamento-gpu\testar-processamento-gpu.ps1"
~~~

O instalador verifica os bytes exatos do ZIP e todos os ficheiros instalados, preservando a build e o recibo anteriores fora das pastas de plugins. Não adquire providers. Para repor a instalação anterior, fecha OBS e executa o primeiro comando com **-Acao Restaurar**.

O script guia OFF → ON → ON → OFF. Cada bloco dura 20 segundos: cinco para estabilizar e quinze medidos. Os prompts pedem alterações manuais na checkbox; o script valida os estados efetivos nos logs. Mantém iluminação, movimento, resolução/FPS, definições da máscara e restante carga iguais.

A comparação padrão pede Similaridade de imagem desligada nos dois modos e recorda a opção anterior para a repores. Se usas normalmente similaridade, repete o segundo comando com **-ManterSimilaridade**; esse resultado é guardado e relatado separadamente.

## Como ler o resultado

CPU normalizada = 100 × delta de segundos CPU / (segundos monotónicos decorridos × processadores lógicos). Por exemplo, um segundo de CPU em dez segundos com dezasseis processadores lógicos corresponde a 0,625%.

O resumo apresenta médias e intervalos dos blocos OFF/ON, diferença em pontos percentuais e alteração relativa em percentagem. Separação dos intervalos é uma melhoria observada nesta comparação sequencial, sem alegação de confiança estatística. Intervalos sobrepostos, restart, mudança de filtro/fonte/definições, logs incompletos/tardios ou fallback tornam o resultado inconclusivo.

Os tempos de componentes incluem espera no host; não são tempo de execução GPU. Rendering lag usa os contadores OBS em limites de telemetria registados separadamente da janela CPU. Dropped/network frames são evidência incompleta quando não existem estatísticas OBS frescas; não são estimados pelo tempo de inferência.

O script também pede confirmação de qualidade da máscara/cabelo/bordos, resposta ao movimento, ON/OFF, CPU/DirectML, resize e remoção/recriação. Qualidade/funcionalidade e medição de CPU são resultados separados. Uma melhoria de CPU com pior máscara ou rendering não constitui aceitação de desempenho.

Os logs, amostras e caminhos completos ficam locais. Envia apenas o **resumo.txt** impresso pelo script. Preserva backups, recibos e logs se aparecer um erro; o resumo identifica a build e os motivos sem expor dados da câmera ou caminhos pessoais.

## Registo pendente

| Gate | Estado |
| --- | --- |
| Source final assinado/DCO e reviews completos | Pendente |
| Check/Windows CI, PS5.1 e package/origin exatos | Pendente |
| ZIP final descarregado e hashes verificados | Pendente |
| Qualidade/lifecycle RX 9070 XT | Pendente |
| CPU OFF/ON controlada | Pendente |
| Similaridade habitual preservada, se aplicável | Pendente |
| Rendering/dropped-frame evidência disponível | Pendente |

Nenhum merge, release ou redução substancial de CPU é registado por este documento.
