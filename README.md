# COMPILADOR

Implementação do compilador da linguagem 2024-2, acompanhado por uma IDE primitiva.

## Especificação léxica (tokens)

A linguagem possui seis tipos distintos de tokens: palavras reservadas, 
identificadores, constantes (int, float e string) e comentários.
Abaixo seguem suas definiçõe e expressões regulares:

* palavra reservada: uma letra minúscula seguida por zero ou mais letras;
```
[a-z] ([a-z] | [A-Z])*
```

* identificador: um prefixo, seguido por uma letra e zero ou mais letras
ou dígitos, desde que não hajam letras maiúsculas consecutivas 
(os prefixos possíveis são: i_, f_, b_ e s_):
```
[ifbs]_ ([A-Z] | [a-z] [A-Z]?) (([a-z] | [0-9]) [A-Z]?)*
```

* constante_int: um ou mais dígitos decimais, sem zeros desnecessários à esquerda:
```
0 | [1-9] [0-9]*
```

* constante_float: uma parte inteira (equivalente a uma constante_int) seguida por
uma vírgula e uma parte fracionária, que é composta por um ou mais dígitos decimais, 
sem zeros desnecessários à direita:
```
(0 | [1-9] [0-9]*),[0-9] (0*[1-9]+)*
```

* constante_string: duas aspas duplas envolvendo uma sequência qualquer de caracteres,
desde que a mesma não contenha quebras de linha, aspas duplas e % (exceto se for seguido por um x):
```
\"([^\n\"%] | %x)*\"
```
* comentário: >@, seguido uma quebra de linha, uma sequência qualquer de caracteres exceto @,
uma quebra de linha e @<:
```
>@\n[^@]*\n@<
```

## Como compilar o compilador

1. Baixar [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases/latest)
   (versão *msvcrt-x86_64*)
2. Descompactar o *.zip* e adicionar o caminho `llvm-mingw...\bin` à variável
   de ambiente *PATH* do seu sistema (ou usuário)
2. Executar `.\build.cmd run` (compila e roda o executável 'compiler_win32.exe')
