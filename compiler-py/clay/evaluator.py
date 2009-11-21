from clay.ast import *
from clay.core import *
from clay.unifier import *
from clay.env import *
from clay.primitives import *



#
# lift
#

lift = multimethod("lift")

lift.register(str)(toCOValue)
lift.register(Type)(toCOValue)
lift.register(Record)(toCOValue)
lift.register(Procedure)(toCOValue)
lift.register(Overloadable)(toCOValue)
lift.register(ExternalProcedure)(toCOValue)
lift.register(Value)(lambda x : x)



#
# temp values
#

_tempsBlocks = []

def pushTempsBlock() :
    block = []
    _tempsBlocks.append(block)

def popTempsBlock() :
    block = _tempsBlocks.pop()
    while block :
        block.pop()

def installTemp(value) :
    _tempsBlocks[-1].append(value)
    return value

def allocTempValue(type_) :
    v = allocValue(type_)
    installTemp(v)
    return v



#
# toOwnedValue, toReferredValue
#

toOwnedValue = multimethod("toOwnedValue")

@toOwnedValue.register(Value)
def foo(v) :
    if not v.isOwned :
        v2 = allocTempValue(v.type)
        copyValue(v2, v)
        return v2
    return v

toReferredValue = multimethod("toReferredValue")

@toReferredValue.register(Value)
def foo(x) :
    return Value(x.type, False, x.address)



#
#
#

def evaluateRootExpr(expr, env, converter=(lambda x : x)) :
    pushTempsBlock()
    try :
        return evaluate(expr, env, converter)
    finally :
        popTempsBlock()

def evaluateList(exprList, env) :
    return [evaluate(x, env) for x in exprList]

def evaluate(expr, env, converter=(lambda x : x)) :
    return withContext(expr, lambda : converter(evaluate2(expr, env)))

evaluate2 = multimethod("evaluate2")

@evaluate2.register(BoolLiteral)
def foo(x, env) :
    return installTemp(toBoolValue(x.value))

_suffixMap = {
    "i8" : toInt8Value,
    "i16" : toInt16Value,
    "i32" : toInt32Value,
    "i64" : toInt64Value,
    "u8" : toUInt8Value,
    "u16" : toUInt16Value,
    "u32" : toUInt32Value,
    "u64" : toUInt64Value,
    "f32" : toFloat32Value,
    "f64" : toFloat64Value}

@evaluate2.register(IntLiteral)
def foo(x, env) :
    if x.suffix is None :
        f = toInt32Value
    else :
        f = _suffixMap.get(x.suffix)
        ensure(f is not None, "invalid suffix: %s" % x.suffix)
    return installTemp(f(x.value))

@evaluate2.register(FloatLiteral)
def foo(x, env) :
    if x.suffix is None :
        f = toFloat64Value
    else :
        ensure(x.suffix in ["f32", "f64"],
               "invalid floating point suffix: %s" % x.suffix)
        f = _suffixMap[x.suffix]
    return installTemp(f(x.value))

@evaluate2.register(CharLiteral)
def foo(x, env) :
    return evaluate(convertCharLiteral(x.value), env)

@evaluate2.register(StringLiteral)
def foo(x, env) :
    return evaluate(convertStringLiteral(x.value), env)

@evaluate2.register(NameRef)
def foo(x, env) :
    v = lookupIdent(env, x.name)
    if type(v) is StaticValue :
        v2 = allocTempValue(v.value.type)
        copyValue(v2, v.value)
        return v2
    ensure(type(v) is Value, "invalid value")
    return toReferredValue(v)

@evaluate2.register(Tuple)
def foo(x, env) :
    return evaluate(convertTuple(x), env)

@evaluate2.register(Array)
def foo(x, env) :
    return evaluate(convertArray(x), env)

@evaluate2.register(Indexing)
def foo(x, env) :
    thing = evaluate(x.expr, env)
    args = evaluateList(x.args, env)
    return invokeIndexing(lower(thing), args)

@evaluate2.register(Call)
def foo(x, env) :
    thing = evaluate(x.expr, env)
    args = evaluateList(x.args, env)
    return invoke(lower(thing), args)

@evaluate2.register(FieldRef)
def foo(x, env) :
    thing = evaluate(x.expr, env)
    name = toCOValue(x.name.s)
    return invoke(PrimObjects.recordFieldRefByName, [thing, name])

@evaluate2.register(TupleRef)
def foo(x, env) :
    thing = evaluate(x.expr, env)
    index = toInt32Value(x.index)
    return invoke(PrimObjects.tupleFieldRef, [thing, index])

@evaluate2.register(Dereference)
def foo(x, env) :
    thing = evaluate(x.expr, env)
    return invoke(PrimObjects.pointerDereference, [thing])

@evaluate2.register(AddressOf)
def foo(x, env) :
    thing = evaluate(x.expr, env)
    return invoke(PrimObjects.addressOf, [thing])

@evaluate2.register(UnaryOpExpr)
def foo(x, env) :
    return evaluate(convertUnaryOpExpr(x), env)

@evaluate2.register(BinaryOpExpr)
def foo(x, env) :
    return evaluate(convertBinaryOpExpr(x), env)

@evaluate2.register(NotExpr)
def foo(x, env) :
    thing = evaluate(x.expr, env)
    return invoke(PrimObjects.boolNot, [thing])

@evaluate2.register(AndExpr)
def foo(x, env) :
    v1 = evaluate(x.expr1, env)
    flag1 = invoke(PrimObjects.boolTruth, [v1])
    if not fromBoolValue(flag1) :
        return v1
    return evaluate(x.expr2, env)

@evaluate2.register(OrExpr)
def foo(x, env) :
    v1 = evaluate(x.expr1, env)
    flag1 = invoke(PrimObjects.boolTruth, [v1])
    if fromBoolValue(flag1) :
        return v1
    return evaluate(x.expr2, env)

@evaluate2.register(StaticExpr)
def foo(x, env) :
    return evaluate(x.expr, env, toOwnedValue)

@evaluate2.register(SCExpression)
def foo(x, env) :
    return evaluate(x.expr, x.env)



#
# de-sugar syntax
#

def convertCharLiteral(c) :
    nameRef = NameRef(Identifier("Char"))
    nameRef = SCExpression(loadedModule("_char").env, nameRef)
    return Call(nameRef, [IntLiteral(ord(c), "i8")])

def convertStringLiteral(s) :
    nameRef = NameRef(Identifier("string"))
    nameRef = SCExpression(loadedModule("_string").env, nameRef)
    charArray = Array([convertCharLiteral(c) for c in s])
    return Call(nameRef, [charArray])

def convertTuple(x) :
    if len(x.args) == 1 :
        return x.args[0]
    return Call(primitiveNameRef("tuple"), x.args)

def convertArray(x) :
    return Call(primitiveNameRef("array"), x.args)

_unaryOps = {"+" : "plus",
             "-" : "minus"}

def convertUnaryOpExpr(x) :
    return Call(coreNameRef(_unaryOps[x.op]), [x.expr])

_binaryOps = {"+"  : "add",
              "-"  : "subtract",
              "*"  : "multiply",
              "/"  : "divide",
              "%"  : "remainder",
              "==" : "equals?",
              "!=" : "notEquals?",
              "<"  : "lesser?",
              "<=" : "lesserEquals?",
              ">"  : "greater?",
              ">=" : "greaterEquals?"}

def convertBinaryOpExpr(x) :
    return Call(coreNameRef(_binaryOps[x.op]), [x.expr1, x.expr2])



#
# invokeIndexing
#

invokeIndexing = multimethod("invokeIndexing")

@invokeIndexing.register(TypeConstructorPrimOp)
def foo(x, args) :
    return invoke(x.constructorPrim, args)

@invokeIndexing.register(Record)
def foo(x, args) :
    return invoke(PrimObjects.RecordType, [toCOValue(x)] + args)



#
# invoke
#

invoke = multimethod("invoke")

@invoke.register(Procedure)
def foo(x, args) :
    result = matchInvokeCode(x.code, args)
    if isinstance(result, MatchError) :
        result.signalError()
    env = result
    return evalCodeBody(x.code, env)

@invoke.register(Overloadable)
def foo(x, args) :
    for y in x.overloads :
        result = matchInvokeCode(y.code, args)
        if not isinstance(result, MatchError) :
            env = result
            return evalCodeBody(y.code, env)
    error("no matching overload")

@invoke.register(ExternalProcedure)
def foo(x, args) :
    raise NotImplementedError



#
# matchInvokeCode
#

class MatchError(object) :
    pass

class ArgCountError(MatchError) :
    def signalError(self) :
        error("incorrect no. of arguments")

class ArgMismatch(MatchError) :
    def __init__(self, pos) :
        super(ArgMismatch, self).__init__()
        self.pos = pos
    def signalError(self) :
        error("mismatch at argument %d" % (self.pos+1))

class PredicateFailure(MatchError) :
    def signalError(self) :
        error("procedure predicate failure")

def matchInvokeCode(x, args) :
    if len(args) != len(x.formalArgs) :
        return ArgCountError()
    cells = [Cell(y) for y in x.typeVars]
    patternEnv = extendEnv(x.env, x.typeVars, cells)
    for i, farg in enumerate(x.formalArgs) :
        if not matchArg(farg, args[i], patternEnv) :
            return ArgMismatch(i)
    env2 = extendEnv(x.env, x.typeVars, derefCells(cells))
    if x.predicate is not None :
        result = evaluateRootExpr(x.predicate, env2, toBoolResult)
        if not result :
            return PredicateFailure()
    for arg, farg in zip(args, x.formalArgs) :
        if type(farg) is ValueArgument :
            addIdent(env2, farg.name, toReferredValue(arg))
    return env2

matchArg = multimethod("matchArg")

@matchArg.register(ValueArgument)
def foo(farg, arg, env) :
    if farg.type is None :
        return True
    pattern = evaluatePattern(farg.type, env)
    return unify(pattern, toCOValue(arg.type))

@matchArg.register(StaticArgument)
def foo(farg, arg, env) :
    pattern = evaluatePattern(farg.pattern, env)
    return unify(pattern, arg)

def toBoolResult(v) :
    return fromBoolValue(invoke(PrimObjects.boolTruth, [v]))



#
# evaluatePattern
#

def evaluatePattern(x, env) :
    pushTempsBlock()
    try :
        return evaluatePattern2(x, env)
    finally :
        popTempsBlock()

evaluatePattern2 = multimethod("evaluatePattern2")

@evaluatePattern2.register(object)
def foo(x, env) :
    return evaluate(x, env, toOwnedValue)

@evaluatePattern2.register(NameRef)
def foo(x, env) :
    v = lookupIdent(env, x.name)
    if type(v) is Cell :
        return v
    return evaluate(x, env, toOwnedValue)

@evaluatePattern2.register(Indexing)
def foo(x, env) :
    thing = evaluate(x.expr, env)
    args = [evaluatePattern(y, env) for y in x.args]
    return invokeIndexingPattern(lower(thing), args)

invokeIndexingPattern = multimethod("invokeIndexingPattern")

@invokeIndexingPattern.register(object)
def foo(x, args) :
    return invokeIndexing(x, args)

@invokeIndexingPattern.register(PrimObjects.Pointer)
def foo(x, args) :
    ensureArity(args, 1)
    return PointerTypePattern(args[0])

@invokeIndexingPattern.register(PrimObjects.Array)
def foo(x, args) :
    ensureArity(args, 2)
    return ArrayTypePattern(args[0], args[1])

@invokeIndexingPattern.register(PrimObjects.Tuple)
def foo(x, args) :
    ensure(len(args) >= 2, "tuple type needs atleast 2 elements")
    return TupleTypePattern(args)

@invokeIndexingPattern.register(Record)
def foo(x, args) :
    ensureArity(args, len(x.typeVars))
    return RecordTypePattern(x, args)



#
# evalCodeBody
#

def evalCodeBody(code, env) :
    result = evalStatement(code.body, env)
    if result is None :
        result = ReturnResult(None)
    return result.asFinalResult()

class StatementResult(object) :
    pass

class GotoResult(StatementResult) :
    def __init__(self, labelName) :
        super(GotoResult, self).__init__()
        self.labelName = labelName
    def asFinalResult(self) :
        withContext(self.labelName, lambda : error("label not found"))

class BreakResult(StatementResult) :
    def __init__(self, stmt) :
        super(BreakResult, self).__init__()
        self.stmt = stmt
    def asFinalResult(self) :
        withContext(self.stmt, lambda : error("invalid break statement"))

class ContinueResult(StatementResult) :
    def __init__(self, stmt) :
        super(ContinueResult, self).__init__()
        self.stmt = stmt
    def asFinalResult(self) :
        withContext(self.stmt, lambda : error("invalid continue statement"))

class ReturnResult(StatementResult) :
    def __init__(self, result) :
        super(ReturnResult, self).__init__()
        self.result = result
    def asFinalResult(self) :
        if self.result.isOwned :
            installTemp(self.result)
        return self.result

def evalStatement(x, env) :
    return withContext(x, lambda : evalStatement2(x, env))

evalStatement2 = multimethod("evalStatement2")

@evalStatement2.register(Block)
def foo(x, env) :
    i = 0
    labels = {}
    evalCollectLabels(x.statements, i, labels, env)
    while i < len(x.statements) :
        y = x.statements[i]
        if type(y) in (VarBinding, RefBinding, StaticBinding) :
            env = evalBinding(y, env)
            evalCollectLabels(x.statements, i+1, labels, env)
        elif type(y) is Label :
            pass
        else :
            result = evalStatement(y, env)
            if type(result) is GotoResult :
                envAndPos = labels.get(result.labelName.s)
                if envAndPos is not None :
                    env, i = envAndPos
                    continue
            if result is not None :
                return result
        i += 1

def evalCollectLabels(statements, startIndex, labels, env) :
    i = startIndex
    while i < len(statements) :
        x = statements[i]
        if type(x) is Label :
            labels[x.name.s] = (env, i)
        elif type(x) in (VarBinding, RefBinding, StaticBinding) :
            break
        i += 1

evalBinding = multimethod("evalBinding")

@evalBinding.register(VarBinding)
def foo(x, env) :
    right = evaluateRootExpr(x.expr, env, toOwnedValue)
    return extendEnv(env, [x.name], [right])

@evalBinding.register(RefBinding)
def foo(x, env) :
    right = evaluateRootExpr(x.expr, env)
    return extendEnv(env, [x.name], [right])

@evalBinding.register(StaticBinding)
def foo(x, env) :
    right = evaluateRootExpr(x.expr, env, toOwnedValue)
    return extendEnv(env, [x.name], [StaticValue(right)])


@evalStatement2.register(Assignment)
def foo(x, env) :
    pushTempsBlock()
    try :
        left = evaluate(x.left, env)
        ensure(not left.isOwned, "cannot assign to a temp")
        right = evaluate(x.right, env)
        assignValue(left, right)
    finally :
        popTempsBlock()

@evalStatement2.register(Goto)
def foo(x, env) :
    return GotoResult(x.labelName)

@evalStatement2.register(Return)
def foo(x, env) :
    if x.expr is None :
        return ReturnResult(None)
    result = evaluateRootExpr(x.expr, env, toOwnedValue)
    return ReturnResult(result)

@evalStatement2.register(ReturnRef)
def foo(x, env) :
    result = evaluateRootExpr(x.expr, env)
    ensure(not result.isOwned, "cannot return a temp by reference")
    return ReturnResult(result)

@evalStatement2.register(IfStatement)
def foo(x, env) :
    cond = evaluateRootExpr(x.condition, env, toBoolResult)
    if cond :
        return evalStatement(x.thenPart, env)
    elif x.elsePart is not None :
        return evalStatement(x.elsePart, env)

@evalStatement2.register(ExprStatement)
def foo(x, env) :
    evaluateRootExpr(x.expr, env)

@evalStatement2.register(While)
def foo(x, env) :
    while True :
        cond = evaluateRootExpr(x.condition, env, toBoolResult)
        if not cond :
            break
        result = evalStatement(x.body, env)
        if type(result) is BreakResult :
            break
        elif type(result) is ContinueResult :
            continue
        elif result is not None :
            return result

@evalStatement2.register(Break)
def foo(x, env) :
    return BreakResult(x)

@evalStatement2.register(Continue)
def foo(x, env) :
    return ContinueResult(x)

@evalStatement2.register(For)
def foo(x, env) :
    return evalStatement(convertForStatement(x), env)



#
# de-sugar for statement
#

def convertForStatement(x) :
    exprVar = Identifier("%expr")
    iterVar = Identifier("%iter")
    block = Block(
        [RefBinding(exprVar, x.expr),
         VarBinding(iterVar, Call(coreNameRef("iterator"),
                                  [NameRef(exprVar)])),
         While(Call(coreNameRef("hasNext?"), [NameRef(iterVar)]),
               Block([RefBinding(x.variable, Call(coreNameRef("next"),
                                                  [NameRef(iterVar)])),
                      x.body]))])
    return block



#
# remove temp name used for multimethod instances
#

del foo
