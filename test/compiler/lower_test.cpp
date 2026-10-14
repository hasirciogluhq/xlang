#include "check.h"

#include "xlang/hir/hir.h"
#include "xlang/lang/frontend.h"
#include "xlang/sema/scope.h"
#include "xlang/sema/sema.h"

#include <sstream>

using namespace xlang;

namespace {

struct Lowered {
    SourceManager sources;
    std::ostringstream out;
    BuildConfig config;
    std::unique_ptr<DiagnosticEngine> diag;
    Program program;
    hir::Module module;
    std::string hir_text;

    explicit Lowered(std::string text, BuildConfig cfg = BuildConfig{}) : config(std::move(cfg)) {
        const FileId file = sources.addFile("t.xl", std::move(text));
        diag = std::make_unique<DiagnosticEngine>(sources, out);
        program = parseSource(sources, file, config, *diag);
        Sema sema(program, config, *diag);
        if (!diag->hasErrors()) {
            sema.analyze();
        }
        if (!diag->hasErrors()) {
            module = sema.lower();
            std::ostringstream h;
            hir::dumpModule(h, module);
            hir_text = h.str();
        }
        diag->flush();
    }

    [[nodiscard]] bool has(const std::string& needle) const {
        return hir_text.find(needle) != std::string::npos;
    }
    [[nodiscard]] bool failedWith(const std::string& code) const {
        return diag->hasErrors() && out.str().find(code) != std::string::npos;
    }
};

void testScopeStack() {
    ScopeStack s;
    s.push(ScopeKind::Module);
    ScopeEntry g;
    g.name = "x";
    g.kind = ScopeEntry::Kind::Global;
    CHECK(s.declare(g) == nullptr);
    CHECK(s.declare(g) != nullptr);  // second binding in the same scope reports the first
    s.push(ScopeKind::Function);
    ScopeEntry l;
    l.name = "x";
    l.kind = ScopeEntry::Kind::Local;
    CHECK(s.declare(l) == nullptr);  // inner scope shadows
    CHECK(s.lookup("x")->kind == ScopeEntry::Kind::Local);
    CHECK(s.lookupModule("x")->kind == ScopeEntry::Kind::Global);
    s.pop();
    CHECK(s.lookup("x")->kind == ScopeEntry::Kind::Global);
    CHECK(s.lookup("missing") == nullptr);
}

void testLowersPlainFunctions() {
    Lowered r("fn add(a: i32, b: i32) -> i32 { return a + b }\n"
              "fn main() -> i32 {\n"
              "    local x: i64 = 1\n"
              "    local y: i32 = add(1, 2)\n"
              "    return y\n"
              "}\n");
    CHECK(!r.diag->hasErrors());
    CHECK(r.has("fn add(a: i32, b: i32) -> i32"));
    CHECK(r.has("(call add 1:i32 2:i32):i32"));
    CHECK(r.has("local x#0: i64 = 1:i64"));  // literal takes the declared type
}

void testImplicitConversionsBecomeCasts() {
    Lowered r("fn main() -> i64 {\n"
              "    local a: i8 = 1\n"
              "    local b: i64 = a\n"
              "    return b + a\n"
              "}\n");
    CHECK(!r.diag->hasErrors());
    CHECK(r.has("(cast.implicit a#0):i64"));
}

void testConversionDiagnostics() {
    {
        Lowered r("fn f() { local n: ui8 = 5\n local m: i32 = n }\n");
        CHECK(r.failedWith("E0601"));
    }
    {
        Lowered r("fn f() { local n: ui8 = 5\n unsafe { local m: i32 = n } }\n");
        CHECK(!r.diag->hasErrors());
    }
    {
        Lowered r("fn f() { local n: i64 = 5\n local m: i8 = n }\n");
        CHECK(r.failedWith("E0708"));
    }
    {
        Lowered r("fn f() { local n: i32 = 5\n local m: f64 = n }\n");
        CHECK(!r.diag->hasErrors());
        CHECK(r.diag->warningCount() == 1);
        CHECK(r.out.str().find("W0601") != std::string::npos);
    }
    {
        Lowered r("fn f() { local n: i32 = 5\n local m: f64 = n as f64 }\n");
        CHECK(r.diag->warningCount() == 0);
    }
    {
        Lowered r("fn f() { local a: i32 = 1\n local b: ui32 = 2\n local c: i32 = a + b }\n");
        CHECK(r.failedWith("E0601"));
    }
}

void testNamesAndShadowing() {
    {
        Lowered r("fn f() { local x = nope }\n");
        CHECK(r.failedWith("E0706"));
    }
    {
        Lowered r("fn f(a: i32) { local a: i32 = 1 }\n");
        CHECK(r.failedWith("E0401"));
    }
    {
        Lowered r("fn f() -> i32 { local x: i32 = 1\n if true { local x: i32 = 2\n return x }\n return x }\n");
        CHECK(!r.diag->hasErrors());
    }
    {
        Lowered r("struct P { x: i32 }\nfn f() -> i32 { return P }\n");
        CHECK(r.failedWith("E0706"));
    }
}

void testCallsAndArity() {
    Lowered r("fn one(a: i32) -> i32 { return a }\nfn f() -> i32 { return one(1, 2) }\n");
    CHECK(r.failedWith("E0710"));
    Lowered p("fn apply(f: fn(i32) -> i32, v: i32) -> i32 { return f(v) }\n"
              "fn dbl(x: i32) -> i32 { return x * 2 }\n"
              "fn main() -> i32 { return apply(dbl, 3) }\n");
    CHECK(!p.diag->hasErrors());
    CHECK(p.has("call.indirect"));
    CHECK(p.has("&fn dbl"));
}

void testSyscallValidation() {
    {
        BuildConfig c;
        c.target = *parseTargetTriple("x86_64-unknown-linux-gnu");
        Lowered ok("fn f(p: *ui8) -> i64 { return syscall(1, 1, p, 3) }\n", c);
        CHECK(!ok.diag->hasErrors());
        CHECK(ok.has("(syscall 1:i32 1:i32 p#0 3:i32):i64"));
        Lowered many("fn f() -> i64 { return syscall(1, 1, 2, 3, 4, 5, 6, 7) }\n", c);
        CHECK(many.failedWith("E0710"));
        Lowered bad("fn f() -> i64 { return syscall(1, 1.5) }\n", c);
        CHECK(bad.failedWith("E0708"));
    }
}

void testFieldsPointersAndIndexing() {
    Lowered r("struct Point { x: i32, y: i32 }\n"
              "fn f(p: *Point) -> i32 {\n"
              "    local a: [i32; 4]\n"
              "    a[1] = p.x\n"
              "    p.y = *(&a[1])\n"
              "    return a[0]\n"
              "}\n");
    CHECK(!r.diag->hasErrors());
    CHECK(r.has("(field.ptr x p#0)"));
    CHECK(r.has("index.checked"));
    {
        Lowered b("fn f() { local a: [i32; 4]\n a[4] = 1 }\n");
        CHECK(b.failedWith("E0701"));
    }
    {
        BuildConfig c;
        c.safety = "unchecked";
        Lowered u("fn f(i: usize) { local a: [i32; 4]\n a[i] = 1 }\n", c);
        CHECK(!u.diag->hasErrors());
        CHECK(!u.has("index.checked"));
    }
    {
        Lowered b("struct P { x: i32 }\nfn f(p: P) { p.z = 1 }\n");
        CHECK(b.failedWith("E0709"));
    }
    {
        Lowered b("fn f() { [1, 2][0] = 1 }\n");
        CHECK(b.failedWith("E0709"));
    }
}

void testInterfacesAreNominal() {
    const std::string common =
        "interface Writer { fn write(self, n: i32) -> i32 }\n"
        "struct Out { fd: i32 }\n"
        "fn emit(w: Writer) -> i32 { return w.write(1) }\n";
    {
        Lowered r(common + "impl Writer for Out { fn write(self, n: i32) -> i32 { return self.fd + n } }\n"
                           "fn main() -> i32 { local o: Out = Out { fd: 1 }\n return emit(o) }\n");
        CHECK(!r.diag->hasErrors());
        CHECK(r.has("(call.itable write[0] w#0 1:i32):i32"));
        CHECK(r.has("to.interface Out"));
    }
    {
        Lowered r(common + "fn main() -> i32 { local o: Out = Out { fd: 1 }\n return emit(o) }\n");
        CHECK(r.failedWith("E0504"));
    }
}

void testMethodsAreFunctionsWithSelf() {
    Lowered r("class Shape {\n"
              "    id: i32\n"
              "    fn get(self) -> i32 { return self.id }\n"
              "    virtual fn area(self) -> f64 { return 0.0 }\n"
              "}\n"
              "fn f(s: *Shape) -> i32 { local v: f64 = s.area()\n return s.get() }\n");
    CHECK(!r.diag->hasErrors());
    CHECK(r.has("fn Shape.get(self: *Shape) -> i32"));
    CHECK(r.has("(call Shape.get s#0):i32"));
    CHECK(r.has("(call.virtual Shape.area s#0):f64"));
}

void testNewAndRegisteredAllocator() {
    const std::string types =
        "interface Allocator { fn alloc(self, size: usize, align: usize) -> *void\n fn free(self, p: *void) }\n"
        "class Heap { }\n"
        "impl Allocator for Heap {\n"
        "    fn alloc(self, size: usize, align: usize) -> *void { return nil }\n"
        "    fn free(self, p: *void) { }\n"
        "}\n"
        "struct Point { x: i32, y: i32 }\n";
    {
        Lowered r(types + "fn main() -> i32 { local p: *Point = new Point { x: 1, y: 2 }\n return p.x }\n");
        CHECK(r.failedWith("E0501"));
    }
    {
        Lowered r(types + "global heap: Heap\n"
                          "register(allocator) global current: Allocator = heap\n"
                          "fn main() -> i32 { local p: *Point = new Point { x: 1, y: 2 }\n return p.x }\n");
        CHECK(!r.diag->hasErrors());
        CHECK(r.has("(new Point registered"));
        CHECK(r.module.allocator_global == "current");
    }
    {
        Lowered r(types + "fn main() -> i32 { local h: Heap = Heap { }\n"
                          "local p: *Point = new(&h) Point { x: 1, y: 2 }\n return p.x }\n");
        CHECK(!r.diag->hasErrors());
        CHECK(r.has("(new Point"));
    }
    {
        Lowered r(types + "fn main() -> i32 { local pt: Point = Point { x: 0, y: 0 }\n"
                          "local p: *Point = new(&pt) Point { x: 1, y: 2 }\n return p.x }\n");
        CHECK(r.failedWith("E0504"));
    }
}

void testRegisterRoles() {
    {
        Lowered r("register(panic) fn panic(msg: *ui8) { }\nregister(panic) fn panic2(msg: *ui8) { }\n");
        CHECK(r.failedWith("E0502"));
    }
    {
        Lowered r("register(panic) fn panic(code: i32) { }\n");
        CHECK(r.failedWith("E0503"));
    }
    {
        Lowered r("register(panic) fn panic(msg: *ui8) { }\n");
        CHECK(!r.diag->hasErrors());
        CHECK(r.module.panic_function == "panic");
    }
    {
        Lowered r("register(panic) fn panic(msg: *ui8) { }\nfn f() { local panic: i32 = 1 }\n");
        CHECK(r.out.str().find("W0402") != std::string::npos);
    }
}

void testGlobalInitializersAreConstant() {
    {
        Lowered r("define N = 4\n"
                  "global table: [ui8; N]\n"
                  "global first: *ui8 = &table[0]\n"
                  "global limit: usize = N * 2\n"
                  "global banner: *ui8 = \"xlang\"\n");
        CHECK(!r.diag->hasErrors());
        CHECK(r.has("global table: [ui8; 4]"));
        CHECK(r.has("global limit: usize = (* 4:usize 2:usize):usize"));
    }
    {
        Lowered r("fn f() -> i32 { return 1 }\nglobal g: i32 = f()\n");
        CHECK(r.failedWith("E0707"));
    }
    {
        Lowered r("global a: i32 = 1\nglobal b: i32 = a\n");
        CHECK(r.failedWith("E0707"));
    }
}

void testImportsAreExternal() {
    Lowered r("import \"math\" as m\n"
              "from \"math\" import cube as c\n"
              "fn f() -> i32 { local x = m.square(3)\n local y = c(2)\n return 0 }\n");
    CHECK(!r.diag->hasErrors());
    CHECK(r.has("call.external math.square"));
    CHECK(r.has("call.external math.cube"));
    Lowered dup("import \"a\" as x\nimport \"b\" as x\n");
    CHECK(dup.failedWith("E0401"));
}

void testEnumsAndStaticAssertInBodies() {
    Lowered r("enum Color: i32 { Red, Green = 5 }\n"
              "fn f() -> i32 {\n"
              "    staticAssert(sizeOf(i32) == 4)\n"
              "    local c: Color = Color.Green\n"
              "    if c == Color.Red { return 1 }\n"
              "    return c as i32\n"
              "}\n");
    CHECK(!r.diag->hasErrors());
    CHECK(r.has("local c#0: Color = 5:Color"));
    Lowered bad("fn f() { staticAssert(sizeOf(i32) == 5) }\n");
    CHECK(bad.failedWith("E0702"));
}

}  // namespace

void runLowerTests() {
    testScopeStack();
    testLowersPlainFunctions();
    testImplicitConversionsBecomeCasts();
    testConversionDiagnostics();
    testNamesAndShadowing();
    testCallsAndArity();
    testSyscallValidation();
    testFieldsPointersAndIndexing();
    testInterfacesAreNominal();
    testMethodsAreFunctionsWithSelf();
    testNewAndRegisteredAllocator();
    testRegisterRoles();
    testGlobalInitializersAreConstant();
    testImportsAreExternal();
    testEnumsAndStaticAssertInBodies();
}
