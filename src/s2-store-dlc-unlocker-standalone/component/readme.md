# UWP / MS Store DLC Unlocker Guide

Este documento detalha o funcionamento interno do mecanismo de desbloqueio de DLCs para jogos UWP (Universal Windows Platform) via MS Store, especificamente para títulos como Call of Duty: WWII, implementado através da interceptação das APIs do GDK (`XPackage` e `XStore`).

## O Problema das DLCs em UWP (MS Store)
Ao contrário do Steam, onde os jogos conseguem ler livremente ficheiros adicionais colocados nas suas pastas, o ambiente UWP é gerido a nível de Kernel pelo `appxsvc` e `GamingServices`. 
O fluxo original de um jogo para aceder a uma DLC na MS Store é o seguinte:
1. O jogo enumera os pacotes instalados (`XPackageEnumeratePackages`).
2. Descobre um pacote associado ao jogo base (ex: `DLC 01 PC MS`).
3. O jogo pede ao sistema operativo para montar fisicamente o pacote (`XPackageMountWithUiAsync`).
4. **BLOQUEIO:** O sistema operativo verifica a licença associada à conta Microsoft do utilizador. Se o utilizador não possuir a licença desse pacote, o OS recusa a montagem e devolve o erro `803F8001`.
5. O jogo lê o estado da operação assíncrona (`XGetAsyncStatus`), vê a falha, e aborta o acesso ao conteúdo da DLC.

## A Solução (Spoofing Completo de Montagem)
Para desbloquear as DLCs, não podemos forçar o Windows a montar um pacote sem licença. A única alternativa é **fazer Bypass à chamada do Sistema Operativo e simular o sucesso para o motor do jogo.**

Na nossa implementação, interceptamos as seguintes funções do VTable da interface `XPackage`:

### 1. `XPackageMountWithUiAsync` (Interceptação do Pedido)
* **Objetivo:** Impedir que o pedido de montagem chegue ao Windows.
* **Ação:** Quando o jogo pede para montar um pacote que não seja o jogo base, nós cortamos a chamada original. Em vez disso, criamos uma "Fake Thread" que chama imediatamente a rotina de *Callback* do jogo.
* **Resultado:** O bloco assíncrono (`XAsyncBlock`) nunca é submetido ao OS, pelo que nunca recebe o código de erro `803F8001`. O jogo assume que a operação ocorreu sem erros.

### 2. `XPackageMountWithUiResult` (Forjar o Handle de Montagem)
* **Objetivo:** Entregar um Handle válido ao jogo após o sucesso simulado.
* **Ação:** Injetamos um pointer estático/dinâmico arbitrário (`malloc(0x10)`) e guardamos esse ponteiro numa lista interna de Handles Falsos (`fake_mount_handles`).
* **Resultado:** O motor do jogo recebe um *Mount Handle* e assume que pode começar a interrogar sobre os ficheiros.

### 3. `XPackageGetMountPathSize` & `XPackageGetMountPath` (Redirecionamento de Ficheiros)
* **Objetivo:** Enganar o jogo sobre a localização física dos ficheiros da DLC.
* **Ação:** Quando o jogo tenta obter o diretório associado ao nosso Handle Falso, a nossa DLL interceta a chamada e devolve o caminho absoluto da pasta do próprio jogo (`GetModuleFileNameA`).
* **Resultado:** O jogo procura os ficheiros da DLC dentro da pasta do executável principal.

## Conclusão e Peculiaridades de Call of Duty: WWII
Neste jogo em específico, a versão descarregada da MS Store (pesando mais de 100GB) já inclui nativamente **todos** os assets, mapas e zonas (pastas `zone`, `video`, etc.) de todas as DLCs existentes.
Os pacotes DLC instalados separadamente via MS Store são minúsculos (apenas alguns KBs) e servem unicamente como **"Activadores"**.

Graças ao nosso Hook, como o jogo acredita que o "Activador" foi montado com sucesso no diretório base, a lógica interna do motor do jogo prossegue para ler os ficheiros `.ff` e `.pak` que **já se encontravam instalados**, libertando acesso ao Season Pass, novos mapas multiplayer (como Carentan) e modos extra, sem necessidade de transferir ou copiar ficheiros manualmente!
