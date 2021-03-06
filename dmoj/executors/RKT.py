from dmoj.executors.base_executor import CompiledExecutor
from dmoj.judgeenv import env


class Executor(CompiledExecutor):
    ext = '.rkt'
    name = 'RKT'
    fs = ['.*\.(?:rkt?$|zo$)', '.*racket.*', '/etc/nsswitch.conf$', '/etc/passwd$']

    raco = env['runtime'].get('raco')
    command = 'racket'

    syscalls = ['epoll_create', 'epoll_wait', 'poll',
                # PR_SET_NAME = 15
                ('prctl', lambda debugger: debugger.arg0 in (15,))]
    address_grace = 131072

    test_program = '''\
#lang racket
(displayln (read-line))
'''

    def get_compile_args(self):
        return [self.raco, 'make', self._code]

    def get_cmdline(self):
        return [self.get_command(), self._code]

    def get_executable(self):
        return self.get_command()

    @classmethod
    def initialize(cls, sandbox=True):
        if cls.raco is None:
            return False
        return super(Executor, cls).initialize(sandbox)
