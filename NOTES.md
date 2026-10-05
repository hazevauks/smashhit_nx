# smashhit_nx — notas do port

Port de **Smash Hit 1.5.14** (com.mediocre.smashhit, versionCode 1051400,
armeabi-v7a) para Nintendo Switch sobre o runtime
[android32](https://github.com/aks796/android32) (submódulo em `runtime/`,
commit `50b352c`).

Estado: **compila no GitHub Actions (primeiro build limpo, sem avisos); ainda não
testado no hardware.** Build em `.github/workflows/build.yml`; nesta máquina não
há Docker, Python nem compilador.

## O jogo

| | |
| --- | --- |
| Motor | próprio da Mediocre ("Qi": `QiRenderer`, `QiAudio`, `QiInput`), com Lua, libpng, libjpeg, Vorbis e libc++ embutidos (`libsmashhit.so`, 2,3 MB, Thumb-2) |
| Entrada no Android | `GameActivity` do Google (AGDK) + `android_main` — não é `NativeActivity` |
| Gráficos | OpenGL ES 2 (69 funções `gl*`) com EGL próprio (`Renderer::initContext`) |
| Áudio | OpenSL ES apenas (`QiAudioDeviceOpenSl`: engine, output mix, um player com buffer queue) |
| `DT_NEEDED` | libc, libm, libdl, liblog, libandroid, libnativewindow, libEGL, libGLESv2, libOpenSLES — só bibliotecas do sistema |
| Imports | 373; 69 `gl*`, 303 ligados, 1 fraco deixado nulo (`__cxa_thread_atexit_impl`) |
| Símbolos | 5 955 exportados (C++ com nomes) |
| ELF | relocações REL simples, só `DT_GNU_HASH` (sem `DT_HASH`), 3 `PT_LOAD`, 11 construtores |
| Outras libs | `libcrashlytics*.so`, `libdatastore_shared_counter.so` — não são carregadas |
| Assets | 2 421 arquivos em `assets/`, **todos armazenados sem compressão** (por isso os nomes `*.mp3`) |

**Atenção:** o APK também traz `arm64-v8a`. O guia da comunidade manda esses
jogos pela rota 64 bits; este port usa a biblioteca `armeabi-v7a` de propósito
(decisão do autor do port).

O runtime cobria todos os imports menos 31: 13 entram como `PASSTHROUGH` em
`tools/imports.cfg`, os demais têm shim em `source/sh_libc.c` (os `_chk` do
FORTIFY, `stdin`/`stdout`/`stderr`, `__register_atfork`,
`__android_log_assert`) e `source/sh_assets.c` (`AAssetManager`).

## Sequência de inicialização (do Java, classes2.dex / classes3.dex)

1. `MainActivity.onCreate` → `jniCrashlyticsInit()` (nativo vazio)
2. `GameActivity.onCreate` → `System.loadLibrary("smashhit")` → construtores
3. `initializeNativeCode(filesDir, obbDir, externalFilesDir, assets, savedState)`
   → `GameActivity_register` (RegisterNatives dos outros nativos), pipe no
   looper da thread de UI, thread do app rodando `android_main`
4. `onStartNative`, `onResumeNative`, `onSurfaceCreatedNative`,
   `onSurfaceChangedNative(fmt, w, h)`, `onWindowFocusChangedNative(true)`
5. por evento: `onTouchEventNative(handle, MotionEvent)`,
   `onKeyDownNative` / `onKeyUpNative(handle, KeyEvent)`
6. saída: `onPauseNative`, `onStopNative`, `onSurfaceDestroyedNative`,
   `terminateNativeCode`

Não há `JNI_OnLoad`. A thread principal do port faz o papel da thread de UI
(`source/sh_activity.c`); o jogo roda na thread do glue
(`android_main` → `Renderer::handleInput` / `render` → `eglSwapBuffers`).

## Leitura de arquivos

`QiFileInputStream::open` (desmontado): `AAssetManager_open` →
`AAsset_openFileDescriptor(&start, &length)` → `dup` → `fdopen("rb")` →
`close` → `fseek(start)`. Ou seja, o motor lê os assets direto de dentro do
APK por descritor de arquivo. `sh_assets.c` indexa o diretório central uma
vez e devolve um descritor do próprio APK (pelo `open` do runtime, com o
cache de APK) e a posição do arquivo. Nada é extraído.

## Canal Java: `MainActivity.command(String)`

Tudo o que o motor pede ao Java passa por `JavaMessenger::sendCommand` →
`command("nome arg arg")` → `CommandHandler.handleCommand`, que responde
texto. A tabela completa (38 comandos) está em `source/sh_command.c`,
respondida como um telefone offline e sem login: loja indisponível, nada
comprado, sem anúncios, sem Play Games, remote config nunca buscado.
**Nada do que o jogo vende é liberado** (`isproductowned` → `false`).

## Entrada

- Toque: `MotionEvent` lido pelo glue por JNI (`getAction`, `getPointerId`,
  `getAxisValue(axis, i)`…); posições em pixels da janela. `handleInput`
  trata DOWN/POINTER_DOWN, UP/POINTER_UP e MOVE.
- Teclas que `handleInput` conhece (tabela de saltos em +0x184):
  `DPAD_UP/DOWN/LEFT/RIGHT` → botões 6/7/4/5 do `QiInput`, `DPAD_CENTER` e
  `BUTTON_A` → 8, `L1`/`L2`/`R1`/`R2` → 12/13/14/15, `BACK` → 16, `MENU` → 17.
- O motor não tem analógico: o port desenha um ponteiro (mira) movido pelo
  analógico e A/ZR/ZL tocam onde ele está (`source/sh_input.c`). A mira é
  desenhada com `glScissor` + `glClear`, sem tocar em programa, buffers ou
  texturas do jogo.
- `[game] tv_mode` responde `istv` → `true` (modo Android TV do jogo):
  experimental, a testar.

## Achados dos testes no hardware

- **Execução 1 (build 202610041910):** o motor carrega, registra os 21 nativos
  do `GameActivity`, cria o contexto GLES 2 (nouveau, Mesa 20.1), abre o OpenSL
  (44,1 kHz estéreo 16 bits, blocos de 4096 bytes) e começa a ler o APK. Crash
  na thread de áudio: `QiAudio::fillBuffer` reserva 128 KB de pilha na entrada
  (`sub sp, #0x20000`) e a thread do `opensles.c` do runtime tem 64 KB. Corrigido
  sem copiar o arquivo: `build/rt/opensles.o` é compilado com `threadCreate`
  trocado por `sh_audio_thread_create` (`source/sh_audio.c`), que dá 1 MB.
- **Execução 2 (build 202610041921):** a correção da pilha entrou (`[audio] the
  OpenSL thread: stack 1024 KB instead of 64 KB`); o jogo abre, chega ao menu,
  toca som e fecha limpo pelo próprio `quit` (onPause → onStop →
  surfaceDestroyed → terminateNativeCode, sem acionar o guarda de 5 s).
  Nenhuma linha `[jni] unhandled`: a tabela de `sh_java.c` cobre tudo o que o
  motor chamou. OpenSL: 44,1 kHz estéreo 16 bits, reamostrado para 48 kHz.
  507 aberturas de asset, 189 "não encontradas": o motor procura cada arquivo em
  várias pastas em sequência (ruído, não erro). Heap em uso: 271 MB.
  Quadros longos só no carregamento inicial (quadros 21-71, 270-670 ms).
- **Execução 3 (build 202610041936), ~170 s jogando:** 60 fps estáveis depois do
  carregamento (59,3-60,1 nos relatórios de 10 s), áudio a 44,1 kHz com 0
  underruns e 0 envios falhos em 139 s, heap estável em ~310-320 MB, 23 objetos
  Java (sem vazamento). O jogo pausa (`popup_shown type pause`) e manda placar
  (`updateleaderboard`, descartado). Sensores de movimento prontos nos três
  tipos de controle; o giroscópio funciona, mas **o eixo Y saiu invertido** no
  modo portátil com os sinais do port de Angry Birds Space: invertido no
  código, e `gyro_invert_x` / `gyro_invert_y` no config.ini para qualquer
  controle que leia diferente.
- **Execução 4 (build 202610050016), ~150 s:** a atualização pelo NRO funcionou
  (1936 → 0016, reinício sozinho) e o config.ini recebeu as 2 opções novas. Eixo
  Y do giroscópio correto (confirmado pelo autor). 60 fps, áudio com 0
  underruns, heap 300-314 MB, 21-23 objetos Java; nenhum `unhandled`, nenhum
  comando desconhecido, nenhum erro de GL. Um quadro de 247 ms ao chegar a um
  checkpoint (carga do trecho seguinte). O log termina sem a sequência de
  saída e sem nenhuma linha de foco perdido: fechar pelo menu HOME congela o
  processo e o encerra, como nos outros ports — o caminho de pausa/retomada
  (`onPauseNative` / `onResumeNative`) **ainda não apareceu em log nenhum**.
- IDs de programa vistos em outros ports no GitHub: 100E, 100F, 1010, 1015,
  10D7, 1F1A. O 10E4 deste port não colide com nenhum deles (a busca só
  alcança repositórios públicos indexados).
- **Giroscópio:** o jogo não usa sensor nenhum (nenhum import `ASensor`), então
  só entra pelo port, movendo a mira: `hidGetSixAxisSensorHandles` /
  `hidGetSixAxisSensorStates` (portátil, Pro Controller, par de Joy-Cons), com
  os eixos e sinais do port de Angry Birds Space (`abs_cursor.c`), já provados
  em hardware. Clique do analógico direito liga/desliga; Y recentra.
  `[controls] gyro_pointer` e `gyro_speed` no config.ini. **A testar.**
- O motor também pergunta `isphone`, que o `CommandHandler` do Java não tem
  (responde `""`); está na tabela só para não poluir o log.
- `dlopen(libcrashlytics.so)` falha e o motor segue (o Crashlytics fica desligado).

## Pendências

- [x] Repositório privado e primeiro build no GitHub Actions: 304 imports,
      303 ligados, 1 fraco nulo, 0 faltando; NSP e NRO nos artefatos
- [ ] Primeiro teste no hardware: mandar `debug.log` e `crash.log`; a lista de
      métodos Java "unhandled" do log é a lista de tarefas de `sh_java.c`
- [x] Formato pedido ao OpenSL ES: 44,1 kHz, estéreo, 16 bits (execução 2)
- [ ] Giroscópio: confirmar o eixo Y corrigido no portátil; testar Pro
      Controller e par de Joy-Cons soltos (sentido, velocidade, deriva)
- [x] Desempenho: 60 fps e áudio sem underruns (execução 3)
- [ ] Conferir o mapeamento de botões (o que `BACK`/`MENU`/D-pad fazem no jogo)
- [x] Save: a versão gratuita não guarda progresso (os checkpoints são do
      premium, que não é liberado). Falta só ver o que ela grava em `data/`
      (config, recorde): `dcr_path_traced` agora registra os acessos a essa pasta
- [x] Ícone do launcher: o do jogo, fornecido pelo autor do port (256x256, sem
      metadados); o README diz que é arte da Mediocre, fora da licença MIT
- [ ] `PORT_NPDM_PROGRAM_ID` (0x01000000000010E4): confirmar que não colide
      com outro port

## Release 0.1.0 (processo, como no dantheman_nx)

- Identidade dos commits: `334693818+hazevauks@users.noreply.github.com`
  (config local do repositório). O histórico de antes da release foi reescrito
  com `git filter-branch --env-filter` (só autor e committer; árvore idêntica,
  `7f3a192`); o original está em `_refs/smashhit_nx-pre-public.bundle`
  (ignorado pelo git).
- Conferido antes de publicar: nenhum e-mail pessoal nem nome real em arquivos
  rastreados, mensagens de commit ou no ícone.
- Destino: o repositório privado vira `smashhit_nx-private` (remoto
  `private-archive`) com o histórico reescrito; um `smashhit_nx` público novo
  recebe `main`, a tag `v0.1.0` e a release (zip do cartão SD + NRO).

## Ferramentas (`tools/`)

Scripts Perl (vindos do dantheman_nx), no lugar de binutils/Python:

- `elfinfo.pl <lib.so> [needed|exports|imports|jni|all]`
- `dexinfo.pl <classes.dex> <regex de classe> [native | code [regex de método]]`
- `thumbcalls.pl <lib.so> <regex de símbolo | @0xENDEREÇO:BYTES>` — o que uma
  função Thumb chama; o modo por endereço serve para funções sem símbolo
- `thumbxref.pl <lib.so> <regex>` — quem chama uma função ou um import (pela PLT)
- `imports_needed.txt` — os símbolos que o jogo importa (nomes, não conteúdo do jogo)

Os dois últimos foram ajustados para bibliotecas só com `DT_GNU_HASH`.

## O que nunca vai para o repositório

O APK e a pasta extraída dele (`smash-hit-*/`) estão no `.gitignore`.
