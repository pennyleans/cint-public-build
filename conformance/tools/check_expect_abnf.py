"""Check .expect files against the ABNF of SPEC-09 9.2 CONF-11, formats 1 to 4, with its rules 1, 2, 5, 12 and 13.
Usage, from the repository root: python conformance/tools/check_expect_abnf.py [file ...]
No arguments: every conformance/**/*.expect. Exit status 1 if any file is rejected."""
import pathlib, re, sys
SEG = r"[A-Za-z0-9_.-]+"; PATH = SEG + r"(?:/" + SEG + r")*"; TEXT = r"[!-~](?: *[!-~])*"
CNT = r"(?:0|[1-9][0-9]*)"; INT = "-?" + CNT; POSN = PATH + r":[1-9][0-9]*:[1-9][0-9]*"
NAME = r"[A-Za-z_][A-Za-z0-9_]*"; MOD = NAME + r"(?:\." + NAME + r")*"   # CONF-12
TV = r"(?:(?:I(?:8|16|32|64|128|256|512|1024)|U(?:8|16|32|64)|Z) %s|Bool (?:true|false))" % INT
REC = lambda p: (r"{p}code E_[A-Z_]+\n{p}operation [a-z0-9_.]+\n(?:{p}operand {tv}\n)*"
                 r"{p}exact (?:{i}|none)\n{p}limit (?:{tv}|none)\n").format(p=re.escape(p), tv=TV, i=INT)
OUT = r"stdout-bytes %s\n(?:stdout-sha256 [0-9a-f]{64}\n)?" % CNT; FUEL = r"fuel-consumed %s\n" % CNT
DESC = r"(?:I(?:8|16|32|64|128|256|512|1024)|U(?:8|16|32|64)) (?:%s %s|none) %s %s(?: %s %s %s)+ (?:read|write)" % (
    CNT, CNT, CNT, INT, CNT, INT, INT)                                  # format 4: a view descriptor
KFAULT = (r"fault\.code E_[A-Z_]+\nfault\.operation [a-z0-9_.]+\n(?:fault\.operand {tv}\n)*(?:fault\.descriptor {d}\n)*"
          r"fault\.exact (?:{i}|none)\nfault\.limit (?:{tv}|none)\nfault\.position {p}\nfault\.revision self\n"
          r"fault\.source-map self\n(?:fault\.phase work-item\nfault\.kernel {k}\nfault\.address {c} {c} {c}\n|"
          r"fault\.phase (?:entry|epilogue)\nfault\.kernel {k}\nfault\.address {c} none none\n)"
          r"fault\.stack-depth {c}\n(?:fault\.stack {p}\n)*").format(
    tv=TV, d=DESC, i=INT, p=POSN, k=MOD + r"\." + NAME, c=CNT)        # format 4: a kernel fault (rule 13)
FAULT = REC("fault.") + (r"fault\.position %s\nfault\.revision self\n(?:fault\.source-map self\n)?"
                         r"fault\.address (?:none|%s %s %s)\nfault\.stack-depth %s\n(?:fault\.stack %s\n)*") % (
    POSN, CNT, CNT, CNT, CNT, POSN)
ERR = r"error\.set %s\nerror\.value (?:%s\.)?%s\nerror\.tag U(?:8|16|32|64) [1-9][0-9]*\n" % (
    MOD, NAME, NAME)                                                    # format 3: the error outcome (rule 12)
RUN = r"source (?:anchor|reference)\n(?:outcome value\n%s(?:return %s\n)?%s|outcome fault\n%s%s(?:%s|%s)|" \
    r"outcome error\n%s%s%s)" % (OUT, TV, FUEL, OUT, FUEL, FAULT, KFAULT, OUT, FUEL, ERR) + \
    r"(?:state\.global %s %s %s\n)*" % (MOD, NAME, TV)
COMP = (r"source compile-error\noutcome compile-error\ndiagnostic\.code C[0-9]{4}\n"
        r"diagnostic\.position %s\n(?:%s)?") % (POSN, REC("diagnostic.fault."))
REFUSED = r"source reference\noutcome refused\nrefused\.reason %s\n" % TEXT
FILE = re.compile(r"case %s\nclause %s\n(?:format [234]\n)?(?:%s|%s|%s)" % (PATH, TEXT, RUN, COMP, REFUSED))
def check(raw):
    try: text = raw.decode("ascii")
    except UnicodeDecodeError: return "not ASCII (rule 1)"
    if not FILE.fullmatch(text): return "does not match the grammar"
    m = re.search(r"^stdout-bytes ([0-9]+)$", text, re.M)
    if m and (m.group(1) != "0") != ("\nstdout-sha256 " in text): return "stdout-sha256 and stdout-bytes disagree (rule 2)"
    fmt = text.split("\n")[2]
    v2 = fmt in ("format 2", "format 3", "format 4")
    if not v2 and re.search(r"^(?:fault\.source-map|fault\.stack|state\.global) ", text, re.M):
        return "a format 2 line in a format 1 file (rules 5 and 10)"
    if fmt not in ("format 3", "format 4") and "\noutcome error\n" in text:
        return "an error outcome in a file below format 3 (rule 12)"
    if fmt != "format 4" and re.search(r"^(?:fault\.descriptor|fault\.phase|fault\.kernel) ", text, re.M):
        return "a format 4 line in a file below format 4 (rule 13)"
    m = re.search(r"^fault\.stack-depth ([0-9]+)$", text, re.M)
    if v2 and m and ("\nfault.source-map self\n" not in text or
                     int(m.group(1)) != len(re.findall(r"^fault\.stack ", text, re.M))):
        return "a format 2 fault record has fault.source-map and one fault.stack line per position (rule 5)"
paths = [pathlib.Path(a) for a in sys.argv[1:]] or sorted(pathlib.Path("conformance").rglob("*.expect"))
bad = [(p, check(p.read_bytes())) for p in paths]
bad = [(p, why) for p, why in bad if why]
for p, why in bad: print("rejected %s: %s" % (p.as_posix(), why))
print("checked %d, rejected %d" % (len(paths), len(bad)))
sys.exit(1 if bad else 0)
