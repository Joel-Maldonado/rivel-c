(function_declaration body: (block "{" (_)* @function.inside "}")) @function.around
(struct_declaration "{" (_)* @class.inside "}") @class.around
(line_comment)+ @comment.around
(block_comment) @comment.around
