(identifier) @variable
(type_identifier) @type
((type_identifier) @type.builtin (#match? @type.builtin "^(int|float|bool|str|void)$"))
"list" @type.builtin
(function_declaration name: (identifier) @function)
(parameter name: (identifier) @variable.parameter)
(field_declaration name: (identifier) @property)
(member_expression property: (identifier) @property)
(call_expression function: (identifier) @function)
(call_expression function: (member_expression property: (identifier) @function))
(argument label: (identifier) @property)
(self) @variable.special
(number) @number
(boolean) @boolean
(null) @constant.builtin
(string) @string
(formatted_string) @string
(interpolation "{" @punctuation.special "}" @punctuation.special)
(interpolation (identifier) @variable)
(escape_sequence) @string.escape
(line_comment) @comment
(block_comment) @comment
["func" "struct" "if" "else" "while" "for" "in" "break" "continue" "return"] @keyword
["+" "-" "*" "/" "%" "&" "|" "^" "~" "<<" ">>" "==" "!=" "<" "<=" ">" ">=" "&&" "||" "!" "=" "+=" "-=" "*=" "/=" "%=" ":=" "??" ".." "..=" "->" "?"] @operator
["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["," ";" ":" "."] @punctuation.delimiter
