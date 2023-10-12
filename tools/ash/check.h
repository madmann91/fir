#pragma once

struct ast;
struct type_set;
struct log;

void check_program(struct ast*, struct type_set*, struct log*);
