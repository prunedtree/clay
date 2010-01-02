#include "clay.hpp"
#include <cstdlib>
#include <climits>
#include <cassert>

static vector<TokenPtr> *tokens;
static int position;
static int maxPosition;

static bool next(TokenPtr &x) {
    if (position == (int)tokens->size())
        return false;
    x = (*tokens)[position];
    if (position > maxPosition)
        maxPosition = position;
    ++position;
    return true;
}

static int save() {
    return position;
}

static void restore(int p) {
    position = p;
}

static LocationPtr currentLocation() {
    if (position == (int)tokens->size())
        return NULL;
    return (*tokens)[position]->location;
}



//
// symbol, keyword
//

static bool symbol(const char *s) {
    TokenPtr t;
    if (!next(t) || (t->tokenKind != T_SYMBOL))
        return false;
    return t->str == s;
}

static bool keyword(const char *s) {
    TokenPtr t;
    if (!next(t) || (t->tokenKind != T_KEYWORD))
        return false;
    return t->str == s;
}



//
// identifier, dottedName
//

static bool identifier(IdentifierPtr &x) {
    LocationPtr location = currentLocation();
    TokenPtr t;
    if (!next(t) || (t->tokenKind != T_IDENTIFIER))
        return false;
    x = new Identifier(t->str);
    x->location = location;
    return true;
}

static bool dottedName(DottedNamePtr &x) {
    LocationPtr location = currentLocation();
    DottedNamePtr y = new DottedName();
    IdentifierPtr ident;
    if (!identifier(ident)) return false;
    y->parts.push_back(ident);
    while (true) {
        int p = save();
        if (!symbol(".") || !identifier(ident)) {
            restore(p);
            break;
        }
        y->parts.push_back(ident);
    }
    x = y;
    x->location = location;
    return true;
}



//
// literals
//

static bool boolLiteral(ExprPtr &x) {
    LocationPtr location = currentLocation();
    int p = save();
    if (keyword("true"))
        x = new BoolLiteral(true);
    else if (restore(p), keyword("false"))
        x = new BoolLiteral(false);
    else
        return false;
    x->location = location;
    return true;
}

static bool intLiteral(ExprPtr &x) {
    LocationPtr location = currentLocation();
    TokenPtr t;
    if (!next(t) || (t->tokenKind != T_INT_LITERAL))
        return false;
    TokenPtr t2;
    int p = save();
    if (next(t2) && (t2->tokenKind == T_LITERAL_SUFFIX)) {
        x = new IntLiteral(t->str, t2->str);
    }
    else {
        restore(p);
        x = new IntLiteral(t->str);
    }
    x->location = location;
    return true;
}

static bool floatLiteral(ExprPtr &x) {
    LocationPtr location = currentLocation();
    TokenPtr t;
    if (!next(t) || (t->tokenKind != T_FLOAT_LITERAL))
        return false;
    TokenPtr t2;
    int p = save();
    if (next(t2) && (t2->tokenKind == T_LITERAL_SUFFIX)) {
        x = new FloatLiteral(t->str, t2->str);
    }
    else {
        restore(p);
        x = new FloatLiteral(t->str);
    }
    x->location = location;
    return true;
}

static bool charLiteral(ExprPtr &x) {
    LocationPtr location = currentLocation();
    TokenPtr t;
    if (!next(t) || (t->tokenKind != T_CHAR_LITERAL))
        return false;
    x = new CharLiteral(t->str[0]);
    x->location = location;
    return true;
}

static bool stringLiteral(ExprPtr &x) {
    LocationPtr location = currentLocation();
    TokenPtr t;
    if (!next(t) || (t->tokenKind != T_STRING_LITERAL))
        return false;
    x = new StringLiteral(t->str);
    x->location = location;
    return true;
}

static bool literal(ExprPtr &x) {
    int p = save();
    if (boolLiteral(x)) return true;
    if (restore(p), intLiteral(x)) return true;
    if (restore(p), floatLiteral(x)) return true;
    if (restore(p), charLiteral(x)) return true;
    if (restore(p), stringLiteral(x)) return true;
    return false;
}



//
// expression misc
//

static bool expression(ExprPtr &x);

static bool optExpression(ExprPtr &x) {
    int p = save();
    if (!expression(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}

static bool expressionList(vector<ExprPtr> &x) {
    ExprPtr a;
    if (!expression(a)) return false;
    x.clear();
    x.push_back(a);
    while (true) {
        int p = save();
        if (!symbol(",") || !expression(a)) {
            restore(p);
            break;
        }
        x.push_back(a);
    }
    return true;
}

static bool optExpressionList(vector<ExprPtr> &x) {
    int p = save();
    if (!expressionList(x)) {
        restore(p);
        x.clear();
    }
    return true;
}



//
// atomic expr
//

static bool arrayExpr(ExprPtr &x) {
    LocationPtr location = currentLocation();
    if (!symbol("[")) return false;
    ArrayPtr y = new Array();
    if (!expressionList(y->args)) return false;
    if (!symbol("]")) return false;
    x = y.ptr();
    x->location = location;
    return true;
}

static bool tupleExpr(ExprPtr &x) {
    LocationPtr location = currentLocation();
    if (!symbol("(")) return false;
    TuplePtr y = new Tuple();
    if (!expressionList(y->args)) return false;
    if (!symbol(")")) return false;
    x = y.ptr();
    x->location = location;
    return true;
}

static bool nameRef(ExprPtr &x) {
    LocationPtr location = currentLocation();
    IdentifierPtr a;
    if (!identifier(a)) return false;
    x = new NameRef(a);
    x->location = location;
    return true;
}

static bool atomicExpr(ExprPtr &x) {
    int p = save();
    if (arrayExpr(x)) return true;
    if (restore(p), tupleExpr(x)) return true;
    if (restore(p), nameRef(x)) return true;
    if (restore(p), literal(x)) return true;
    return false;
}



//
// suffix expr
//

static bool indexingSuffix(ExprPtr &x) {
    LocationPtr location = currentLocation();
    if (!symbol("[")) return false;
    IndexingPtr y = new Indexing(NULL);
    if (!expressionList(y->args)) return false;
    if (!symbol("]")) return false;
    x = y.ptr();
    x->location = location;
    return true;
}

static bool callSuffix(ExprPtr &x) {
    LocationPtr location = currentLocation();
    if (!symbol("(")) return false;
    CallPtr y = new Call(NULL);
    if (!optExpressionList(y->args)) return false;
    if (!symbol(")")) return false;
    x = y.ptr();
    x->location = location;
    return true;
}

static bool fieldRefSuffix(ExprPtr &x) {
    LocationPtr location = currentLocation();
    if (!symbol(".")) return false;
    IdentifierPtr a;
    if (!identifier(a)) return false;
    x = new FieldRef(NULL, a);
    x->location = location;
    return true;
}

static bool tupleRefSuffix(ExprPtr &x) {
    LocationPtr location = currentLocation();
    if (!symbol(".")) return false;
    TokenPtr t;
    if (!next(t) || (t->tokenKind != T_INT_LITERAL))
        return false;
    char *b = (char *)(t->str.c_str());
    char *end = b;
    long c = strtol(b, &end, 0);
    assert(*end == 0);
    x = new TupleRef(NULL, (int)c);
    x->location = location;
    return true;
}

static bool dereferenceSuffix(ExprPtr &x) {
    LocationPtr location = currentLocation();
    if (!symbol("^")) return false;
    x = new UnaryOp(DEREFERENCE, NULL);
    x->location = location;
    return true;
}

static bool suffix(ExprPtr &x) {
    int p = save();
    if (indexingSuffix(x)) return true;
    if (restore(p), callSuffix(x)) return true;
    if (restore(p), fieldRefSuffix(x)) return true;
    if (restore(p), tupleRefSuffix(x)) return true;
    if (restore(p), dereferenceSuffix(x)) return true;
    return false;
}

static void setSuffixBase(Expr *a, ExprPtr base) {
    switch (a->objKind) {
    case INDEXING : {
        Indexing *b = (Indexing *)a;
        b->expr = base;
        break;
    }
    case CALL : {
        Call *b = (Call *)a;
        b->expr = base;
        break;
    }
    case FIELD_REF : {
        FieldRef *b = (FieldRef *)a;
        b->expr = base;
        break;
    }
    case TUPLE_REF : {
        TupleRef *b = (TupleRef *)a;
        b->expr = base;
        break;
    }
    case UNARY_OP : {
        UnaryOp *b = (UnaryOp *)a;
        assert(b->op == DEREFERENCE);
        b->expr = base;
        break;
    }
    default :
        assert(false);
    }
}

static bool suffixExpr(ExprPtr &x) {
    if (!atomicExpr(x)) return false;
    while (true) {
        int p = save();
        ExprPtr y;
        if (!suffix(y)) {
            restore(p);
            break;
        }
        setSuffixBase(y.ptr(), x);
        x = y;
    }
    return true;
}



//
// prefix expr
//

static bool addressOfExpr(ExprPtr &x) {
    LocationPtr location = currentLocation();
    if (!symbol("&")) return false;
    ExprPtr a;
    if (!suffixExpr(a)) return false;
    x = new UnaryOp(ADDRESS_OF, a);
    x->location = location;
    return true;
}

static bool plusOrMinus(int &op) {
    int p = save();
    if (symbol("+"))
        op = PLUS;
    else if (restore(p), symbol("-"))
        op = MINUS;
    else
        return false;
    return true;
}

static bool signExpr(ExprPtr &x) {
    LocationPtr location = currentLocation();
    int op;
    if (!plusOrMinus(op)) return false;
    ExprPtr b;
    if (!suffixExpr(b)) return false;
    x = new UnaryOp(op, b);
    x->location = location;
    return true;
}

static bool prefixExpr(ExprPtr &x) {
    int p = save();
    if (signExpr(x)) return true;
    if (restore(p), addressOfExpr(x)) return true;
    if (restore(p), suffixExpr(x)) return true;
    return false;
}



//
// arithmetic expr
//

static bool mulDivOp(int &op) {
    int p = save();
    if (symbol("*"))
        op = MULTIPLY;
    else if (restore(p), symbol("/"))
        op = DIVIDE;
    else if (restore(p), symbol("%"))
        op = REMAINDER;
    else
        return false;
    return true;
}

static bool mulDivTail(BinaryOpPtr &x) {
    LocationPtr location = currentLocation();
    int op;
    if (!mulDivOp(op)) return false;
    ExprPtr b;
    if (!prefixExpr(b)) return false;
    x = new BinaryOp(op, NULL, b);
    x->location = location;
    return true;
}

static bool mulDivExpr(ExprPtr &x) {
    if (!prefixExpr(x)) return false;
    while (true) {
        int p = save();
        BinaryOpPtr y;
        if (!mulDivTail(y)) {
            restore(p);
            break;
        }
        y->expr1 = x;
        x = y.ptr();
    }
    return true;
}

static bool addSubOp(int &op) {
    int p = save();
    if (symbol("+"))
        op = ADD;
    else if (restore(p), symbol("-"))
        op = SUBTRACT;
    else
        return false;
    return true;
}

static bool addSubTail(BinaryOpPtr &x) {
    LocationPtr location = currentLocation();
    int op;
    if (!addSubOp(op)) return false;
    ExprPtr y;
    if (!mulDivExpr(y)) return false;
    x = new BinaryOp(op, NULL, y);
    x->location = location;
    return true;
}

static bool addSubExpr(ExprPtr &x) {
    if (!mulDivExpr(x)) return false;
    while (true) {
        int p = save();
        BinaryOpPtr y;
        if (!addSubTail(y)) {
            restore(p);
            break;
        }
        y->expr1 = x;
        x = y.ptr();
    }
    return true;
}



//
// compare expr
//

static bool compareOp(int &op) {
    int p = save();
    const char *s[] = {"==", "!=", "<", "<=", ">", ">=", NULL};
    const int ops[] = {EQUALS, NOT_EQUALS, LESSER, LESSER_EQUALS,
                       GREATER, GREATER_EQUALS};
    for (const char **a = s; *a; ++a) {
        restore(p);
        if (symbol(*a)) {
            int i = a - s;
            op = ops[i];
            return true;
        }
    }
    return false;
}

static bool compareTail(BinaryOpPtr &x) {
    LocationPtr location = currentLocation();
    int op;
    if (!compareOp(op)) return false;
    ExprPtr y;
    if (!addSubExpr(y)) return false;
    x = new BinaryOp(op, NULL, y);
    x->location = location;
    return true;
}

static bool compareExpr(ExprPtr &x) {
    if (!addSubExpr(x)) return false;
    while (true) {
        int p = save();
        BinaryOpPtr y;
        if (!compareTail(y)) {
            restore(p);
            break;
        }
        y->expr1 = x;
        x = y.ptr();
    }
    return true;
}



//
// not, and, or
//

static bool notExpr(ExprPtr &x) {
    LocationPtr location = currentLocation();
    int p = save();
    if (!keyword("not")) {
        restore(p);
        return compareExpr(x);
    }
    ExprPtr y;
    if (!compareExpr(y)) return false;
    x = new UnaryOp(NOT, y);
    x->location = location;
    return true;
}

static bool andExprTail(AndPtr &x) {
    LocationPtr location = currentLocation();
    if (!keyword("and")) return false;
    ExprPtr y;
    if (!notExpr(y)) return false;
    x = new And(NULL, y);
    x->location = location;
    return true;
}

static bool andExpr(ExprPtr &x) {
    if (!notExpr(x)) return false;
    while (true) {
        int p = save();
        AndPtr y;
        if (!andExprTail(y)) {
            restore(p);
            break;
        }
        y->expr1 = x;
        x = y.ptr();
    }
    return true;
}

static bool orExprTail(OrPtr &x) {
    LocationPtr location = currentLocation();
    if (!keyword("or")) return false;
    ExprPtr y;
    if (!andExpr(y)) return false;
    x = new Or(NULL, y);
    x->location = location;
    return true;
}

static bool orExpr(ExprPtr &x) {
    if (!andExpr(x)) return false;
    while (true) {
        int p = save();
        OrPtr y;
        if (!orExprTail(y)) {
            restore(p);
            break;
        }
        y->expr1 = x;
        x = y.ptr();
    }
    return true;
}



//
// expression
//

static bool expression(ExprPtr &x) {
    return orExpr(x);
}



//
// statements
//

static bool statement(StatementPtr &x);

static bool labelDef(StatementPtr &x) {
    LocationPtr location = currentLocation();
    IdentifierPtr y;
    if (!identifier(y)) return false;
    if (!symbol(":")) return false;
    x = new Label(y);
    x->location = location;
    return true;
}

static bool bindingKind(int &bindingKind) {
    int p = save();
    if (keyword("var"))
        bindingKind = VAR;
    else if (restore(p), keyword("ref"))
        bindingKind = REF;
    else if (restore(p), keyword("static"))
        bindingKind = STATIC;
    else
        return false;
    return true;
}

static bool localBinding(StatementPtr &x) {
    LocationPtr location = currentLocation();
    int bk;
    if (!bindingKind(bk)) return false;
    IdentifierPtr y;
    if (!identifier(y)) return false;
    if (!symbol("=")) return false;
    ExprPtr z;
    if (!expression(z)) return false;
    if (!symbol(";")) return false;
    x = new Binding(bk, y, z);
    x->location = location;
    return true;
}

static bool blockItem(StatementPtr &x) {
    int p = save();
    if (labelDef(x)) return true;
    if (restore(p), localBinding(x)) return true;
    if (restore(p), statement(x)) return true;
    return false;
}

static bool block(StatementPtr &x) {
    LocationPtr location = currentLocation();
    if (!symbol("{")) return false;
    BlockPtr y = new Block();
    while (true) {
        int p = save();
        StatementPtr z;
        if (!blockItem(z)) {
            restore(p);
            break;
        }
        y->statements.push_back(z);
    }
    if (!symbol("}")) return false;
    x = y.ptr();
    x->location = location;
    return true;
}

static bool assignment(StatementPtr &x) {
    LocationPtr location = currentLocation();
    ExprPtr y, z;
    if (!expression(y)) return false;
    if (!symbol("=")) return false;
    if (!expression(z)) return false;
    if (!symbol(";")) return false;
    x = new Assignment(y, z);
    x->location = location;
    return true;
}

static bool gotoStatement(StatementPtr &x) {
    LocationPtr location = currentLocation();
    IdentifierPtr y;
    if (!keyword("goto")) return false;
    if (!identifier(y)) return false;
    if (!symbol(";")) return false;
    x = new Goto(y);
    x->location = location;
    return true;
}

static bool returnStatement(StatementPtr &x) {
    LocationPtr location = currentLocation();
    ExprPtr y;
    if (!keyword("return")) return false;
    if (!optExpression(y)) return false;
    if (!symbol(";")) return false;
    x = new Return(y);
    x->location = location;
    return true;
}

static bool returnRefStatement(StatementPtr &x) {
    LocationPtr location = currentLocation();
    ExprPtr y;
    if (!keyword("returnref")) return false;
    if (!expression(y)) return false;
    if (!symbol(";")) return false;
    x = new ReturnRef(y);
    x->location = location;
    return true;
}

static bool ifStatement(StatementPtr &x) {
    LocationPtr location = currentLocation();
    ExprPtr y;
    StatementPtr z, z2;
    if (!keyword("if")) return false;
    if (!symbol("(")) return false;
    if (!expression(y)) return false;
    if (!symbol(")")) return false;
    if (!statement(z)) return false;
    int p = save();
    if (!keyword("else") || !statement(z2))
        restore(p);
    x = new If(y, z, z2);
    x->location = location;
    return true;
}

static bool exprStatement(StatementPtr &x) {
    LocationPtr location = currentLocation();
    ExprPtr y;
    if (!expression(y)) return false;
    if (!symbol(";")) return false;
    x = new ExprStatement(y);
    x->location = location;
    return true;
}

static bool whileStatement(StatementPtr &x) {
    LocationPtr location = currentLocation();
    ExprPtr y;
    StatementPtr z;
    if (!keyword("while")) return false;
    if (!symbol("(")) return false;
    if (!expression(y)) return false;
    if (!symbol(")")) return false;
    if (!statement(z)) return false;
    x = new While(y, z);
    x->location = location;
    return true;
}

static bool breakStatement(StatementPtr &x) {
    LocationPtr location = currentLocation();
    if (!keyword("break")) return false;
    if (!symbol(";")) return false;
    x = new Break();
    x->location = location;
    return true;
}

static bool continueStatement(StatementPtr &x) {
    LocationPtr location = currentLocation();
    if (!keyword("continue")) return false;
    if (!symbol(";")) return false;
    x = new Continue();
    x->location = location;
    return true;
}

static bool forStatement(StatementPtr &x) {
    LocationPtr location = currentLocation();
    IdentifierPtr a;
    ExprPtr b;
    StatementPtr c;
    if (!keyword("for")) return false;
    if (!symbol("(")) return false;
    if (!identifier(a)) return false;
    if (!keyword("in")) return false;
    if (!expression(b)) return false;
    if (!symbol(")")) return false;
    if (!statement(c)) return false;
    x = new For(a, b, c);
    x->location = location;
    return true;
}

static bool statement(StatementPtr &x) {
    int p = save();
    if (block(x)) return true;
    if (restore(p), assignment(x)) return true;
    if (restore(p), ifStatement(x)) return true;
    if (restore(p), gotoStatement(x)) return true;
    if (restore(p), returnStatement(x)) return true;
    if (restore(p), returnRefStatement(x)) return true;
    if (restore(p), exprStatement(x)) return true;
    if (restore(p), whileStatement(x)) return true;
    if (restore(p), breakStatement(x)) return true;
    if (restore(p), continueStatement(x)) return true;
    if (restore(p), forStatement(x)) return true;
    return false;
}



//
// identifierList, patternVars, typeSpec, optTypeSpec
//

static bool identifierList(vector<IdentifierPtr> &x) {
    IdentifierPtr y;
    if (!identifier(y)) return false;
    x.clear();
    x.push_back(y);
    while (true) {
        int p = save();
        if (!symbol(",") || !identifier(y)) {
            restore(p);
            break;
        }
        x.push_back(y);
    }
    return true;
}

static bool patternVars(vector<IdentifierPtr> &x) {
    if (!symbol("[")) return false;
    if (!identifierList(x)) return false;
    if (!symbol("]")) {
        x.clear();
        return false;
    }
    return true;
}

static bool optPatternVars(vector<IdentifierPtr> &x) {
    int p = save();
    if (!patternVars(x)) {
        restore(p);
        x.clear();
    }
    return true;
}

static bool typeSpec(ExprPtr &x) {
    if (!symbol(":")) return false;
    return expression(x);
}

static bool optTypeSpec(ExprPtr &x) {
    int p = save();
    if (!typeSpec(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}



//
// code
//

static bool valueArg(FormalArgPtr &x) {
    LocationPtr location = currentLocation();
    IdentifierPtr y;
    ExprPtr z;
    if (!identifier(y)) return false;
    if (!optTypeSpec(z)) return false;
    x = new ValueArg(y, z);
    x->location = location;
    return true;
}

static bool staticArg(FormalArgPtr &x) {
    LocationPtr location = currentLocation();
    ExprPtr y;
    if (!keyword("static")) return false;
    if (!expression(y)) return false;
    x = new StaticArg(y);
    x->location = location;
    return true;
}

static bool formalArg(FormalArgPtr &x) {
    int p = save();
    if (valueArg(x)) return true;
    if (restore(p), staticArg(x)) return true;
    return false;
}

static bool formalArgs(vector<FormalArgPtr> &x) {
    FormalArgPtr y;
    if (!formalArg(y)) return false;
    x.clear();
    x.push_back(y);
    while (true) {
        int p = save();
        if (!symbol(",") || !formalArg(y)) {
            restore(p);
            break;
        }
        x.push_back(y);
    }
    return true;
}

static bool optFormalArgs(vector<FormalArgPtr> &x) {
    int p = save();
    if (!formalArgs(x)) {
        restore(p);
        x.clear();
    }
    return true;
}

static bool predicate(ExprPtr &x) {
    if (!symbol("|")) return false;
    return expression(x);
}

static bool optPredicate(ExprPtr &x) {
    int p = save();
    if (!predicate(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}

static bool patternVarsWithCond(vector<IdentifierPtr> &x, ExprPtr &y) {
    if (!symbol("[")) return false;
    if (!identifierList(x)) return false;
    if (!optPredicate(y) || !symbol("]")) {
        x.clear();
        y = NULL;
        return false;
    }
    return true;
}

static bool optPatternVarsWithCond(vector<IdentifierPtr> &x, ExprPtr &y) {
    int p = save();
    if (!patternVarsWithCond(x, y)) {
        restore(p);
        x.clear();
        y = NULL;
    }
    return true;
}

static bool exprBody(StatementPtr &x) {
    if (!symbol("=")) return false;
    ExprPtr y;
    if (!expression(y)) return false;
    if (!symbol(";")) return false;
    x = new Return(y);
    x->location = y->location;
    return true;
}

static bool exprBody2(StatementPtr &x) {
    if (!symbol("=")) return false;
    if (!keyword("ref")) return false;
    ExprPtr y;
    if (!expression(y)) return false;
    if (!symbol(";")) return false;
    x = new ReturnRef(y);
    x->location = y->location;
    return true;
}

static bool body(StatementPtr &x) {
    int p = save();
    if (exprBody(x)) return true;
    if (restore(p), exprBody2(x)) return true;
    if (restore(p), block(x)) return true;
    return false;
}

static bool code(CodePtr &x) {
    LocationPtr location = currentLocation();
    CodePtr y = new Code();
    if (!optPatternVarsWithCond(y->patternVars, y->predicate)) return false;
    if (!symbol("(")) return false;
    if (!optFormalArgs(y->formalArgs)) return false;
    if (!symbol(")")) return false;
    if (!body(y->body)) return false;
    x = y;
    x->location = location;
    return true;
}



//
// records
//

static bool recordValueArg(FormalArgPtr &x) {
    LocationPtr location = currentLocation();
    IdentifierPtr y;
    ExprPtr z;
    if (!identifier(y)) return false;
    if (!typeSpec(z)) return false;
    x = new ValueArg(y, z);
    x->location = location;
    return true;
}

static bool recordFormalArg(FormalArgPtr &x) {
    int p = save();
    if (recordValueArg(x)) return true;
    if (restore(p), staticArg(x)) return true;
    return false;
}

static bool recordFormalArgs(vector<FormalArgPtr> &x) {
    FormalArgPtr y;
    if (!recordFormalArg(y)) return false;
    x.clear();
    x.push_back(y);
    while (true) {
        int p = save();
        if (!symbol(",") || !recordFormalArg(y)) {
            restore(p);
            break;
        }
        x.push_back(y);
    }
    return true;
}

static bool optRecordFormalArgs(vector<FormalArgPtr> &x) {
    int p = save();
    if (!recordFormalArgs(x)) {
        restore(p);
        x.clear();
    }
    return true;
}

static bool record(TopLevelItemPtr &x) {
    LocationPtr location = currentLocation();
    if (!keyword("record")) return false;
    RecordPtr y = new Record();
    if (!identifier(y->name)) return false;
    if (!optPatternVars(y->patternVars)) return false;
    if (!symbol("(")) return false;
    if (!optRecordFormalArgs(y->formalArgs)) return false;
    if (!symbol(")")) return false;
    if (!symbol(";")) return false;
    x = y.ptr();
    x->location = location;
    return true;
}



//
// procedure, overloadable, overload
//

static bool procedure(TopLevelItemPtr &x) {
    LocationPtr location = currentLocation();
    IdentifierPtr y;
    if (!identifier(y)) return false;
    CodePtr z;
    if (!code(z)) return false;
    x = new Procedure(y, z);
    x->location = location;
    return true;
}

static bool overloadable(TopLevelItemPtr &x) {
    LocationPtr location = currentLocation();
    if (!keyword("overloadable")) return false;
    IdentifierPtr y;
    if (!identifier(y)) return false;
    if (!symbol(";")) return false;
    x = new Overloadable(y);
    x->location = location;
    return true;
}

static bool overload(TopLevelItemPtr &x) {
    LocationPtr location = currentLocation();
    if (!keyword("overload")) return false;
    IdentifierPtr y;
    if (!identifier(y)) return false;
    CodePtr z;
    if (!code(z)) return false;
    x = new Overload(y, z);
    x->location = location;
    return true;
}



//
// external procedure
//

static bool externalArg(ExternalArgPtr &x) {
    LocationPtr location = currentLocation();
    IdentifierPtr y;
    if (!identifier(y)) return false;
    ExprPtr z;
    if (!typeSpec(z)) return false;
    x = new ExternalArg(y, z);
    x->location = location;
    return true;
}

static bool externalArgs(vector<ExternalArgPtr> &x) {
    ExternalArgPtr y;
    if (!externalArg(y)) return false;
    x.clear();
    x.push_back(y);
    while (true) {
        int p = save();
        if (!symbol(",") || !externalArg(y)) {
            restore(p);
            break;
        }
        x.push_back(y);
    }
    return true;
}

static bool optExternalArgs(vector<ExternalArgPtr> &x) {
    int p = save();
    if (!externalArgs(x)) {
        restore(p);
        x.clear();
    }
    return true;
}

static bool external(TopLevelItemPtr &x) {
    LocationPtr location = currentLocation();
    if (!keyword("external")) return false;
    ExternalProcedurePtr y = new ExternalProcedure();
    if (!identifier(y->name)) return false;
    if (!symbol("(")) return false;
    if (!optExternalArgs(y->args)) return false;
    if (!symbol(")")) return false;
    if (!typeSpec(y->returnType)) return false;
    if (!symbol(";")) return false;
    x = y.ptr();
    x->location = location;
    return true;
}



//
// import, export, module
//

static bool import(ImportPtr &x) {
    LocationPtr location = currentLocation();
    if (!keyword("import")) return false;
    DottedNamePtr y;
    if (!dottedName(y)) return false;
    if (!symbol(";")) return false;
    x = new Import(y);
    x->location = location;
    return true;
}

static bool imports(vector<ImportPtr> &x) {
    x.clear();
    while (true) {
        int p = save();
        ImportPtr y;
        if (!import(y)) {
            restore(p);
            break;
        }
        x.push_back(y);
    }
    return true;
}

static bool export_(ExportPtr &x) {
    LocationPtr location = currentLocation();
    if (!keyword("export")) return false;
    DottedNamePtr y;
    if (!dottedName(y)) return false;
    if (!symbol(";")) return false;
    x = new Export(y);
    x->location = location;
    return true;
}

static bool exports(vector<ExportPtr> &x) {
    x.clear();
    while (true) {
        int p = save();
        ExportPtr y;
        if (!export_(y)) {
            restore(p);
            break;
        }
        x.push_back(y);
    }
    return true;
}

static bool topLevelItem(TopLevelItemPtr &x) {
    int p = save();
    if (record(x)) return true;
    if (restore(p), procedure(x)) return true;
    if (restore(p), overloadable(x)) return true;
    if (restore(p), overload(x)) return true;
    if (restore(p), external(x)) return true;
    return false;
}

static bool topLevelItems(vector<TopLevelItemPtr> &x) {
    x.clear();
    while (true) {
        int p = save();
        TopLevelItemPtr y;
        if (!topLevelItem(y)) {
            restore(p);
            break;
        }
        x.push_back(y);
    }
    return true;
}

static bool module(ModulePtr &x) {
    LocationPtr location = currentLocation();
    ModulePtr y = new Module();
    if (!imports(y->imports)) return false;
    if (!exports(y->exports)) return false;
    if (!topLevelItems(y->topLevelItems)) return false;
    x = y.ptr();
    x->location = location;
    return true;
}



//
// parse
//

ModulePtr parse(SourcePtr source) {
    vector<TokenPtr> t;
    tokenize(source, t);

    tokens = &t;
    position = maxPosition = 0;

    ModulePtr m;
    if (!module(m) || (position < (int)t.size())) {
        LocationPtr location;
        if (maxPosition == (int)t.size())
            location = new Location(source, source->size);
        else
            location = t[maxPosition]->location;
        pushLocation(location);
        error("parse error");
    }

    tokens = NULL;
    position = maxPosition = 0;

    return m;
}