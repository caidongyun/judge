from dmoj.executors.base_executor import CompiledExecutor


class Executor(CompiledExecutor):
    ext = '.ml'
    name = 'OCAML'
    command = 'ocaml'
    command_paths = ['ocamlopt']
    test_program = 'print_endline (input_line stdin)'

    def get_compile_args(self):
        return [self.get_command(), self._code, '-o', self.problem]
