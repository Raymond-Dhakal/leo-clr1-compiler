#!/usr/bin/env python3
"""Random Leo program generator + reference interpreter (oracle).
usage: gen.py SEED OUTDIR   -> writes OUTDIR/pSEED.leo, .in, .expect (stdout), .status (ok|rt|skip)
"""
import sys, random, math

M = 1 << 64
def wrap(x):
    x &= M - 1
    return x - M if x >= (1 << 63) else x
class RT(Exception): pass
class Skip(Exception): pass

def idiv(a, b):
    if b == 0: raise RT()
    if b == -1: return wrap(-a)
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q
def imod(a, b):
    if b == 0: raise RT()
    if b == -1: return 0
    return a - idiv(a, b) * b
def ipow(a, e):
    if e < 0: raise RT()
    r = 1
    for _ in range(e if e < 70 else 70):   # exponents are tiny in generated programs
        r = wrap(r * a)
    if e >= 70: raise Skip()
    return r
def fin(x):
    if not math.isfinite(x): raise Skip()
    return x

# ---------------- expression nodes: (kind, ..., ty) ----------------
PREC = {'OR':1,'AND':2,'==':3,'!=':3,'<':4,'>':4,'<=':4,'>=':4,'+':5,'~':5,'*':6,'/':6,'MOD':6,'^':7}
def prec(e):
    k = e[0]
    if k == 'bin': return PREC[e[1]]
    if k == 'un': return 8
    return 9
def ftxt(v): return '%.2f' % v

def emit(e, rnd, min_prec=0):
    k = e[0]
    if k == 'int': s = str(e[1])
    elif k == 'float': s = ftxt(e[1])
    elif k == 'bool': s = 'TRUE' if e[1] else 'FALSE'
    elif k == 'var': s = e[1]
    elif k == 'un':
        op = e[1]
        s = ('NOT ' if op == 'NOT' else '~') + emit(e[2], rnd, 8)
    else:
        op, l, r = e[1], e[2], e[3]
        p = PREC[op]
        if op == '^': lp, rp = 8, 7
        else: lp, rp = p, p + 1
        s = emit(l, rnd, lp) + ' ' + op + ' ' + emit(r, rnd, rp)
    if prec(e) < min_prec or (e[0] == 'bin' and rnd.random() < 0.08):
        s = '(' + s + ')'
    return s

def numtxt(e, rnd):
    """Literal text; negative numbers are written with the unary minus '~'."""
    if e[0] in ('int', 'float') and e[1] < 0:
        return '~' + (str(-e[1]) if e[0] == 'int' else ftxt(-e[1]))
    return emit(e, rnd)

def mk_bin(op, l, r):
    if op in ('<','>','<=','>=','==','!=','AND','OR'): ty = 'BOOL'
    else: ty = 'FLOAT' if 'FLOAT' in (l[-1], r[-1]) else 'INT'
    return ('bin', op, l, r, ty)
def mk_un(op, e):
    return ('un', op, e, 'BOOL' if op == 'NOT' else e[-1])

# ---------------- generator ----------------
class G:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.n = 0
        self.counters = 0
        self.ro = set()          # names the current FOR bodies must not assign (loop variables)
    STRS = [('x =', 'x ='), ('tab\\there', 'tab\there'), ('say \\"hi\\"', 'say "hi"'),
            ('100% done', '100% done'), ('', ''), ('a\\\\b', 'a\\b'), ('line\\nbreak', 'line\nbreak'), ('??/ ok', '??/ ok')]
    def fresh(self, base):
        self.n += 1
        return '%s%d' % (base, self.n)

    def lit(self, ty):
        r = self.r
        if ty == 'INT': return ('int', r.choice([0,1,1,2,2,3,4,5,7,10,12,25,100]), 'INT')
        if ty == 'FLOAT': return ('float', round(r.uniform(0, 9), 2) if r.random() < 0.8 else r.choice([0.0, 1.0, 2.0, 0.5]), 'FLOAT')
        return ('bool', r.random() < 0.5, 'BOOL')

    def expr(self, ty, depth, vars_):
        r = self.r
        cands = [v for v in vars_ if v[1] == ty]
        if ty == 'FLOAT': cands += [v for v in vars_ if v[1] == 'INT']
        if depth <= 0 or r.random() < 0.25:
            if cands and r.random() < 0.65:
                n, t = r.choice(cands)
                return ('var', n, t)
            return self.lit(ty)
        d = depth - 1
        if ty == 'INT':
            c = r.random()
            if c < 0.12: return mk_un('~', self.expr('INT', d, vars_))
            op = r.choice(['+','+','~','~','*','*','/','MOD','^'])
            if op == '^':
                ex = ('int', r.choice([0,1,2,2,3]), 'INT') if r.random() < 0.92 else self.expr('INT', d, vars_)
                return mk_bin('^', self.expr('INT', d, vars_), ex)
            rhs = self.expr('INT', d, vars_)
            if op in ('/', 'MOD') and r.random() < 0.85: rhs = ('int', r.choice([1,2,3,4,5,7,9,-1,-3]) if False else r.choice([1,2,3,4,5,7,9]), 'INT')
            return mk_bin(op, self.expr('INT', d, vars_), rhs)
        if ty == 'FLOAT':
            c = r.random()
            if c < 0.12: return mk_un('~', self.expr('FLOAT', d, vars_))
            op = r.choice(['+','~','*','/','^','+','*'])
            if op == '^':
                ex = r.choice([('int',2,'INT'), ('int',3,'INT'), ('float',2.0,'FLOAT'), ('int',1,'INT')])
                return mk_bin('^', self.expr('FLOAT', d, vars_), ex)
            lt = r.choice(['FLOAT', 'INT', 'FLOAT'])
            rt = 'FLOAT' if lt == 'INT' else r.choice(['FLOAT', 'INT'])
            rhs = self.expr(rt, d, vars_)
            if op == '/' and r.random() < 0.85: rhs = ('float', r.choice([0.5, 1.5, 2.0, 3.25, 4.0]), 'FLOAT')
            return mk_bin(op, self.expr(lt, d, vars_), rhs)
        # BOOL
        c = r.random()
        if c < 0.12: return mk_un('NOT', self.expr('BOOL', d, vars_))
        if c < 0.45:
            op = r.choice(['<','>','<=','>=','==','!='])
            lt, rt = r.choice(['INT','FLOAT']), r.choice(['INT','FLOAT'])
            return mk_bin(op, self.expr(lt, d, vars_), self.expr(rt, d, vars_))
        if c < 0.55:
            return mk_bin(r.choice(['==','!=']), self.expr('BOOL', d, vars_), self.expr('BOOL', d, vars_))
        return mk_bin(r.choice(['AND','OR']), self.expr('BOOL', d, vars_), self.expr('BOOL', d, vars_))

    def block(self, depth, vars_, nstmts):
        """vars_: list of (name, type) visible and assignable. Returns statement list."""
        r = self.r
        out = []
        vars_ = list(vars_)
        declared_here = set()
        for _ in range(nstmts):
            c = r.random()
            if c < 0.20:                                   # declaration (maybe shadowing)
                ty = r.choice(['INT','INT','FLOAT','BOOL'])
                name = r.choice([v[0] for v in vars_]) if (vars_ and depth > 0 and r.random() < 0.25) else self.fresh('v')
                if name in declared_here or (depth == 0 and any(v[0] == name for v in vars_)): name = self.fresh('v')
                declared_here.add(name)
                init = self.expr(ty, 2, vars_) if r.random() < 0.7 else None
                out.append(('decl', name, ty, init))
                vars_ = [v for v in vars_ if v[0] != name] + [(name, ty)] if True else vars_
                # note: at depth>0 a shadowing decl hides the outer var inside this block only
            elif c < 0.50 and [v for v in vars_ if v[0] not in self.ro]:   # assignment
                n, t = r.choice([v for v in vars_ if v[0] not in self.ro])
                out.append(('assign', n, self.expr(t, 3, vars_)))
            elif c < 0.68:
                items = []
                for _i in range(r.choice([1, 1, 2, 3, 4])):
                    if r.random() < 0.35: items.append(('str',) + r.choice(self.STRS))
                    else: items.append(self.expr(r.choice(['INT','FLOAT','BOOL']), 3, vars_))
                out.append(('print', items))
            elif c < 0.74 and [v for v in vars_ if v[0] not in self.ro]:
                n, t = r.choice([v for v in vars_ if v[0] not in self.ro])
                out.append(('input', n, r.choice([None, None] + [x for x in self.STRS if x[0] != '' or True])))
            elif c < 0.80 and depth < 3:                      # FOR loop
                declare = r.random() < 0.6
                ints = [v for v in vars_ if v[1] == 'INT' and v[0] not in self.ro]
                flts = [v for v in vars_ if v[1] == 'FLOAT' and v[0] not in self.ro]
                use_float = r.random() < 0.25
                if declare:
                    name = self.fresh('f'); ty = 'FLOAT' if use_float else 'INT'
                elif use_float and flts: name, ty = r.choice(flts)
                elif ints: name, ty = r.choice(ints)
                else: name = self.fresh('f'); ty = 'INT'; declare = True
                if ty == 'INT':
                    st = r.choice([1, 1, 2, 3, -1, -2, -3]); a = r.randint(-3, 6); b = r.randint(-3, 6)
                    lo = [('int', a, 'INT'), None]; start = ('int', a, 'INT'); lim = ('int', b, 'INT')
                    step = ('int', st, 'INT')
                else:
                    st = r.choice([0.5, 0.25, 1.0, 1, -0.5, -0.25, -1.0]); a = r.choice([0, 1, 2, 0.5, 3, -1]); b = r.choice([0, 1, 2, 0.5, 3, 2.5])
                    start = ('int', a, 'INT') if isinstance(a, int) else ('float', a, 'FLOAT')
                    lim = ('int', b, 'INT') if isinstance(b, int) else ('float', b, 'FLOAT')
                    step = ('int', st, 'INT') if isinstance(st, int) else ('float', st, 'FLOAT')
                if r.random() < 0.15: step = None        # default step 1
                # a variable step: declared just before, nonzero
                pre = []
                if step is not None and r.random() < 0.2:
                    sn = self.fresh('s')
                    lit = step if step[1] >= 0 else ('un', '~', (step[0], -step[1], step[2]), step[2])
                    pre.append(('decl', sn, step[2], lit))
                    vars_ = vars_ + [(sn, step[2])]
                    step = ('var', sn, step[2])
                body_vars = vars_ + ([(name, ty)] if declare else [])
                self.ro.add(name)
                body = self.block(depth + 1, body_vars, r.randint(1, 3))
                self.ro.discard(name)
                out += pre
                out.append(('for', name, ty, declare, start, lim, step, body))
            elif c < 0.86 and depth < 3:
                cond = self.expr('BOOL', 3, vars_)
                th = self.block(depth + 1, vars_, r.randint(0, 3))
                el = self.block(depth + 1, vars_, r.randint(0, 3)) if r.random() < 0.6 else None
                out.append(('if', cond, th, el))
            elif depth < 3:
                cn = self.fresh('c'); N = r.randint(0, 4)
                out.append(('decl', cn, 'INT', ('int', 0, 'INT')))
                body = self.block(depth + 1, vars_, r.randint(1, 3))
                kind = 'while' if r.random() < 0.5 else 'repeat'
                out.append((kind, cn, N, body))
            else:
                out.append(('print', [self.expr('INT', 2, vars_)]))
        return out

# ---------------- text output ----------------
def text(stmts, rnd, ind=0):
    pad = '    ' * ind
    L = []
    for s in stmts:
        k = s[0]
        if k == 'decl':
            L.append('%s%s %s%s' % (pad, s[2], s[1], '' if s[3] is None else ' <- ' + emit(s[3], rnd)))
        elif k == 'assign': L.append('%s%s <- %s' % (pad, s[1], emit(s[2], rnd)))
        elif k == 'print':
            L.append('%sPRINTN %s' % (pad, ', '.join('"%s"' % it[1] if it[0] == 'str' else emit(it, rnd) for it in s[1])))
        elif k == 'input':
            if s[2] is None: L.append('%sINPUT %s' % (pad, s[1]))
            else: L.append('%sINPUT "%s", %s' % (pad, s[2][0], s[1]))
        elif k == 'for':
            _, name, ty, declare, start, lim, step, body = s
            head = '%s%s <- %s TO %s' % (ty + ' ' if declare else '', name, numtxt(start, rnd), numtxt(lim, rnd))
            if step is not None: head += ' STEP ' + numtxt(step, rnd)
            L.append('%sFOR %s DO' % (pad, head if rnd.random() < 0.6 else '(' + head + ')'))
            L += text(body, rnd, ind + 1)
            L.append('%sEND' % pad)
        elif k == 'if':
            L.append('%sIF (%s) THEN' % (pad, emit(s[1], rnd)))
            L += text(s[2], rnd, ind + 1)
            if s[3] is not None:
                L.append('%sELSE' % pad); L += text(s[3], rnd, ind + 1)
            L.append('%sEND' % pad)
        elif k == 'while':
            L.append('%sWHILE (%s < %d) DO' % (pad, s[1], s[2]))
            L += text(s[3], rnd, ind + 1)
            L.append('%s    %s <- %s + 1' % (pad, s[1], s[1]))
            L.append('%sEND' % pad)
        elif k == 'repeat':
            L.append('%sREPEAT' % pad)
            L += text(s[3], rnd, ind + 1)
            L.append('%s    %s <- %s + 1' % (pad, s[1], s[1]))
            L.append('%sUNTIL (%s >= %d)' % (pad, s[1], s[2]))
    return L

# ---------------- oracle ----------------
class Interp:
    def __init__(self, inputs):
        self.scopes = [{}]; self.out = []; self.inp = list(inputs); self.pos = 0
    def look(self, n):
        for sc in reversed(self.scopes):
            if n in sc: return sc[n]
        raise Exception('undeclared ' + n)
    def num(self, v): return float(v)
    def ev(self, e):
        k = e[0]
        if k in ('int','float','bool'): return e[1]
        if k == 'var': return self.look(e[1])[1]
        if k == 'un':
            v = self.ev(e[2])
            if e[1] == 'NOT': return not v
            return wrap(-v) if e[3] == 'INT' else -v
        op = e[1]
        if op == 'AND':
            return self.ev(e[2]) and self.ev(e[3])
        if op == 'OR':
            return self.ev(e[2]) or self.ev(e[3])
        a, b = self.ev(e[2]), self.ev(e[3])
        lt, rt = e[2][-1], e[3][-1]
        if op in ('==','!=','<','>','<=','>='):
            if lt == 'BOOL': x, y = a, b
            elif lt == 'FLOAT' or rt == 'FLOAT': x, y = float(a), float(b)
            else: x, y = a, b
            return {'==':x==y,'!=':x!=y,'<':x<y,'>':x>y,'<=':x<=y,'>=':x>=y}[op]
        if e[4] == 'INT':
            if op == '+': return wrap(a + b)
            if op == '~': return wrap(a - b)
            if op == '*': return wrap(a * b)
            if op == '/': return idiv(a, b)
            if op == 'MOD': return imod(a, b)
            if op == '^': return ipow(a, b)
        x, y = float(a), float(b)
        if op == '+': return fin(x + y)
        if op == '~': return fin(x - y)
        if op == '*': return fin(x * y)
        if op == '/':
            if y == 0.0: raise RT()
            return fin(x / y)
        if op == '^':
            try: return fin(math.pow(x, y))
            except (OverflowError, ValueError): raise Skip()
        raise Exception(op)
    def conv(self, ty, v):
        return float(v) if ty == 'FLOAT' and not isinstance(v, bool) else v
    def run(self, stmts):
        for s in stmts: self.stmt(s)
    def blk(self, stmts):
        self.scopes.append({}); self.run(stmts); self.scopes.pop()
    def stmt(self, s):
        k = s[0]
        if k == 'decl':
            v = self.conv(s[2], self.ev(s[3])) if s[3] is not None else {'INT':0,'FLOAT':0.0,'BOOL':False}[s[2]]
            self.scopes[-1][s[1]] = [s[2], v]
        elif k == 'assign':
            cell = self.look(s[1]); cell[1] = self.conv(cell[0], self.ev(s[2]))
        elif k == 'print':
            n = len(s[1])
            for idx, it in enumerate(s[1]):
                if it[0] == 'str': txt = it[2]
                else:
                    v = self.ev(it); ty = it[-1]
                    txt = 'TRUE' if v is True else 'FALSE' if v is False else ('%g' % v) if ty == 'FLOAT' else str(v)
                self.out.append(txt + ('\n' if idx == n - 1 else ' '))
        elif k == 'for':
            _, name, ty, declare, start, lim, step, body = s
            a = self.ev(start); b = self.ev(lim); st = self.ev(step) if step is not None else 1
            if ty == 'FLOAT': a, b, st = float(a), float(b), float(st)
            self.scopes.append({})
            if declare: self.scopes[-1][name] = [ty, a]
            else: self.look(name)[1] = a
            cell = self.look(name)
            while (cell[1] <= b) if st > 0 else (cell[1] >= b):
                self.blk(body)
                cell[1] = wrap(cell[1] + st) if ty == 'INT' else cell[1] + st
            self.scopes.pop()
        elif k == 'input':
            if s[2] is not None: self.out.append(s[2][1])
            cell = self.look(s[1])
            tok = self.inp[self.pos]; self.pos += 1
            if cell[0] == 'BOOL':
                if tok in ('1','true','TRUE'): cell[1] = True
                elif tok in ('0','false','FALSE'): cell[1] = False
                else: raise RT()
            elif cell[0] == 'FLOAT': cell[1] = float(tok)
            else: cell[1] = int(tok)
        elif k == 'if':
            if self.ev(s[1]): self.blk(s[2])
            elif s[3] is not None: self.blk(s[3])
        elif k in ('while', 'repeat'):
            cn, N, body = s[1], s[2], s[3]
            if k == 'while':
                while self.look(cn)[1] < N:
                    self.scopes.append({}); self.run(body)
                    c = self.look(cn); c[1] = wrap(c[1] + 1); self.scopes.pop()
            else:
                while True:
                    self.scopes.append({}); self.run(body)
                    c = self.look(cn); c[1] = wrap(c[1] + 1); self.scopes.pop()
                    if self.look(cn)[1] >= N: break

def main():
    seed, outdir = int(sys.argv[1]), sys.argv[2]
    g = G(seed)
    top = []
    pool = []
    for i in range(g.r.randint(2, 5)):
        ty = g.r.choice(['INT','INT','FLOAT','BOOL'])
        nm = g.fresh('g')
        init = g.expr(ty, 1, pool) if g.r.random() < 0.6 else None
        top.append(('decl', nm, ty, init)); pool.append((nm, ty))
    prog = top + g.block(0, pool, g.r.randint(6, 14))
    inputs = [str(g.r.choice([0,1,2,3,5,7,-1,1,0,1])) for _ in range(400)]
    rnd = random.Random(seed + 1)
    src = '\n'.join(text(prog, rnd)) + '\n'
    it = Interp(inputs); status = 'ok'
    try: it.run(prog)
    except RT: status = 'rt'
    except Skip: status = 'skip'
    except RecursionError: status = 'skip'
    open('%s/p%d.leo' % (outdir, seed), 'w').write(src)
    open('%s/p%d.in' % (outdir, seed), 'w').write(' '.join(inputs) + '\n')
    open('%s/p%d.expect' % (outdir, seed), 'w').write(''.join(it.out))
    open('%s/p%d.status' % (outdir, seed), 'w').write(status + '\n')
main()
