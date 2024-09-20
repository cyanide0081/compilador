# Compilador 2024-2

Implementação do compilador da linguagem *2024-2*, integrado em uma interface
gráfica simples (nativa para *Windows*) para facilitar a edição e compilação
de pequenos programas.

### Etapas do projeto

- [X] Interface gráfica (*GUI*)
- [X] Analisador léxico (*tokenizer*)
- [ ] Analisador sintático (*parser*)
- [ ] Analisador semântico
- [ ] Gerador de código intermediário (*IL*)

## Especificação léxica (*tokens*)

A linguagem possui sete tipos distintos de *tokens*: palavras reservadas,
símbolos especiais,  identificadores, constantes (*int*, *float* e *string*)
e comentários. Abaixo seguem suas definições e expressões regulares:

* **palavra reservada**: uma letra minúscula, seguida por zero ou mais letras:
```
[a-z]([a-z]|[A-Z])*
```

As palavras reservadas da linguagem são: *main*, *end*, *if*, *elif*, *else*,
*false*, *true*, *read*, write *writeln*, *repeat*, *until*, e *while*. 

* **símbolo especial**: uma das seguintes sequências: _&& || ! == != < >
  \+ \- \* / , ; = ( )_.

* **identificador**: um prefixo, seguido por uma letra e zero ou mais letras
  ou dígitos, desde que não hajam letras maiúsculas consecutivas 
  (os prefixos possíveis são: `i_`, `f_`, `b_` e `s_`):
```
[ifbs]_([A-Z]|[a-z][A-Z]?)(([a-z]|[0-9])[A-Z]?)*
```

* **_constante_int_**: um ou mais dígitos decimais,
  sem zeros desnecessários à esquerda:
```
0|[1-9][0-9]*
```

* **_constante_float_**: uma parte inteira (equivalente a uma *constante_int*),
  seguida por uma vírgula e uma parte fracionária, que é composta por um ou
  mais dígitos decimais, sem zeros desnecessários à direita:
```
(0|[1-9][0-9]*),[0-9](0*[1-9]+)*
```

* **_constante_string_**: duas aspas duplas envolvendo uma sequência qualquer
  de caracteres, desde que a mesma não contenha quebras de linha, aspas duplas
  ou `%` (exceto se for imediatamente seguido por um `x`):
```
\"([^\n\"%]|%x)*\"
```

* **comentário**: `>@`, seguido por uma quebra de linha, uma sequência qualquer
  de caracteres exceto `@`, uma quebra de linha e `@<:`
```
>@\n[^@]*\n@<
```

## Como compilar o compilador

1. Baixar [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases/latest)
   (versão *msvcrt-x86_64* (64-bit) ou *msvcrt-i686* (32-bit));
2. Descompactar o *.zip* e adicionar o caminho `llvm-mingw...\bin` à variável
   de ambiente *PATH* do seu sistema (ou usuário);
2. Executar `.\build.cmd run`
   (compila e roda o executável `compiler_win32.exe`).
