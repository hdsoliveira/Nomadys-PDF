# Nomadys-PDF

Leitor de PDF minimalista e rápido para Linux (Wayland/X11), controlado por
teclado no estilo vim. Ao abrir, mostra apenas o PDF.

- Motor: [PDFium](https://pdfium.googlesource.com/pdfium/) (o do Chrome),
  binário pré-compilado de
  [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries),
  release `chromium/8066`, em `third_party/pdfium/` (SHA-256 do pacote
  `0b43f405477cf2cfc4dbff06905093c3309756c6bca1fb9da99234a2ca97fed2`).
  Build sem V8: formulários funcionam, mas JavaScript de formulário (cálculos
  automáticos de campos) não é executado.
- Janela e teclado: SDL3. Texto da interface: fonte bitmap embutida
  (DejaVu Sans Mono, gerada por `tools/genfont.py`).

## Compilar e instalar

Dependência do sistema: `sudo apt install libsdl3-dev`. O PDFium é baixado
automaticamente no primeiro `make` por `tools/fetch_pdfium.sh` (versão fixa,
checksum SHA-256 conferido) e não fica no repositório.

```sh
make                 # gera ./nomadys-pdf
make install         # ~/.local/bin/nomadys-pdf e ~/.local/lib/nomadys-pdf/libpdfium.so
make uninstall
```

## Uso

```sh
nomadys-pdf arquivo.pdf [outro.pdf ...]    # cada arquivo abre numa aba
```

| Tecla | Ação |
|---|---|
| `j` `k` `h` `l`, setas | rolar |
| `Ctrl-d` `Ctrl-u` / `Ctrl-f` `Ctrl-b`, `Espaço` | meia tela / tela inteira |
| `J` `K` | página seguinte / anterior |
| `gg` `G` `42G` (ou `:42`) | início / fim / página 42 |
| `+` `-` `=` `z` | zoom / ajustar à largura / página inteira (também `Ctrl` + roda) |
| `d` | duas páginas lado a lado |
| `i` | modo escuro (imagens mantêm as cores) |
| `Ctrl-j` `Ctrl-k` | próximo / anterior campo de formulário (também `Tab`) |
| em um campo | digitar; `Espaço` marca checkbox; `Ctrl-a` `Ctrl-z` `Ctrl-v`; `Esc` sai |
| `v` | modo cursor: `h j k l w b 0 $`, `v` seleciona, `H` destaca, `y` copia, `Esc` sai |
| `/` `n` `N` | buscar / próxima / anterior (`Esc` limpa) |
| `t` | mostrar/esconder abas |
| `Ctrl-,` `Ctrl-.` | aba anterior / seguinte |
| `:e arquivo` | abrir em nova aba (`Tab` completa o caminho) |
| `Ctrl-n` | nova janela do mesmo documento (edições compartilhadas) |
| `Ctrl-s`, `:w` | salvar |
| `:q` / `q` | fechar aba / fechar janela |

Números antes de um comando repetem-no (`5j`, `3n`).

## Salvamento

Por padrão, formulários e destaques são salvos automaticamente ~1 s depois da
alteração (e ao sair do campo, e ao fechar). O salvamento é incremental: o
conteúdo original é preservado e as mudanças são anexadas; o arquivo é escrito
em um temporário e substituído atomicamente. Com `autosave = false`, salve com
`Ctrl-s`/`:w`; fechar com alterações pendentes pede confirmação.

## Configuração

`~/.config/nomadys-pdf/config` (veja `config.example`):

```ini
theme = dark              # dark | light
autosave = true
autosave_delay_ms = 1000
highlight_color = #FFE14D
zoom = fit-width          # ou porcentagem, ex. 125
background_light = #D9D9D9
background_dark = #101010
dark_contrast_low = 28    # luminância mínima/máxima no modo escuro (0-255)
dark_contrast_high = 228
```

## Modo escuro

Inverte a luminosidade mantendo o tom das cores (vermelho continua vermelho),
exceto nas regiões de imagens raster e gradientes, que ficam intactas. Uma
imagem que cobre quase a página inteira (PDF escaneado) é invertida junto.
Gráficos vetoriais (plots em PDF) escurecem com o fundo, mas mantêm os tons.

## Testes

`tools/make_test_pdf.py` gera `tests/sample.pdf` (texto, imagem, formulário).
Modo de teste sem janela, com teclas simuladas e capturas de tela:

```sh
SDL_VIDEODRIVER=offscreen ./nomadys-pdf --test-out /tmp/shots \
    --test-script '<C-j>texto<Esc><shot:form>vjw<shot:caret>' copia.pdf
```

Tokens: caracteres comuns, `<Esc>`, `<Enter>`, `<Tab>`, `<BS>`, setas,
`<C-x>` (Ctrl), `<S-Tab>`, `<wait:ms>`, `<shot:nome>` (salva `nome.bmp`).

## Licenças

Código do Nomadys-PDF: MIT (`LICENSE`). PDFium: BSD-3/Apache-2.0
(`third_party/pdfium/LICENSE` e `licenses/`). DejaVu fonts: licença Bitstream
Vera / domínio público.
