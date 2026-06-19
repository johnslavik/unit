from unit.context import Context
from unit.opcode import OpCode
from unit import _core

class CompiledProcedure:
    def __init__(self, compiled_procedure: _core.CompiledProcedure) -> None:
        if __debug__ and not isinstance(compiled_procedure, _core.CompiledProcedure):
            raise TypeError(f"expected an internal compiled procedure, got {compiled_procedure!r}")

        self._compiled = compiled_procedure

    def write_object_file(self, path: str, format: int) -> None:
        self._compiled.write_object_file(path, format)


class Procedure:
    def __init__(self, name: str, *, context: Context | None = None) -> None:
        self.context = context or Context.current_or_new()
        self.name = name
        self._procedure = _core.Procedure(self.context._context, name)

    def _add_op_int(self, opcode: OpCode, oparg: int) -> None:
        self._procedure.add_operation(opcode.value, oparg)

    def _add_op(self, opcode: OpCode) -> None:
        self._add_op_int(opcode, 0)

    def load_integer(self, value: int, /) -> None:
        self._add_op_int(OpCode.LOAD_INTEGER, value)

    def store_local(self, id: int, /) -> None:
        self._add_op_int(OpCode.STORE_LOCAL, id)

    def load_local(self, id: int, /) -> None:
        self._add_op_int(OpCode.LOAD_LOCAL, id)

    def add(self) -> None:
        self._add_op(OpCode.ADD)

    def subtract(self) -> None:
        self._add_op(OpCode.SUBTRACT)

    def multiply(self) -> None:
        self._add_op(OpCode.MULTIPLY)

    def divide(self) -> None:
        self._add_op(OpCode.DIVIDE)

    def modulo(self) -> None:
        self._add_op(OpCode.MODULO)

    def exit(self) -> None:
        self._add_op(OpCode.EXIT)

    def return_value(self) -> None:
        self._add_op(OpCode.RETURN_VALUE)

    def load_argument(self, arg_number: int, /) -> None:
        self._add_op_int(OpCode.LOAD_ARGUMENT, arg_number)

    def call_name(self, name: str, num_args: int, /) -> None:
        self._procedure.add_call_name(name, num_args)

    def compare_equal(self) -> None:
        self._add_op(OpCode.COMPARE_EQUAL)

    def compare_not_equal(self) -> None:
        self._add_op(OpCode.COMPARE_NOT_EQUAL)

    def compare_greater(self) -> None:
        self._add_op(OpCode.COMPARE_GREATER)

    def compare_greater_equal(self) -> None:
        self._add_op(OpCode.COMPARE_GREATER_EQUAL)

    def compare_less(self) -> None:
        self._add_op(OpCode.COMPARE_LESS)

    def compare_less_equal(self) -> None:
        self._add_op(OpCode.COMPARE_LESS_EQUAL)

    def copy(self, offset_from_top: int, /) -> None:
        self._add_op_int(OpCode.COPY, offset_from_top)

    def swap(self, offset_from_top: int, /) -> None:
        self._add_op_int(OpCode.SWAP, offset_from_top)

    def pop(self, offset_from_top: int, /) -> None:
        self._add_op_int(OpCode.POP, offset_from_top)

    def address_of(self, id: int, /) -> None:
        self._add_op_int(OpCode.ADDRESS_OF, id)

    def read_bytes(self, num_bytes: int, /) -> None:
        self._add_op_int(OpCode.READ_BYTES, num_bytes)

    def write_bytes(self, num_bytes: int, /) -> None:
        self._add_op_int(OpCode.WRITE_BYTES, num_bytes)

    def compile(self, architecture: int) -> CompiledProcedure:
        return CompiledProcedure(self._procedure.compile(architecture))
