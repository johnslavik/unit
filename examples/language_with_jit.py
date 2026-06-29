from collections.abc import Iterable, Iterator, Sequence
from typing import Any, Literal, overload
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from queue import LifoQueue
import io
from enum import Enum, auto
import operator as py_operators


class OpCode(Enum):
    LOAD_CONST = auto()
    LOAD_NAME = auto()
    STORE_NAME = auto()
    PRINT = auto()
    STORE_FUNCTION = auto()
    LOAD_FUNCTION = auto()
    POP_TOP = auto()
    CALL = auto()
    RETURN = auto()
    JUMP = auto()
    JUMP_IF_TRUE = auto()
    BINARY_OP = auto()
    COMPARE = auto()


@dataclass(frozen=True, slots=True)
class Instruction:
    opcode: OpCode
    oparg: Any = None


@dataclass(slots=True)
class JumpLabel:
    name: str
    index: int | None = field(init=False, default=None)


type Code = Instruction | JumpLabel


class ASTNode(ABC):
    @abstractmethod
    def codegen(self) -> Iterator[Code]: ...


class Expression(ASTNode):
    @abstractmethod
    def codegen(self) -> Iterator[Instruction]: ...


@dataclass(slots=True)
class Constant[T](Expression):
    value: T

    def codegen(self) -> Iterator[Instruction]:
        yield Instruction(OpCode.LOAD_CONST, self.value)


@dataclass(slots=True)
class FunctionCall(Expression):
    name: str
    args: Sequence[Expression]

    def codegen(self) -> Iterator[Instruction]:
        yield Instruction(OpCode.LOAD_FUNCTION, self.name)
        for argument in self.args:
            yield from argument.codegen()

        yield Instruction(OpCode.CALL, len(self.args))


@dataclass(slots=True)
class BinaryOperator(Expression):
    left: Expression
    right: Expression
    operator: str

    def codegen(self) -> Iterator[Instruction]:
        yield from self.left.codegen()
        yield from self.right.codegen()
        yield Instruction(OpCode.BINARY_OP, self.operator)


@dataclass(slots=True)
class Compare(Expression):
    left: Expression
    right: Expression
    operator: str

    def codegen(self) -> Iterator[Instruction]:
        yield from self.left.codegen()
        yield from self.right.codegen()
        yield Instruction(OpCode.COMPARE, self.operator)


@dataclass(slots=True)
class Name(Expression):
    name: str

    def codegen(self) -> Iterator[Instruction]:
        yield Instruction(OpCode.LOAD_NAME, self.name)


class Statement(ASTNode):
    pass


@dataclass(slots=True)
class Body(ASTNode):
    statements: Iterable[Statement]

    def codegen(self) -> Iterator[Code]:
        for statement in self.statements:
            yield from statement.codegen()


@dataclass(slots=True)
class InlineExpression(Statement):
    expression: Expression

    def codegen(self) -> Iterator[Instruction]:
        yield from self.expression.codegen()
        yield Instruction(OpCode.POP_TOP)


@dataclass(slots=True)
class Let(Statement):
    name: str
    value: Expression

    def codegen(self) -> Iterator[Instruction]:
        yield from self.value.codegen()
        yield Instruction(OpCode.STORE_NAME, self.name)


@dataclass(slots=True)
class Function:
    name: str
    parameters: Sequence[str]
    body: Iterable[Code]


@dataclass(slots=True)
class FunctionDefinition(Statement):
    name: str
    parameters: Sequence[str]
    body: Body

    def codegen(self) -> Iterator[Code]:
        code: list[Code] = []
        for instruction in self.body.codegen():
            code.append(instruction)

        function = Function(self.name, self.parameters, code)
        yield Instruction(OpCode.STORE_FUNCTION, function)


@dataclass(slots=True)
class Print(Statement):
    value: Expression

    def codegen(self) -> Iterator[Instruction]:
        yield from self.value.codegen()
        yield Instruction(OpCode.PRINT)


@dataclass(slots=True)
class Return(Statement):
    value: Expression

    def codegen(self) -> Iterator[Instruction]:
        yield from self.value.codegen()
        yield Instruction(OpCode.RETURN)


@dataclass(slots=True)
class If(Statement):
    condition: Expression
    if_true: Body
    if_false: Body | None

    def codegen(self) -> Iterator[Code]:
        if_true = JumpLabel("if_true")
        end = JumpLabel("end")
        yield from self.condition.codegen()
        yield Instruction(OpCode.JUMP_IF_TRUE, if_true)

        if self.if_false is not None:
            yield from self.if_false.codegen()

        yield Instruction(OpCode.JUMP, end)

        yield if_true
        yield from self.if_true.codegen()
        yield end


@dataclass(slots=True)
class Module:
    body: Iterable[Statement]

    def codegen(self) -> Iterator[Code]:
        for statement in self.body:
            yield from statement.codegen()


def tokenize(source: str) -> Iterator[str]:
    for line in source.split("\n"):
        if line == "":
            continue

        buffer = io.StringIO()
        for character in line:
            if character in {"'", '"', "(", ")", "=", ",", " ", "+", "-", "*", "/"}:
                result = buffer.getvalue()
                if result != "":
                    yield result
                    buffer = io.StringIO()

                if character != " ":
                    yield character
            else:
                buffer.write(character)

        if buffer.getvalue() != "":
            yield buffer.getvalue()


class Parser:
    def __init__(self, source: str) -> None:
        self.tokens = tokenize(source)
        self._token_stack = LifoQueue()

    def push_token(self, value: str) -> None:
        self._token_stack.put(value)

    @overload
    def expect_token(
        self, value: str | None = None, *, allow_empty: Literal[False] = False
    ) -> str:
        pass

    @overload
    def expect_token(
        self, value: str | None = None, *, allow_empty: Literal[True] = True
    ) -> str | None:
        pass

    def expect_token(
        self, value: str | None = None, *, allow_empty: bool = False
    ) -> str | None:
        if not self._token_stack.empty():
            return self._token_stack.get()

        try:
            token = next(self.tokens)
        except StopIteration:
            if allow_empty is True:
                return None

            raise SyntaxError("Unexpected end of input") from None

        if value is not None:
            if token != value:
                raise SyntaxError(f"Expected {value!r}, not {token!r}")

        return token

    def pop_token_if_matches(
        self, *values: str, allow_empty: bool = False
    ) -> str | None:
        token = self.expect_token(allow_empty=allow_empty)

        if token in values:
            return token

        if token is not None:
            self.push_token(token)

        return None

    def parse_single_quote_str(self) -> Constant[str]:
        value = self.expect_token()
        self.expect_token("'")
        return Constant(value)

    def parse_double_quote_str(self) -> Constant[str]:
        value = self.expect_token()
        self.expect_token('"')
        return Constant(value)

    def parse_call(self, name: str) -> FunctionCall:
        arguments: list[Expression] = []

        while True:
            if self.pop_token_if_matches(")"):
                return FunctionCall(name, arguments)

            argument = self.parse_expr()
            arguments.append(argument)

            self.pop_token_if_matches(",")

    def parse_inner_expr(self) -> Expression:
        token = self.expect_token()
        if token.isdigit():
            return Constant[int](int(token))
        elif token == "'":
            return self.parse_single_quote_str()
        elif token == '"':
            return self.parse_double_quote_str()
        else:
            if self.pop_token_if_matches("("):
                return self.parse_call(token)
            return Name(token)

    def parse_expr(self) -> Expression:
        first = self.parse_inner_expr()

        if operator := self.pop_token_if_matches("+", "-", "*", "/", allow_empty=True):
            second = self.parse_expr()
            return BinaryOperator(first, second, operator)

        if first_cmp := self.pop_token_if_matches("!", "=", "<", ">", allow_empty=True):
            if second_cmp := self.pop_token_if_matches("="):
                full = first_cmp + second_cmp
                second = self.parse_expr()
                return Compare(first, second, full)

            if first_cmp in {"!", "="}:
                self.push_token(first_cmp)
                return first
            second = self.parse_expr()

            return Compare(first, second, first_cmp)

        return first

    def parse_let(self) -> Let:
        name = self.expect_token()
        self.expect_token("=")
        value = self.parse_expr()
        return Let(name, value)

    def parse_body(self) -> Body:
        body: list[Statement] = []
        self.expect_token("{")
        while True:
            if self.pop_token_if_matches("}"):
                break

            statement = self.parse_statement()
            body.append(statement)

        return Body(body)

    def parse_function_definition(self) -> FunctionDefinition:
        name = self.expect_token()
        self.expect_token("(")
        parameters: list[str] = []

        while True:
            token = self.expect_token()
            if token == ")":
                break

            parameters.append(token)
            self.pop_token_if_matches(",")

        return FunctionDefinition(name, parameters, self.parse_body())

    def parse_print(self) -> Print:
        return Print(self.parse_expr())

    def parse_return(self) -> Return:
        return Return(self.parse_expr())

    def parse_if(self) -> If:
        condition = self.parse_expr()
        if_true = self.parse_body()

        if_false = None
        if self.pop_token_if_matches("else", allow_empty=True):
            if_false = self.parse_body()

        return If(condition, if_true, if_false)

    def parse_statement(self) -> Statement:
        token = self.expect_token()

        if token == "let":
            return self.parse_let()
        elif token == "func":
            return self.parse_function_definition()
        elif token == "print":
            return self.parse_print()
        elif token == "return":
            return self.parse_return()
        elif token == "if":
            return self.parse_if()
        else:
            self.push_token(token)
            return InlineExpression(self.parse_expr())

    def parse_module(self) -> Module:
        statements: list[Statement] = []

        while True:
            token = self.expect_token(allow_empty=True)
            if token is None:
                return Module(statements)

            self.push_token(token)
            statements.append(self.parse_statement())

    @classmethod
    def parse(cls, source: str) -> Module:
        return cls(source).parse_module()


class Stack[T]:
    def __init__(self) -> None:
        self._queue = LifoQueue[T]()

    def push(self, value: T) -> None:
        self._queue.put_nowait(value)

    def pop(self) -> T:
        return self._queue.get_nowait()

    def is_empty(self) -> bool:
        return self._queue.empty()


class Interpreter:
    def __init__(self, code: Iterable[Code]) -> None:
        self.code = list(code)
        self.variables: dict[str, Any] = {}
        self.functions: dict[str, Function] = {}
        self.stack = Stack[Any]()
        self._index = 0

    def interpret_instruction(self, instruction: Instruction) -> None:
        opcode = instruction.opcode
        oparg = instruction.oparg

        if opcode == OpCode.LOAD_CONST:
            self.stack.push(oparg)
        elif opcode == OpCode.STORE_NAME:
            assert isinstance(oparg, str)
            value = self.stack.pop()
            self.variables[oparg] = value
        elif opcode == OpCode.LOAD_NAME:
            assert isinstance(oparg, str)
            try:
                self.stack.push(self.variables[oparg])
            except KeyError:
                raise RuntimeError(f"No variable named {oparg!r}") from None
        elif opcode == OpCode.PRINT:
            assert oparg is None
            print(self.stack.pop())
        elif opcode == OpCode.STORE_FUNCTION:
            assert isinstance(oparg, Function)
            name = oparg.name
            self.functions[name] = oparg
        elif opcode == OpCode.LOAD_FUNCTION:
            assert isinstance(oparg, str)
            try:
                function = self.functions[oparg]
            except KeyError:
                raise RuntimeError(f"No function named {oparg!r}") from None
            self.stack.push(function)
        elif opcode == OpCode.CALL:
            assert isinstance(oparg, int)
            arguments = [self.stack.pop() for _ in range(oparg)]
            arguments.reverse()

            function = self.stack.pop()
            assert isinstance(function, Function)

            num_parameters = len(function.parameters)
            if num_parameters != len(arguments):
                raise RuntimeError(
                    f"expected {num_parameters} arguments to {function.name} (got {len(arguments)})"
                )

            interpreter = Interpreter([inst for inst in function.body])

            # To allow recursive calls:
            interpreter.functions[function.name] = function

            for parameter, value in zip(function.parameters, arguments):
                assert parameter not in interpreter.variables
                interpreter.variables[parameter] = value

            interpreter.interpret()
            if interpreter.stack.is_empty():
                self.stack.push(None)
            else:
                self.stack.push(interpreter.stack.pop())
        elif opcode == OpCode.POP_TOP:
            assert oparg is None
            self.stack.pop()
        elif opcode == OpCode.RETURN:
            assert oparg is None
            # The value should already be on the stack -- the calling interpreter
            # will get it.
            assert not self.stack.is_empty()
            self._index = len(self.code)  # Jump to the end
        elif opcode == OpCode.JUMP:
            assert isinstance(oparg, JumpLabel)
            assert oparg.index is not None
            self._index = oparg.index
        elif opcode == OpCode.JUMP_IF_TRUE:
            assert isinstance(oparg, JumpLabel)
            item = self.stack.pop()
            if bool(item):
                assert oparg.index is not None
                self._index = oparg.index
        elif opcode == OpCode.BINARY_OP:
            assert isinstance(oparg, str)
            operators = {
                "+": py_operators.add,
                "-": py_operators.sub,
                "*": py_operators.mul,
                "/": py_operators.truediv,
            }
            right = self.stack.pop()
            left = self.stack.pop()

            self.stack.push(operators[oparg](left, right))
        elif opcode == OpCode.COMPARE:
            assert isinstance(oparg, str)
            operators = {
                "==": py_operators.eq,
                "!=": py_operators.ne,
                ">": py_operators.gt,
                ">=": py_operators.ge,
                "<": py_operators.lt,
                "<=": py_operators.le,
            }

            right = self.stack.pop()
            left = self.stack.pop()

            self.stack.push(operators[oparg](left, right))
        else:
            raise RuntimeError(f"unknown opcode: {opcode}")

    def _populate_jump_label_indices(self) -> None:
        for index, item in enumerate(self.code):
            if isinstance(item, JumpLabel):
                item.index = index

    def interpret(self) -> None:
        self._populate_jump_label_indices()

        while self._index < len(self.code):
            code = self.code[self._index]
            if isinstance(code, Instruction):
                self.interpret_instruction(code)
            else:
                assert isinstance(code, JumpLabel)

            self._index += 1

    @classmethod
    def run(cls, module: Module) -> None:
        cls(module.codegen()).interpret()


def run(source: str) -> None:
    module = Parser.parse(source)
    Interpreter.run(module)


source = """
func fib(n) {
    if n <= 0 {
        return 0
    }

    if n == 1 {
        return 1
    }

    return fib(n - 1) + fib(n - 2)
}
print fib(10)
"""

run(source)
