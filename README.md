# COMPILADOR
Compila a linguagem 2024-2 (algum dia...)

## Especificação léxica (tokens)
A linguagem possui 6 tipos distintos de tokens: palavras reservadas, 
identificadores, constantes (int, float e string) e comentários; Abaixo seguem
suas definições regulares:
* palavra reservada: \[a-z\](\[a-z\] | \[A-Z\]))\*
* identificador: \[ifbs\]_(\[A-Z\] | \[a-z\] \[A-Z\]?) ((\[a-z\] | \[0-9\]) \[A-Z\]?)\*
* constante_int: 0 | \[1-9\] \[0-9\]\*
* constante_float: (0 | \[1-9\] \[0-9\]\*),\[0-9\](0\*\[1-9\]\+)\*
* constante_string: \"(\[\^\n\"%\] | %x)\*\"
* comentário: >@\n\[\^@\]\*\n@<

## Como compilar o compilador
1. Baixar [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases/latest)
   (versão *msvcrt-x86_64*)
2. Descompactar o *.zip* e adicionar o caminho `llvm-mingw...\bin` à variável
   de ambiente *PATH* do seu sistema (ou usuário)
2. Executar `.\build.cmd run` (compila e roda o executável 'compiler_win32.exe')
