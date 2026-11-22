import sys

class Ed:
    def __init__(self, path):
        self.path = path
        self.s = open(path, encoding='utf-8').read()
    def rep(self, a, b, count=1):
        n = self.s.count(a)
        if n == 0 or (count == 1 and n != 1):
            sys.exit(f"{self.path}: expected {count} match(es), got {n} for:\n{a[:200]}")
        self.s = self.s.replace(a, b)
    def save(self):
        open(self.path, 'w', encoding='utf-8', newline='').write(self.s)
