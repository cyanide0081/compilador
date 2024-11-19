# Compilador 2024-2

Implementação do compilador da linguagem *2024-2*, integrado em uma interface
gráfica simples (nativa para *Windows*) para facilitar a edição e compilação
de pequenos programas.

### Etapas do projeto

- [X] Interface gráfica (*GUI*)
- [X] Analisador léxico (*tokenizer*)
- [X] Analisador sintático (*parser*)
- [X] Analisador semântico (*checker*)
- [ ] Gerador de código intermediário (*MSIL/CIL*)

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

## Especificação sintática (*gramática*)

A gramática da linguagem é relativamente simples, com a regra inicial sendo o
ponto de entrada do programa, e este contendo uma lista não vazia de instruções,
que podem ser comandos ou declarações de variáveis.

Estas podem ser números inteiros, reais, cadeias de texto ou valores lógicos,
enquanto os comandos incluem entrada e saída, condicionais, laços e atribuições,
que permitem atribuir expressões à variáveis e condições de comandos.

As expressões da linguagem consistem em operações binárias e unárias em
operandos, que podem ser identificadores ou literais. 

Segue abaixo a especificação da gramática em BNF, que foi fatorada para
possibilitar a implementação de um analisador LL(1):

```
<inicio> ::= main <lista_instr> end ;
<lista_instr> ::= <instrucao> ";" <lista_instr_rep>;
<lista_instr_rep> ::= <lista_instr> | î ;
<instrucao> ::= <dec_ou_atr> | <cmd_entr> | <cmd_saida> | <cmd_rep> | <cmd_sel> ;
<dec_ou_atr> ::= <lista_id> <atr_opt> ;
<atr_opt> ::= "=" <expr> | î ;

<lista_id> ::= identificador <lista_id_mul> ;
<lista_id_mul> ::= "," <lista_id> | î ;

<cmd> ::= <cmd_atr> | <cmd_entr> | <cmd_saida> | <cmd_rep> | <cmd_sel> ;

<cmd_atr> ::= <lista_id> "=" <expr> ;

<cmd_entr> ::= read "(" <lista_entr> ")" ;
<lista_entr> ::= <cte_str_opt> identificador <lista_entr_mul> ;
<lista_entr_mul> ::= "," <lista_entr> | î ;
<cte_str_opt> ::= constante_string "," | î ;

<cmd_saida> ::= <cmd_saida_tipo> "(" <lista_expr> ")" ;
<cmd_saida_tipo> ::= write | writeln ;
<lista_expr> ::= <expr> <lista_expr_mul> ;
<lista_expr_mul> ::= "," <lista_expr> | î ;

<cmd_sel> ::= if <expr> <lista_cmd> <elif> <else> end ;
<elif> ::= elif <expr> <lista_cmd> <elif> | î ;
<else> ::= else <lista_cmd> | î ;
<lista_cmd> ::= <cmd> ";" <lista_cmd_mul> ;
<lista_cmd_mul> ::= <lista_cmd> | î ;

<cmd_rep> ::= repeat <lista_cmd> <cmd_rep_tipo> <expr> ;
<cmd_rep_tipo> ::= while | until ;

<expr> ::= <elemento> <expr1> ; 
<expr1> ::= î | "&&" <elemento> <expr1> | "||" <elemento> <expr1> ; 
<elemento> ::= <relacional> | true | false | "!" <elemento> ; 
<relacional> ::= <aritmetica> <relacional1> ; 
<relacional1> ::= î | <operador_relacional> <aritmetica> ; 
<operador_relacional> ::= "==" | "!=" | "<" | ">" ; 
<aritmetica> ::= <termo> <aritmetica1> ; 
<aritmetica1> ::= î | "+" <termo> <aritmetica1> | "-" <termo> <aritmetica1> ; 
<termo> ::= <fator> <termo1> ; 
<termo1> ::= î | "*" <fator> <termo1> | "/" <fator> <termo1> ; 
<fator> ::= identificador | constante_int | constante_float | constante_string |
    "(" <expr> ")" |  "+" <fator> | "-" <fator> ;  
```
    
## Como compilar o compilador

1. Baixar [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases/latest)
   (versão *msvcrt-x86_64* (64-bit) ou *msvcrt-i686* (32-bit));
2. Descompactar o *.zip* e adicionar o caminho `llvm-mingw...\bin` à variável
   de ambiente *PATH* do seu sistema (ou usuário);
2. Executar `.\build.cmd run`
   (compila e roda o executável `compiler_win32.exe`).
