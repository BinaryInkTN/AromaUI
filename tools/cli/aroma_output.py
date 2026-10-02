"""Terminal colors and logging helpers."""

import sys

class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
    
    @classmethod
    def disable(cls):
        for attr in dir(cls):
            if not attr.startswith('_') and not callable(getattr(cls, attr)):
                val = getattr(cls, attr)
                if isinstance(val, str):
                    setattr(cls, attr, '')


class Logger:
    _verbose = False
    
    @classmethod
    def setup(cls, verbose: bool = False):
        cls._verbose = verbose
    
    @classmethod
    def step(cls, msg: str):
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} {Colors.BOLD}{msg}{Colors.ENDC}")
    
    @classmethod
    def success(cls, msg: str):
        print(f"{Colors.OKGREEN}OK {msg}{Colors.ENDC}")
    
    @classmethod
    def error(cls, msg: str):
        print(f"{Colors.FAIL}ERROR: {msg}{Colors.ENDC}", file=sys.stderr)
    
    @classmethod
    def info(cls, msg: str):
        if cls._verbose:
            print(f"{Colors.OKCYAN}INFO {msg}{Colors.ENDC}")
    
    @classmethod
    def warning(cls, msg: str):
        print(f"{Colors.WARNING}WARN: {msg}{Colors.ENDC}")
