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

## Pendências

- [x] Repositório privado e primeiro build no GitHub Actions: 304 imports,
      303 ligados, 1 fraco nulo, 0 faltando; NSP e NRO nos artefatos
- [ ] Primeiro teste no hardware: mandar `debug.log` e `crash.log`; a lista de
      métodos Java "unhandled" do log é a lista de tarefas de `sh_java.c`
- [ ] Conferir o formato pedido ao OpenSL ES (taxa, canais) no log do
      `opensles.c`
- [ ] Conferir o mapeamento de botões (o que `BACK`/`MENU`/D-pad fazem no jogo)
- [ ] Conferir onde o jogo grava o save (`user://`) e se persiste
- [ ] Ícone do launcher: hoje um provisório desenhado pelo port
- [ ] `PORT_NPDM_PROGRAM_ID` (0x01000000000010E4): confirmar que não colide
      com outro port

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
