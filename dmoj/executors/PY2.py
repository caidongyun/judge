from dmoj.judgeenv import env
from .python_executor import PythonExecutor


class Executor(PythonExecutor):
    command = 'python'
    command_paths = ['python2.7', 'python2', 'python']
    test_program = "print __import__('sys').stdin.read()"
    name = 'PY2'
    fs = ['.*\.(?:py[co]?$)', '.*/lib(?:32|64)?/python[\d.]+/.*', '.*/lib/locale/']
    if 'python2dir' in env:
        fs += [str(env['python3dir'])]
