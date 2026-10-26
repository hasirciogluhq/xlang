// Plan 4.7: HIR -> LLVM IR, written from scratch over the typed HIR (plan 4.3).
// Value model: scalars (integers, floats, bool, pointers, function pointers, enums over scalars)
// are SSA values; aggregates (struct, class, array, interface fat pointers) live in memory and
// expressions of those types evaluate to the *address* of the object. Passing and returning
// aggregates is by pointer (callee gets its own copy / an sret slot).

#include "artifact.h"

#include "xlang/util/error.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Triple.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <map>
#include <set>

namespace xlang::codegen {

namespace {

/// An empty block has no terminator (`getTerminator()` must not be called on it).
[[nodiscard]] bool hasTerminator(const llvm::BasicBlock* bb) {
    return !bb->empty() && bb->back().isTerminator();
}

[[nodiscard]] bool isUnknownType(const Type& t) {
    return t.kind == TypeKind::Named;
}

class Gen {
public:
    Gen(const CodegenInput& input, Artifact::Impl& out)
        : in_(input), hm_(input.module), ctx_(*out.context), mod_(*out.module), B_(*out.context),
          AB_(*out.context), DL_(out.machine->createDataLayout()), out_(out) {}

    bool run();

private:
    // ---- types
    [[nodiscard]] unsigned ptrBits() const { return in_.config.target.pointer_width; }
    [[nodiscard]] llvm::Type* ptrTy() { return llvm::PointerType::getUnqual(ctx_); }
    [[nodiscard]] bool isMem(const Type& t) const;
    [[nodiscard]] llvm::Type* sty(const Type& t);   ///< SSA type of a scalar type
    [[nodiscard]] llvm::Type* mty(const Type& t);   ///< in-memory type of a scalar (bool -> i8)
    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> sizeAlign(const Type& t);
    [[nodiscard]] const TypeLayout* layoutOf(const Type& t);
    [[nodiscard]] llvm::FunctionType* fnTypeFor(const std::vector<Type>& params, const Type& ret);
    [[nodiscard]] bool isSigned(const Type& t) const;

    // ---- module level
    void declareFunction(const hir::Function& f);
    void declareGlobal(const hir::Global& g);
    void buildTables();
    void defineGlobal(const hir::Global& g);
    void defineFunction(const hir::Function& f);
    void emitEntry();
    llvm::GlobalVariable* stringGlobal(const std::string& text);
    llvm::Constant* vtablePointer(const std::string& class_name);
    llvm::Constant* itablePointer(const std::string& type_name, const std::string& iface_name);

    // ---- statements
    void genStmts(const std::vector<hir::Stmt>& body);
    void genStmt(const hir::Stmt& s);

    // ---- expressions
    llvm::Value* genScalar(const hir::Expr& e);
    llvm::Value* genAddr(const hir::Expr& e);
    llvm::Value* genMem(const hir::Expr& e);
    void genInto(const hir::Expr& e, llvm::Value* dest);
    llvm::Value* genCall(const hir::Expr& e, llvm::Value** mem_result);
    /// Calls the `init` function `key` on the object at `self` with the given arguments.
    void callInit(const std::string& key, llvm::Value* self, const std::vector<hir::ExprPtr>& args, const Span& span);
    llvm::Value* genCallCommon(llvm::FunctionType* fty, llvm::Value* callee, const std::vector<const hir::Expr*>& args,
                               llvm::Value* self, const Type& ret, llvm::Value** mem_result, const Span& at);
    llvm::Value* genSyscall(const hir::Expr& e);
    llvm::Value* genBinary(const hir::Expr& e);
    llvm::Value* genCast(const hir::Expr& e);
    llvm::Value* castScalar(llvm::Value* v, const Type& from, const Type& to);
    llvm::Value* genNew(const hir::Expr& e);
    llvm::Value* genAtomic(const hir::Expr& e);
    llvm::Value* genAsm(const hir::Expr& e);
    llvm::Constant* genConst(const hir::Expr& e);
    llvm::Value* loadScalar(const Type& t, llvm::Value* addr);
    void storeScalar(const Type& t, llvm::Value* value, llvm::Value* addr);
    void copyMem(llvm::Value* dest, llvm::Value* src, const Type& t);
    void zeroMem(llvm::Value* dest, std::uint64_t size, std::uint64_t align);
    llvm::Value* fieldAddr(llvm::Value* base, const Type& agg, std::uint32_t index);
    llvm::Value* byteOffset(llvm::Value* base, std::uint64_t offset);
    llvm::AllocaInst* makeAlloca(llvm::Type* ty, std::uint64_t align, const std::string& name = "");
    llvm::AllocaInst* makeTemp(const Type& t);
    void panicCall(const char* message);
    void checkedBranch(llvm::Value* bad, const char* message);
    void setLoc(const Span& span);
    void error(const Span& at, const std::string& message);
    [[nodiscard]] llvm::Function* functionNamed(const std::string& name);
    [[nodiscard]] const hir::Interface* interfaceNamed(const std::string& name) const;
    [[nodiscard]] const hir::Aggregate* aggregateNamed(const std::string& name) const;
    void ensureTerminated();

    const CodegenInput& in_;
    const hir::Module& hm_;
    llvm::LLVMContext& ctx_;
    llvm::Module& mod_;
    llvm::IRBuilder<> B_;
    llvm::IRBuilder<> AB_;  ///< inserts allocas at the start of the entry block (plan 5.1)
    llvm::DataLayout DL_;
    Artifact::Impl& out_;

    std::map<std::string, llvm::Function*> fns_;
    std::map<std::string, llvm::GlobalVariable*> globals_;
    std::map<std::string, llvm::GlobalVariable*> vtables_;
    std::map<std::string, llvm::GlobalVariable*> itables_;  ///< key: type + "|" + interface
    std::map<std::string, llvm::GlobalVariable*> strings_;
    std::map<std::string, const hir::Interface*> interfaces_;
    std::map<std::string, const hir::Aggregate*> aggregates_;
    std::map<std::string, const hir::Function*> hir_fns_;
    std::map<std::string, const hir::Global*> hir_globals_;
    std::map<std::string, std::unique_ptr<TypeLayout>> layouts_;

    // per-function state
    const hir::Function* cur_{nullptr};
    llvm::Function* cur_fn_{nullptr};
    std::vector<llvm::Value*> locals_;
    llvm::Value* sret_{nullptr};
    struct LoopTargets {
        llvm::BasicBlock* cont;  ///< where `continue` goes (step block, or the condition)
        llvm::BasicBlock* brk;   ///< where `break` goes
    };
    std::vector<LoopTargets> loops_;  ///< enclosing loops, innermost last
    bool failed_{false};

    // debug info
    std::unique_ptr<llvm::DIBuilder> di_;
    llvm::DICompileUnit* cu_{nullptr};
    std::map<FileId, llvm::DIFile*> di_files_;
    llvm::DISubprogram* cur_sp_{nullptr};
    llvm::DIFile* di_file(FileId id);
    llvm::DIType* di_basic(const Type& t);
};

// ---------------------------------------------------------------------------------------------
// Types

bool Gen::isMem(const Type& t) const {
    return t.kind == TypeKind::Struct || t.kind == TypeKind::Class || t.kind == TypeKind::Array ||
           t.kind == TypeKind::Interface || t.kind == TypeKind::Str;
}

bool Gen::isSigned(const Type& t) const {
    if (t.kind == TypeKind::Enum && t.elem) {
        return isSigned(*t.elem);
    }
    return t.kind == TypeKind::Int && t.is_signed;
}

llvm::Type* Gen::sty(const Type& t) {
    switch (t.kind) {
    case TypeKind::Void: return llvm::Type::getVoidTy(ctx_);
    case TypeKind::Bool: return llvm::Type::getInt1Ty(ctx_);
    case TypeKind::Int: return llvm::Type::getIntNTy(ctx_, t.bits == 0 ? ptrBits() : t.bits);
    case TypeKind::Float: return t.bits == 32 ? llvm::Type::getFloatTy(ctx_) : llvm::Type::getDoubleTy(ctx_);
    case TypeKind::Pointer:
    case TypeKind::FnPtr: return ptrTy();
    case TypeKind::Enum: return t.elem ? sty(*t.elem) : llvm::Type::getInt32Ty(ctx_);
    default: return ptrTy();  // aggregates are handled through their address
    }
}

llvm::Type* Gen::mty(const Type& t) {
    llvm::Type* s = sty(t);
    return s->isIntegerTy(1) ? llvm::Type::getInt8Ty(ctx_) : s;
}

const TypeLayout* Gen::layoutOf(const Type& t) {
    const std::string key = typeToString(t);
    if (const auto it = layouts_.find(key); it != layouts_.end()) {
        return it->second.get();
    }
    auto l = in_.layout(t);
    if (!l) {
        return nullptr;
    }
    return (layouts_[key] = std::make_unique<TypeLayout>(std::move(*l))).get();
}

std::pair<std::uint64_t, std::uint64_t> Gen::sizeAlign(const Type& t) {
    switch (t.kind) {
    case TypeKind::Bool: return {1, 1};
    case TypeKind::Int: {
        const unsigned bits = t.bits == 0 ? ptrBits() : t.bits;
        return {bits / 8, bits / 8};
    }
    case TypeKind::Float: return {t.bits / 8, t.bits / 8};
    case TypeKind::Pointer:
    case TypeKind::FnPtr: return {ptrBits() / 8, ptrBits() / 8};
    case TypeKind::Enum: return t.elem ? sizeAlign(*t.elem) : std::make_pair<std::uint64_t, std::uint64_t>(4, 4);
    default: break;
    }
    if (const TypeLayout* l = layoutOf(t)) {
        return {l->size, l->align};
    }
    return {0, 1};
}

llvm::FunctionType* Gen::fnTypeFor(const std::vector<Type>& params, const Type& ret) {
    std::vector<llvm::Type*> ps;
    llvm::Type* r = llvm::Type::getVoidTy(ctx_);
    if (isMem(ret)) {
        ps.push_back(ptrTy());  // sret
    } else if (!ret.isVoid()) {
        r = sty(ret);
    }
    for (const Type& p : params) {
        ps.push_back(isMem(p) ? ptrTy() : sty(p));
    }
    return llvm::FunctionType::get(r, ps, false);
}

void Gen::error(const Span& at, const std::string& message) {
    failed_ = true;
    in_.diagnostics.error("E0714", at, message, "code generation");
}

void Gen::setLoc(const Span& span) {
    if (di_ && cur_sp_ != nullptr && span.valid()) {
        B_.SetCurrentDebugLocation(llvm::DILocation::get(ctx_, span.line, span.column, cur_sp_));
    }
}

llvm::DIFile* Gen::di_file(FileId id) {
    if (const auto it = di_files_.find(id); it != di_files_.end()) {
        return it->second;
    }
    std::string path = "<unknown>";
    if (id != kNoFile && id < in_.sources.fileCount()) {
        path = in_.sources.file(id).path().string();
    }
    const std::filesystem::path p(path);
    llvm::DIFile* f = di_->createFile(p.filename().string(), p.parent_path().string());
    di_files_[id] = f;
    return f;
}

llvm::DIType* Gen::di_basic(const Type& t) {
    const std::string name = typeToString(t);
    switch (t.kind) {
    case TypeKind::Bool: return di_->createBasicType(name, 8, llvm::dwarf::DW_ATE_boolean);
    case TypeKind::Int: {
        const unsigned bits = t.bits == 0 ? ptrBits() : t.bits;
        return di_->createBasicType(name, bits, t.is_signed ? llvm::dwarf::DW_ATE_signed : llvm::dwarf::DW_ATE_unsigned);
    }
    case TypeKind::Float: return di_->createBasicType(name, t.bits, llvm::dwarf::DW_ATE_float);
    case TypeKind::Void: return nullptr;
    default: return di_->createBasicType(name, ptrBits(), llvm::dwarf::DW_ATE_address);
    }
}

// ---------------------------------------------------------------------------------------------
// Helpers

llvm::AllocaInst* Gen::makeAlloca(llvm::Type* ty, std::uint64_t align, const std::string& name) {
    llvm::BasicBlock& entry = cur_fn_->getEntryBlock();
    AB_.SetInsertPoint(&entry, entry.begin());
    AB_.SetCurrentDebugLocation(llvm::DebugLoc());  // never inherit another instruction's location
    llvm::AllocaInst* a = AB_.CreateAlloca(ty, nullptr, name);
    if (align != 0) {
        a->setAlignment(llvm::Align(align));
    }
    return a;
}

llvm::AllocaInst* Gen::makeTemp(const Type& t) {
    if (isMem(t)) {
        const auto [size, align] = sizeAlign(t);
        return makeAlloca(llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx_), std::max<std::uint64_t>(size, 1)), align, "tmp");
    }
    llvm::Type* m = mty(t);
    return makeAlloca(m, DL_.getABITypeAlign(m).value(), "tmp");
}

llvm::Value* Gen::loadScalar(const Type& t, llvm::Value* addr) {
    llvm::Type* m = mty(t);
    llvm::LoadInst* l = B_.CreateAlignedLoad(m, addr, llvm::Align(DL_.getABITypeAlign(m).value()));
    if (sty(t)->isIntegerTy(1)) {
        return B_.CreateICmpNE(l, llvm::ConstantInt::get(m, 0));
    }
    return l;
}

void Gen::storeScalar(const Type& t, llvm::Value* value, llvm::Value* addr) {
    llvm::Type* m = mty(t);
    if (sty(t)->isIntegerTy(1)) {
        value = B_.CreateZExt(value, m);
    }
    B_.CreateAlignedStore(value, addr, llvm::Align(DL_.getABITypeAlign(m).value()));
}

void Gen::copyMem(llvm::Value* dest, llvm::Value* src, const Type& t) {
    const auto [size, align] = sizeAlign(t);
    if (size == 0) {
        return;
    }
    B_.CreateMemCpy(dest, llvm::Align(align), src, llvm::Align(align), size);
}

void Gen::zeroMem(llvm::Value* dest, std::uint64_t size, std::uint64_t align) {
    if (size == 0) {
        return;
    }
    B_.CreateMemSet(dest, B_.getInt8(0), size, llvm::Align(std::max<std::uint64_t>(align, 1)));
}

llvm::Value* Gen::byteOffset(llvm::Value* base, std::uint64_t offset) {
    if (offset == 0) {
        return base;
    }
    return B_.CreateInBoundsGEP(llvm::Type::getInt8Ty(ctx_), base, {B_.getInt64(offset)});
}

llvm::Value* Gen::fieldAddr(llvm::Value* base, const Type& agg, std::uint32_t index) {
    const TypeLayout* l = layoutOf(agg);
    if (l == nullptr || index >= l->fields.size()) {
        error(Span{}, std::format("no layout for `{}`", typeToString(agg)));
        return base;
    }
    return byteOffset(base, l->fields[index].offset);
}

llvm::GlobalVariable* Gen::stringGlobal(const std::string& text) {
    if (const auto it = strings_.find(text); it != strings_.end()) {
        return it->second;
    }
    llvm::Constant* data = llvm::ConstantDataArray::getString(ctx_, text, true);
    auto* g = new llvm::GlobalVariable(mod_, data->getType(), true, llvm::GlobalValue::PrivateLinkage, data, ".str");
    g->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    g->setAlignment(llvm::Align(1));
    return strings_[text] = g;
}

llvm::Function* Gen::functionNamed(const std::string& name) {
    const auto it = fns_.find(name);
    return it == fns_.end() ? nullptr : it->second;
}

const hir::Interface* Gen::interfaceNamed(const std::string& name) const {
    const auto it = interfaces_.find(name);
    return it == interfaces_.end() ? nullptr : it->second;
}

const hir::Aggregate* Gen::aggregateNamed(const std::string& name) const {
    const auto it = aggregates_.find(name);
    return it == aggregates_.end() ? nullptr : it->second;
}

void Gen::panicCall(const char* message) {
    llvm::Function* panic = hm_.panic_function.empty() ? nullptr : functionNamed(hm_.panic_function);
    if (panic != nullptr) {
        B_.CreateCall(panic, {stringGlobal(message)});
    } else {
        B_.CreateCall(llvm::Intrinsic::getOrInsertDeclaration(&mod_, llvm::Intrinsic::trap));
    }
    B_.CreateUnreachable();
}

/// `if (bad) panic(message)`; emission continues after the check.
void Gen::checkedBranch(llvm::Value* bad, const char* message) {
    llvm::BasicBlock* fail = llvm::BasicBlock::Create(ctx_, "panic", cur_fn_);
    llvm::BasicBlock* ok = llvm::BasicBlock::Create(ctx_, "ok", cur_fn_);
    B_.CreateCondBr(bad, fail, ok);
    B_.SetInsertPoint(fail);
    panicCall(message);
    B_.SetInsertPoint(ok);
}

void Gen::ensureTerminated() {
    llvm::BasicBlock* bb = B_.GetInsertBlock();
    if (bb == nullptr || hasTerminator(bb)) {
        return;
    }
    llvm::Type* rt = cur_fn_->getReturnType();
    if (rt->isVoidTy()) {
        B_.CreateRetVoid();
    } else {
        B_.CreateRet(llvm::Constant::getNullValue(rt));
    }
}

// ---------------------------------------------------------------------------------------------
// Module level

/// Per-module mode: does this module define (not just declare) the item of module `key`?
static bool definedHere(const CodegenInput& in, const std::string& key) {
    return !in.only_module || *in.only_module == key;
}

void Gen::declareFunction(const hir::Function& f) {
    std::vector<Type> params;
    for (std::uint32_t i = 0; i < f.param_count; ++i) {
        params.push_back(f.locals[i].type);
    }
    llvm::FunctionType* fty = fnTypeFor(params, f.ret);
    llvm::GlobalValue::LinkageTypes link = llvm::GlobalValue::InternalLinkage;
    const bool root_main = f.module.empty() && f.name == "main";
    const bool start_fn = !hm_.start_function.empty() && f.name == hm_.start_function;
    if (f.exported || f.declared || root_main || start_fn || !f.has_body || in_.only_module) {
        link = llvm::GlobalValue::ExternalLinkage;  // per-module objects reference each other
    }
    llvm::Function* fn = llvm::Function::Create(fty, link, f.symbol.empty() ? f.name : f.symbol, mod_);
    fn->addFnAttr(llvm::Attribute::NoUnwind);
    // No implicit library-call idioms: `memcpy`/`memset` are ordinary xl code (plan 7.2).
    fn->addFnAttr("no-builtins");
    // Frame pointers everywhere: stdlib context switches rely on a stable frame layout.
    fn->addFnAttr("frame-pointer", "all");
    if (isMem(f.ret)) {
        fn->addParamAttr(0, llvm::Attribute::get(ctx_, llvm::Attribute::NoAlias));
    }
    if (!hm_.panic_function.empty() && f.name == hm_.panic_function) {
        fn->addFnAttr(llvm::Attribute::NoReturn);
    }
    fns_[f.name] = fn;
    hir_fns_[f.name] = &f;
}

void Gen::declareGlobal(const hir::Global& g) {
    const auto [size, align] = sizeAlign(g.type);
    llvm::Type* ty = isMem(g.type) ? static_cast<llvm::Type*>(llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx_),
                                                                                    std::max<std::uint64_t>(size, 1)))
                                   : mty(g.type);
    const bool here = definedHere(in_, g.module);
    llvm::Constant* init = (g.declared || !here) ? nullptr : llvm::Constant::getNullValue(ty);
    const bool external = g.exported || g.declared || in_.only_module;
    auto* gv = new llvm::GlobalVariable(mod_, ty, g.constant,
                                        external ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage,
                                        init, g.symbol.empty() ? g.name : g.symbol);
    gv->setAlignment(llvm::Align(std::max<std::uint64_t>(align, 1)));
    globals_[g.name] = gv;
    hir_globals_[g.name] = &g;
}

llvm::Constant* Gen::vtablePointer(const std::string& class_name) {
    const auto it = vtables_.find(class_name);
    if (it == vtables_.end()) {
        return nullptr;
    }
    const hir::Aggregate* a = aggregateNamed(class_name);
    llvm::GlobalVariable* gv = it->second;
    if (a != nullptr && a->repr_c) {
        // Itanium: [offset-to-top, typeinfo, functions...]; the vptr points at the first function.
        return llvm::ConstantExpr::getInBoundsGetElementPtr(gv->getValueType(), gv,
                                                            llvm::ArrayRef<llvm::Constant*>{
                                                                llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 0),
                                                                llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), 2)});
    }
    return gv;
}

llvm::Constant* Gen::itablePointer(const std::string& type_name, const std::string& iface_name) {
    const auto it = itables_.find(type_name + "|" + iface_name);
    return it == itables_.end() ? nullptr : it->second;
}

void Gen::buildTables() {
    llvm::PointerType* pty = llvm::PointerType::getUnqual(ctx_);
    for (const hir::Aggregate& a : hm_.aggregates) {
        aggregates_[a.name] = &a;
    }
    for (const hir::Interface& i : hm_.interfaces) {
        interfaces_[i.name] = &i;
    }
    for (const hir::Aggregate& a : hm_.aggregates) {
        if (!a.is_class || a.vtable.empty()) {
            continue;
        }
        std::vector<llvm::Constant*> slots;
        if (a.repr_c) {
            slots.push_back(llvm::Constant::getNullValue(pty));  // offset-to-top (0)
            slots.push_back(llvm::Constant::getNullValue(pty));  // typeinfo: not provided (plan decision pending)
        }
        for (const hir::VtableSlot& s : a.vtable) {
            llvm::Function* f = functionNamed(s.function);
            slots.push_back(f != nullptr ? static_cast<llvm::Constant*>(f) : llvm::Constant::getNullValue(pty));
        }
        auto* ty = llvm::ArrayType::get(pty, slots.size());
        auto* gv = new llvm::GlobalVariable(mod_, ty, true,
                                            in_.only_module ? llvm::GlobalValue::LinkOnceODRLinkage
                                                            : llvm::GlobalValue::InternalLinkage,
                                            llvm::ConstantArray::get(ty, slots), "vtable." + a.name);
        gv->setAlignment(llvm::Align(ptrBits() / 8));
        vtables_[a.name] = gv;
    }
    for (const hir::Impl& im : hm_.impls) {
        std::vector<llvm::Constant*> slots;
        for (const std::string& fname : im.methods) {
            llvm::Function* f = functionNamed(fname);
            slots.push_back(f != nullptr ? static_cast<llvm::Constant*>(f) : llvm::Constant::getNullValue(pty));
        }
        auto* ty = llvm::ArrayType::get(pty, slots.size());
        auto* gv = new llvm::GlobalVariable(mod_, ty, true,
                                            in_.only_module ? llvm::GlobalValue::LinkOnceODRLinkage
                                                            : llvm::GlobalValue::InternalLinkage,
                                            llvm::ConstantArray::get(ty, slots), "itable." + im.type + "." + im.interface);
        gv->setAlignment(llvm::Align(ptrBits() / 8));
        itables_[im.type + "|" + im.interface] = gv;
    }
}

llvm::Constant* Gen::genConst(const hir::Expr& e) {
    switch (e.kind) {
    case hir::ExprKind::StructLit: {
        const TypeLayout* l = layoutOf(e.type);
        if (l == nullptr) {
            error(e.span, std::format("no layout for `{}`", typeToString(e.type)));
            return nullptr;
        }
        // Build the object as a packed struct of [padding, field, padding, ...] by offset.
        struct Part {
            std::uint64_t offset;
            llvm::Constant* value;
            std::uint64_t size;
        };
        std::vector<Part> parts;
        if (l->has_vptr) {
            if (llvm::Constant* vp = vtablePointer(e.type.path.front())) {
                parts.push_back({0, vp, ptrBits() / 8});
            }
        }
        for (const hir::FieldInit& f : e.fields) {
            llvm::Constant* c = genConst(*f.value);
            if (c == nullptr) {
                return nullptr;
            }
            const FieldLayout& fl = l->fields[f.index];
            parts.push_back({fl.offset, c, fl.size});
        }
        std::sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) { return a.offset < b.offset; });
        std::vector<llvm::Constant*> elems;
        std::vector<llvm::Type*> types;
        std::uint64_t cursor = 0;
        const auto pad = [&](std::uint64_t to) {
            if (to > cursor) {
                auto* at = llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx_), to - cursor);
                elems.push_back(llvm::ConstantAggregateZero::get(at));
                types.push_back(at);
                cursor = to;
            }
        };
        for (const Part& p : parts) {
            pad(p.offset);
            elems.push_back(p.value);
            types.push_back(p.value->getType());
            cursor = p.offset + p.size;
        }
        pad(l->size);
        auto* st = llvm::StructType::get(ctx_, types, true);
        return llvm::ConstantStruct::get(st, elems);
    }
    case hir::ExprKind::ArrayList:
    case hir::ExprKind::ArrayRepeat: {
        const TypeLayout* l = layoutOf(e.type);
        if (l == nullptr || !e.type.elem) {
            error(e.span, "array without layout");
            return nullptr;
        }
        std::vector<llvm::Constant*> elems;
        if (e.kind == hir::ExprKind::ArrayList) {
            for (const hir::ExprPtr& a : e.args) {
                llvm::Constant* c = genConst(*a);
                if (c == nullptr) {
                    return nullptr;
                }
                elems.push_back(c);
            }
        } else {
            llvm::Constant* c = genConst(*e.lhs);
            if (c == nullptr) {
                return nullptr;
            }
            elems.assign(e.int_value, c);
        }
        if (elems.empty()) {
            return llvm::ConstantAggregateZero::get(llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx_), 1));
        }
        // Each element is its own packed unit; keep them in a struct so mixed element types fit.
        std::vector<llvm::Type*> types;
        for (llvm::Constant* c : elems) {
            types.push_back(c->getType());
        }
        auto* st = llvm::StructType::get(ctx_, types, true);
        return llvm::ConstantStruct::get(st, elems);
    }
    case hir::ExprKind::ToInterface: {
        llvm::Constant* data = genConst(*e.lhs);
        llvm::Constant* itable = itablePointer(e.text, e.type.path.front());
        if (data == nullptr || itable == nullptr) {
            error(e.span, "interface constant without an itable");
            return nullptr;
        }
        auto* st = llvm::StructType::get(ctx_, {ptrTy(), ptrTy()}, true);
        return llvm::ConstantStruct::get(st, {data, itable});
    }
    case hir::ExprKind::StrConst: {
        llvm::GlobalVariable* bytes = stringGlobal(e.text);
        if (e.type.kind != TypeKind::Str) {
            return bytes;
        }
        auto* st = llvm::StructType::get(ctx_, {ptrTy(), llvm::Type::getInt64Ty(ctx_)}, true);
        return llvm::ConstantStruct::get(
            st, {bytes, llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), e.text.size())});
    }
    case hir::ExprKind::Nil:
        return llvm::Constant::getNullValue(ptrTy());
    case hir::ExprKind::FuncRef: {
        llvm::Function* f = functionNamed(e.text);
        if (f == nullptr) {
            error(e.span, std::format("unknown function `{}`", e.text));
        }
        return f;
    }
    case hir::ExprKind::BoolConst:
        return llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx_), e.bool_value ? 1 : 0);
    case hir::ExprKind::AddrOf: {
        // &global, &global[const], &global.field
        const hir::Expr* cur = e.lhs.get();
        std::uint64_t offset = 0;
        while (cur != nullptr && cur->kind != hir::ExprKind::GlobalRef) {
            if (cur->kind == hir::ExprKind::Index) {
                const auto [size, align] = sizeAlign(cur->type);
                (void)align;
                offset += size * cur->rhs->int_value;
                cur = cur->lhs.get();
            } else if (cur->kind == hir::ExprKind::Field) {
                const TypeLayout* l = layoutOf(cur->lhs->type);
                if (l == nullptr) {
                    return nullptr;
                }
                offset += l->fields[cur->index].offset;
                cur = cur->lhs.get();
            } else {
                break;
            }
        }
        if (cur == nullptr || cur->kind != hir::ExprKind::GlobalRef) {
            error(e.span, "unsupported address constant");
            return nullptr;
        }
        const auto it = globals_.find(cur->text);
        if (it == globals_.end()) {
            error(e.span, std::format("unknown global `{}`", cur->text));
            return nullptr;
        }
        llvm::Constant* base = it->second;
        if (offset == 0) {
            return base;
        }
        return llvm::ConstantExpr::getInBoundsGetElementPtr(llvm::Type::getInt8Ty(ctx_), base,
                                                            llvm::ArrayRef<llvm::Constant*>{llvm::ConstantInt::get(
                                                                llvm::Type::getInt64Ty(ctx_), offset)});
    }
    case hir::ExprKind::IntConst:
    case hir::ExprKind::FloatConst:
    default: {
        // Scalar constant expressions fold through the IR builder (no insertion point).
        B_.ClearInsertionPoint();
        llvm::Value* v = genScalar(e);
        auto* c = v != nullptr ? llvm::dyn_cast<llvm::Constant>(v) : nullptr;
        if (c == nullptr) {
            error(e.span, "initializer is not a constant");
            return nullptr;
        }
        if (c->getType()->isIntegerTy(1)) {
            return llvm::ConstantInt::get(llvm::Type::getInt8Ty(ctx_), c->isOneValue() ? 1 : 0);
        }
        return c;
    }
    }
}

void Gen::defineGlobal(const hir::Global& g) {
    llvm::GlobalVariable* old = globals_[g.name];
    if (!g.init || g.declared) {
        return;
    }
    cur_fn_ = nullptr;
    llvm::Constant* init = genConst(*g.init);
    if (init == nullptr) {
        return;
    }
    if (init->getType() == old->getValueType()) {
        old->setInitializer(init);
        return;
    }
    // The initializer has its own (packed struct) type: replace the placeholder variable.
    auto* gv = new llvm::GlobalVariable(mod_, init->getType(), old->isConstant(), old->getLinkage(), init, "");
    gv->setAlignment(old->getAlign().value_or(llvm::Align(1)));
    gv->takeName(old);
    old->replaceAllUsesWith(gv);
    old->eraseFromParent();
    globals_[g.name] = gv;
}

// ---------------------------------------------------------------------------------------------
// Functions and statements

void Gen::defineFunction(const hir::Function& f) {
    if (!f.has_body) {
        return;
    }
    llvm::Function* fn = functionNamed(f.name);
    cur_ = &f;
    cur_fn_ = fn;
    locals_.assign(f.locals.size(), nullptr);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(ctx_, "entry", fn);
    B_.SetInsertPoint(entry);

    if (di_) {
        std::vector<llvm::Metadata*> sig;
        sig.push_back(di_basic(f.ret));
        for (std::uint32_t i = 0; i < f.param_count; ++i) {
            sig.push_back(di_basic(f.locals[i].type));
        }
        llvm::DIFile* file = di_file(f.span.file);
        llvm::DISubroutineType* st = di_->createSubroutineType(di_->getOrCreateTypeArray(sig));
        cur_sp_ = di_->createFunction(file, f.name, fn->getName(), file, f.span.line, st, f.span.line,
                                      llvm::DINode::FlagPrototyped, llvm::DISubprogram::SPFlagDefinition);
        fn->setSubprogram(cur_sp_);
    }

    auto arg = fn->arg_begin();
    sret_ = nullptr;
    if (isMem(f.ret)) {
        sret_ = &*arg++;
    }
    for (std::uint32_t i = 0; i < f.param_count; ++i, ++arg) {
        const Type& pt = f.locals[i].type;
        arg->setName(f.locals[i].name);
        if (isMem(pt)) {
            locals_[i] = &*arg;  // the caller passed its own copy
        } else {
            llvm::AllocaInst* slot = makeAlloca(mty(pt), DL_.getABITypeAlign(mty(pt)).value(), f.locals[i].name);
            setLoc(f.locals[i].span);
            storeScalar(pt, &*arg, slot);
            locals_[i] = slot;
        }
    }
    genStmts(f.body);
    ensureTerminated();
    cur_sp_ = nullptr;
    B_.SetCurrentDebugLocation(llvm::DebugLoc());
    cur_ = nullptr;
}

void Gen::genStmts(const std::vector<hir::Stmt>& body) {
    for (const hir::Stmt& s : body) {
        genStmt(s);
    }
}

void Gen::genStmt(const hir::Stmt& s) {
    setLoc(s.span);
    switch (s.kind) {
    case hir::StmtKind::Local: {
        const hir::Local& l = cur_->locals[s.local];
        if (isMem(l.type)) {
            const auto [size, align] = sizeAlign(l.type);
            llvm::AllocaInst* slot =
                makeAlloca(llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx_), std::max<std::uint64_t>(size, 1)), align, l.name);
            locals_[s.local] = slot;
            if (s.value) {
                genInto(*s.value, slot);
            } else {
                zeroMem(slot, size, align);
                if (l.type.kind == TypeKind::Class) {
                    if (llvm::Constant* vp = vtablePointer(l.type.path.front())) {
                        B_.CreateAlignedStore(vp, slot, llvm::Align(ptrBits() / 8));
                    }
                }
            }
        } else {
            llvm::AllocaInst* slot = makeAlloca(mty(l.type), DL_.getABITypeAlign(mty(l.type)).value(), l.name);
            locals_[s.local] = slot;
            if (s.value) {
                storeScalar(l.type, genScalar(*s.value), slot);
            } else {
                storeScalar(l.type, llvm::Constant::getNullValue(sty(l.type)), slot);
            }
        }
        break;
    }
    case hir::StmtKind::Assign: {
        if (isMem(s.target->type)) {
            llvm::Value* dest = genAddr(*s.target);
            genInto(*s.value, dest);
        } else {
            llvm::Value* v = genScalar(*s.value);
            llvm::Value* dest = genAddr(*s.target);
            storeScalar(s.target->type, v, dest);
        }
        break;
    }
    case hir::StmtKind::Return: {
        if (s.value) {
            if (isMem(cur_->ret)) {
                genInto(*s.value, sret_);
                B_.CreateRetVoid();
            } else {
                B_.CreateRet(genScalar(*s.value));
            }
        } else {
            B_.CreateRetVoid();
        }
        llvm::BasicBlock* dead = llvm::BasicBlock::Create(ctx_, "after.ret", cur_fn_);
        B_.SetInsertPoint(dead);
        break;
    }
    case hir::StmtKind::Expr: {
        const hir::Expr& e = *s.value;
        if (isMem(e.type)) {
            (void)genMem(e);
        } else if (e.kind == hir::ExprKind::AtomicStore || e.kind == hir::ExprKind::Fence ||
                   e.kind == hir::ExprKind::Asm) {
            (void)genScalar(e);
        } else if (e.type.isVoid() || e.kind == hir::ExprKind::Call || e.kind == hir::ExprKind::CallIndirect ||
                   e.kind == hir::ExprKind::CallVirtual || e.kind == hir::ExprKind::CallItable ||
                   e.kind == hir::ExprKind::Syscall) {
            llvm::Value* mem = nullptr;
            (void)genCall(e, &mem);
        } else {
            (void)genScalar(e);
        }
        break;
    }
    case hir::StmtKind::If: {
        llvm::Value* c = genScalar(*s.value);
        llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(ctx_, "then", cur_fn_);
        llvm::BasicBlock* else_bb = s.else_body.empty() ? nullptr : llvm::BasicBlock::Create(ctx_, "else", cur_fn_);
        llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(ctx_, "endif", cur_fn_);
        B_.CreateCondBr(c, then_bb, else_bb != nullptr ? else_bb : end_bb);
        B_.SetInsertPoint(then_bb);
        genStmts(s.body);
        if (!hasTerminator(B_.GetInsertBlock())) {
            B_.CreateBr(end_bb);
        }
        if (else_bb != nullptr) {
            B_.SetInsertPoint(else_bb);
            genStmts(s.else_body);
            if (!hasTerminator(B_.GetInsertBlock())) {
                B_.CreateBr(end_bb);
            }
        }
        B_.SetInsertPoint(end_bb);
        break;
    }
    case hir::StmtKind::While: {
        llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(ctx_, "while.cond", cur_fn_);
        llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(ctx_, "while.body", cur_fn_);
        llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(ctx_, "while.end", cur_fn_);
        llvm::BasicBlock* step_bb =
            s.step.empty() ? nullptr : llvm::BasicBlock::Create(ctx_, "while.step", cur_fn_);
        // `continue` runs the step (a `for`), then re-tests the condition.
        llvm::BasicBlock* next_bb = step_bb != nullptr ? step_bb : cond_bb;
        B_.CreateBr(s.post_test ? body_bb : cond_bb);
        B_.SetInsertPoint(cond_bb);
        setLoc(s.span);
        llvm::Value* c = genScalar(*s.value);
        B_.CreateCondBr(c, body_bb, end_bb);
        B_.SetInsertPoint(body_bb);
        loops_.push_back({next_bb, end_bb});
        genStmts(s.body);
        loops_.pop_back();
        if (!hasTerminator(B_.GetInsertBlock())) {
            B_.CreateBr(next_bb);
        }
        if (step_bb != nullptr) {
            B_.SetInsertPoint(step_bb);
            genStmts(s.step);
            if (!hasTerminator(B_.GetInsertBlock())) {
                B_.CreateBr(cond_bb);
            }
        }
        B_.SetInsertPoint(end_bb);
        break;
    }
    case hir::StmtKind::Continue:
    case hir::StmtKind::Break: {
        const LoopTargets& t = loops_[loops_.size() - 1 - s.depth];
        B_.CreateBr(s.kind == hir::StmtKind::Break ? t.brk : t.cont);
        llvm::BasicBlock* dead = llvm::BasicBlock::Create(ctx_, "after.jump", cur_fn_);
        B_.SetInsertPoint(dead);
        break;
    }
    case hir::StmtKind::Block:
        genStmts(s.body);
        break;
    }
}

// ---------------------------------------------------------------------------------------------
// Expressions

llvm::Value* Gen::genAddr(const hir::Expr& e) {
    setLoc(e.span);
    switch (e.kind) {
    case hir::ExprKind::LocalRef: return locals_[e.local];
    case hir::ExprKind::GlobalRef: {
        const auto it = globals_.find(e.text);
        if (it == globals_.end()) {
            error(e.span, std::format("unknown global `{}`", e.text));
            return llvm::Constant::getNullValue(ptrTy());
        }
        return it->second;
    }
    case hir::ExprKind::Deref: return genScalar(*e.lhs);
    case hir::ExprKind::Field: {
        llvm::Value* base = e.through_pointer ? genScalar(*e.lhs) : genAddr(*e.lhs);
        const Type agg = e.through_pointer ? *e.lhs->type.elem : e.lhs->type;
        return fieldAddr(base, agg, e.index);
    }
    case hir::ExprKind::Index: {
        const bool is_array = e.lhs->type.kind == TypeKind::Array;
        const bool is_str = e.lhs->type.kind == TypeKind::Str;
        llvm::Value* base = is_array || is_str ? genAddr(*e.lhs) : genScalar(*e.lhs);
        llvm::Value* idx = genScalar(*e.rhs);
        const bool idx_signed = isSigned(e.rhs->type);
        llvm::Type* i64 = llvm::Type::getInt64Ty(ctx_);
        if (idx->getType()->getIntegerBitWidth() != 64) {
            idx = idx_signed ? B_.CreateSExt(idx, i64) : B_.CreateZExt(idx, i64);
        }
        if (is_str) {
            // `str` is { ptr, len }: check the index against len, then index the bytes at ptr.
            llvm::Value* len = B_.CreateZExt(
                B_.CreateAlignedLoad(sty(Type::makeInt(0, false)), byteOffset(base, ptrBits() / 8),
                                     llvm::Align(ptrBits() / 8)),
                i64);
            if (e.checked) {
                checkedBranch(B_.CreateICmpUGE(idx, len), "str index out of bounds");
            }
            llvm::Value* data =
                B_.CreateAlignedLoad(ptrTy(), base, llvm::Align(ptrBits() / 8));
            return B_.CreateInBoundsGEP(llvm::Type::getInt8Ty(ctx_), data, {idx});
        }
        if (is_array && e.checked) {
            const TypeLayout* l = layoutOf(e.lhs->type);
            const auto elem_size = sizeAlign(*e.lhs->type.elem).first;
            const std::uint64_t len = l != nullptr && elem_size != 0 ? l->size / elem_size : 0;
            checkedBranch(B_.CreateICmpUGE(idx, B_.getInt64(len)), "array index out of bounds");
        }
        const auto elem_size = sizeAlign(*e.lhs->type.elem).first;
        llvm::Value* off = B_.CreateMul(idx, B_.getInt64(elem_size));
        return B_.CreateInBoundsGEP(llvm::Type::getInt8Ty(ctx_), base, {off});
    }
    default:
        // An rvalue used as a place: materialize it.
        if (isMem(e.type)) {
            return genMem(e);
        }
        llvm::Value* v = genScalar(e);
        llvm::AllocaInst* tmp = makeTemp(e.type);
        storeScalar(e.type, v, tmp);
        return tmp;
    }
}

void Gen::genInto(const hir::Expr& e, llvm::Value* dest) {
    setLoc(e.span);
    switch (e.kind) {
    case hir::ExprKind::StructLit: {
        const TypeLayout* l = layoutOf(e.type);
        if (l == nullptr) {
            error(e.span, std::format("no layout for `{}`", typeToString(e.type)));
            return;
        }
        zeroMem(dest, l->size, l->align);
        if (l->has_vptr) {
            if (llvm::Constant* vp = vtablePointer(e.type.path.front())) {
                B_.CreateAlignedStore(vp, dest, llvm::Align(ptrBits() / 8));
            }
        }
        for (const hir::FieldInit& f : e.fields) {
            llvm::Value* addr = byteOffset(dest, l->fields[f.index].offset);
            if (isMem(f.value->type)) {
                genInto(*f.value, addr);
            } else {
                storeScalar(f.value->type, genScalar(*f.value), addr);
            }
        }
        return;
    }
    case hir::ExprKind::ArrayList: {
        const auto [esize, ealign] = sizeAlign(*e.type.elem);
        (void)ealign;
        for (std::size_t i = 0; i < e.args.size(); ++i) {
            llvm::Value* addr = byteOffset(dest, esize * i);
            if (isMem(e.args[i]->type)) {
                genInto(*e.args[i], addr);
            } else {
                storeScalar(e.args[i]->type, genScalar(*e.args[i]), addr);
            }
        }
        return;
    }
    case hir::ExprKind::ArrayRepeat: {
        const auto [esize, ealign] = sizeAlign(*e.type.elem);
        (void)ealign;
        for (std::uint64_t i = 0; i < e.int_value; ++i) {
            llvm::Value* addr = byteOffset(dest, esize * i);
            if (isMem(e.lhs->type)) {
                genInto(*e.lhs, addr);
            } else {
                storeScalar(e.lhs->type, genScalar(*e.lhs), addr);
            }
        }
        return;
    }
    case hir::ExprKind::Select: {
        llvm::Value* c = genScalar(*e.lhs);
        llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(ctx_, "sel.then", cur_fn_);
        llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(ctx_, "sel.else", cur_fn_);
        llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(ctx_, "sel.end", cur_fn_);
        B_.CreateCondBr(c, then_bb, else_bb);
        B_.SetInsertPoint(then_bb);
        genInto(*e.rhs, dest);
        B_.CreateBr(end_bb);
        B_.SetInsertPoint(else_bb);
        genInto(*e.args[0], dest);
        B_.CreateBr(end_bb);
        B_.SetInsertPoint(end_bb);
        return;
    }
    case hir::ExprKind::ToInterface: {
        llvm::Value* data = genScalar(*e.lhs);
        llvm::Constant* itable = itablePointer(e.text, e.type.path.front());
        if (itable == nullptr) {
            error(e.span, std::format("no itable for `{}` as `{}`", e.text, e.type.path.front()));
            return;
        }
        B_.CreateAlignedStore(data, dest, llvm::Align(ptrBits() / 8));
        B_.CreateAlignedStore(itable, byteOffset(dest, ptrBits() / 8), llvm::Align(ptrBits() / 8));
        return;
    }
    case hir::ExprKind::Construct: {
        // A new object in `dest`: zeroed, vptr stored, then its `init` runs on it.
        const TypeLayout* l = layoutOf(e.target);
        if (l != nullptr) {
            zeroMem(dest, l->size, l->align);
        }
        if (e.target.kind == TypeKind::Class) {
            if (llvm::Constant* vp = vtablePointer(e.target.path.front())) {
                B_.CreateAlignedStore(vp, dest, llvm::Align(ptrBits() / 8));
            }
        }
        if (!e.text.empty()) {
            callInit(e.text, dest, e.args, e.span);
        }
        return;
    }
    case hir::ExprKind::StrConst: {
        if (e.type.kind != TypeKind::Str) {
            storeScalar(e.type, genScalar(e), dest);
            return;
        }
        B_.CreateAlignedStore(stringGlobal(e.text), dest, llvm::Align(ptrBits() / 8));
        B_.CreateAlignedStore(B_.getInt64(e.text.size()), byteOffset(dest, ptrBits() / 8),
                              llvm::Align(ptrBits() / 8));
        return;
    }
    default: {
        llvm::Value* src = genMem(e);
        if (src != dest) {
            copyMem(dest, src, e.type);
        }
        return;
    }
    }
}

llvm::Value* Gen::genMem(const hir::Expr& e) {
    setLoc(e.span);
    switch (e.kind) {
    case hir::ExprKind::LocalRef:
    case hir::ExprKind::GlobalRef:
    case hir::ExprKind::Deref:
    case hir::ExprKind::Field:
    case hir::ExprKind::Index:
        return genAddr(e);
    case hir::ExprKind::Call:
    case hir::ExprKind::CallIndirect:
    case hir::ExprKind::CallVirtual:
    case hir::ExprKind::CallItable: {
        llvm::Value* mem = nullptr;
        (void)genCall(e, &mem);
        return mem;
    }
    case hir::ExprKind::Move: {
        // The value is read first; the drop flag is cleared, so the owner no longer drops it.
        llvm::Value* src = genMem(*e.lhs);
        storeScalar(Type::makeBool(), B_.getInt1(false), locals_[e.local]);
        return src;
    }
    case hir::ExprKind::Materialize: {
        llvm::Value* tmp = locals_[e.local];
        genInto(*e.lhs, tmp);
        storeScalar(Type::makeBool(), B_.getInt1(true), locals_[e.index]);
        return tmp;
    }
    case hir::ExprKind::External:
    case hir::ExprKind::ExternalCall:
        error(e.span, std::format("unresolved external reference `{}.{}`", e.text, e.member));
        return makeTemp(e.type);
    default: {
        llvm::AllocaInst* tmp = makeTemp(e.type);
        genInto(e, tmp);
        return tmp;
    }
    }
}

llvm::Value* Gen::castScalar(llvm::Value* v, const Type& from, const Type& to) {
    llvm::Type* dst = sty(to);
    llvm::Type* src = v->getType();
    if (src == dst) {
        return v;
    }
    const Type& f = from.kind == TypeKind::Enum && from.elem ? *from.elem : from;
    const Type& t = to.kind == TypeKind::Enum && to.elem ? *to.elem : to;
    const bool f_int = f.kind == TypeKind::Int || f.kind == TypeKind::Bool;
    const bool t_int = t.kind == TypeKind::Int || t.kind == TypeKind::Bool;
    if (f_int && t_int) {
        if (t.kind == TypeKind::Bool) {
            return B_.CreateICmpNE(v, llvm::ConstantInt::get(src, 0));
        }
        const unsigned sb = src->getIntegerBitWidth();
        const unsigned db = dst->getIntegerBitWidth();
        if (sb == db) {
            return v;
        }
        if (sb > db) {
            return B_.CreateTrunc(v, dst);
        }
        return f.kind == TypeKind::Int && f.is_signed ? B_.CreateSExt(v, dst) : B_.CreateZExt(v, dst);
    }
    if (f_int && t.kind == TypeKind::Float) {
        return f.kind == TypeKind::Int && f.is_signed ? B_.CreateSIToFP(v, dst) : B_.CreateUIToFP(v, dst);
    }
    if (f.kind == TypeKind::Float && t_int) {
        if (t.kind == TypeKind::Bool) {
            return B_.CreateFCmpUNE(v, llvm::ConstantFP::get(src, 0.0));
        }
        return t.is_signed ? B_.CreateFPToSI(v, dst) : B_.CreateFPToUI(v, dst);
    }
    if (f.kind == TypeKind::Float && t.kind == TypeKind::Float) {
        return f.bits < t.bits ? B_.CreateFPExt(v, dst) : B_.CreateFPTrunc(v, dst);
    }
    const bool f_ptr = f.kind == TypeKind::Pointer || f.kind == TypeKind::FnPtr;
    const bool t_ptr = t.kind == TypeKind::Pointer || t.kind == TypeKind::FnPtr;
    if (f_ptr && t.kind == TypeKind::Int) {
        return B_.CreatePtrToInt(v, dst);
    }
    if (f.kind == TypeKind::Int && t_ptr) {
        return B_.CreateIntToPtr(v, dst);
    }
    return v;  // pointer <-> pointer
}

llvm::Value* Gen::genCast(const hir::Expr& e) {
    llvm::Value* v = genScalar(*e.lhs);
    return castScalar(v, e.lhs->type, e.type);
}

llvm::Value* Gen::genBinary(const hir::Expr& e) {
    const BinaryOp op = e.binary_op;
    if (op == BinaryOp::And || op == BinaryOp::Or) {
        llvm::Value* l = genScalar(*e.lhs);
        if (llvm::isa<llvm::Constant>(l) && B_.GetInsertBlock() == nullptr) {
            llvm::Value* r = genScalar(*e.rhs);
            return op == BinaryOp::And ? B_.CreateAnd(l, r) : B_.CreateOr(l, r);
        }
        llvm::BasicBlock* lhs_bb = B_.GetInsertBlock();
        llvm::BasicBlock* rhs_bb = llvm::BasicBlock::Create(ctx_, "sc.rhs", cur_fn_);
        llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(ctx_, "sc.end", cur_fn_);
        if (op == BinaryOp::And) {
            B_.CreateCondBr(l, rhs_bb, end_bb);
        } else {
            B_.CreateCondBr(l, end_bb, rhs_bb);
        }
        B_.SetInsertPoint(rhs_bb);
        llvm::Value* r = genScalar(*e.rhs);
        llvm::BasicBlock* rhs_end = B_.GetInsertBlock();
        B_.CreateBr(end_bb);
        B_.SetInsertPoint(end_bb);
        llvm::PHINode* phi = B_.CreatePHI(llvm::Type::getInt1Ty(ctx_), 2);
        phi->addIncoming(B_.getInt1(op == BinaryOp::Or), lhs_bb);
        phi->addIncoming(r, rhs_end);
        return phi;
    }
    llvm::Value* l = genScalar(*e.lhs);
    llvm::Value* r = genScalar(*e.rhs);
    const Type& t = e.lhs->type;
    const bool is_float = t.kind == TypeKind::Float;
    const bool sign = isSigned(t);
    switch (op) {
    case BinaryOp::Add: return is_float ? B_.CreateFAdd(l, r) : B_.CreateAdd(l, r);
    case BinaryOp::Sub: return is_float ? B_.CreateFSub(l, r) : B_.CreateSub(l, r);
    case BinaryOp::Mul: return is_float ? B_.CreateFMul(l, r) : B_.CreateMul(l, r);
    case BinaryOp::Div:
    case BinaryOp::Mod: {
        if (is_float) {
            return B_.CreateFDiv(l, r);
        }
        if (e.checked && B_.GetInsertBlock() != nullptr) {
            checkedBranch(B_.CreateICmpEQ(r, llvm::ConstantInt::get(r->getType(), 0)), "division by zero");
        }
        if (op == BinaryOp::Div) {
            return sign ? B_.CreateSDiv(l, r) : B_.CreateUDiv(l, r);
        }
        return sign ? B_.CreateSRem(l, r) : B_.CreateURem(l, r);
    }
    case BinaryOp::BitAnd: return B_.CreateAnd(l, r);
    case BinaryOp::BitOr: return B_.CreateOr(l, r);
    case BinaryOp::BitXor: return B_.CreateXor(l, r);
    case BinaryOp::Shl:
    case BinaryOp::Shr: {
        // A count >= the bit width gives 0 (`>>` of a negative signed value: -1), never poison.
        const unsigned bits = l->getType()->getIntegerBitWidth();
        llvm::Value* zero = llvm::ConstantInt::get(l->getType(), 0);
        llvm::Value* over = B_.CreateICmpUGE(r, llvm::ConstantInt::get(r->getType(), bits));
        llvm::Value* count = B_.CreateSelect(over, zero, r);
        if (op == BinaryOp::Shl) {
            return B_.CreateSelect(over, zero, B_.CreateShl(l, count));
        }
        if (sign) {
            llvm::Value* fill = B_.CreateAShr(l, llvm::ConstantInt::get(l->getType(), bits - 1));
            return B_.CreateSelect(over, fill, B_.CreateAShr(l, count));
        }
        return B_.CreateSelect(over, zero, B_.CreateLShr(l, count));
    }
    case BinaryOp::Eq:
        return is_float ? B_.CreateFCmpOEQ(l, r) : B_.CreateICmpEQ(l, r);
    case BinaryOp::Ne:
        return is_float ? B_.CreateFCmpUNE(l, r) : B_.CreateICmpNE(l, r);
    case BinaryOp::Lt:
        return is_float ? B_.CreateFCmpOLT(l, r) : (sign ? B_.CreateICmpSLT(l, r) : B_.CreateICmpULT(l, r));
    case BinaryOp::Le:
        return is_float ? B_.CreateFCmpOLE(l, r) : (sign ? B_.CreateICmpSLE(l, r) : B_.CreateICmpULE(l, r));
    case BinaryOp::Gt:
        return is_float ? B_.CreateFCmpOGT(l, r) : (sign ? B_.CreateICmpSGT(l, r) : B_.CreateICmpUGT(l, r));
    case BinaryOp::Ge:
        return is_float ? B_.CreateFCmpOGE(l, r) : (sign ? B_.CreateICmpSGE(l, r) : B_.CreateICmpUGE(l, r));
    default: break;
    }
    return l;
}

llvm::Value* Gen::genSyscall(const hir::Expr& e) {
    // Plan 5.3: arguments are converted to the register width; the result is `i64` with errors
    // as negative `-errno` on every target.
    llvm::Type* i64 = llvm::Type::getInt64Ty(ctx_);
    std::vector<llvm::Value*> args;
    for (const hir::ExprPtr& a : e.args) {
        llvm::Value* v = genScalar(*a);
        if (v->getType()->isPointerTy()) {
            v = B_.CreatePtrToInt(v, i64);
        } else if (v->getType()->getIntegerBitWidth() < 64) {
            v = isSigned(a->type) ? B_.CreateSExt(v, i64) : B_.CreateZExt(v, i64);
        }
        args.push_back(v);
    }
    const TargetInfo& t = in_.config.target;
    const bool x86 = t.arch == "x86_64";
    std::string constraints;
    std::string text;
    std::vector<llvm::Value*> inputs;
    const std::size_t n = args.size() - 1;  // arguments after the number
    const auto addInput = [&](const std::string& reg, llvm::Value* v) {
        constraints += (constraints.empty() ? "" : ",") + std::string("{") + reg + "}";
        inputs.push_back(v);
    };
    llvm::Value* result = nullptr;
    if (t.os == "linux" && x86) {
        static const char* regs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
        constraints = "={rax}";
        addInput("rax", args[0]);
        for (std::size_t i = 0; i < n; ++i) {
            addInput(regs[i], args[i + 1]);
        }
        constraints += ",~{rcx},~{r11},~{memory}";
        text = "syscall";
    } else if (t.os == "linux") {  // aarch64
        constraints = "={x0}";
        addInput("x8", args[0]);
        for (std::size_t i = 0; i < n; ++i) {
            if (i == 0) {
                constraints += ",0";
                inputs.push_back(args[1]);
            } else {
                addInput("x" + std::to_string(i), args[i + 1]);
            }
        }
        if (n == 0) {
            // x0 is an output only; no input needed.
        }
        constraints += ",~{memory}";
        text = "svc #0";
    } else if (t.os == "macos" && x86) {
        static const char* regs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
        constraints = "={rax}";
        llvm::Value* nr = B_.CreateOr(args[0], llvm::ConstantInt::get(i64, 0x2000000));
        addInput("rax", nr);
        for (std::size_t i = 0; i < n; ++i) {
            addInput(regs[i], args[i + 1]);
        }
        constraints += ",~{rcx},~{r11},~{cc},~{memory}";
        text = "syscall\n\tjnc 1f\n\tnegq %rax\n1:";
    } else if (t.os == "macos") {  // aarch64
        constraints = "={x0}";
        addInput("x16", args[0]);
        for (std::size_t i = 0; i < n; ++i) {
            if (i == 0) {
                constraints += ",0";
                inputs.push_back(args[1]);
            } else {
                addInput("x" + std::to_string(i), args[i + 1]);
            }
        }
        constraints += ",~{cc},~{memory}";
        text = "svc #0x80\n\tb.cc 1f\n\tneg x0, x0\n1:";
    } else if (t.os == "windows" && x86) {
        if (n > 4) {
            error(e.span, "`syscall` with more than 4 arguments on Windows is not supported yet (stack passing)");
            return llvm::ConstantInt::get(i64, 0);
        }
        static const char* regs[] = {"r10", "rdx", "r8", "r9"};
        constraints = "={rax}";
        addInput("rax", args[0]);
        for (std::size_t i = 0; i < n; ++i) {
            addInput(regs[i], args[i + 1]);
        }
        constraints += ",~{rcx},~{r11},~{memory}";
        text = "syscall";
    } else {
        error(e.span, std::format("`syscall` is not supported on target `{}/{}`", t.os, t.arch));
        return llvm::ConstantInt::get(i64, 0);
    }
    std::vector<llvm::Type*> in_types;
    for (llvm::Value* v : inputs) {
        in_types.push_back(v->getType());
    }
    llvm::FunctionType* fty = llvm::FunctionType::get(i64, in_types, false);
    const bool intel = false;
    (void)intel;
    llvm::InlineAsm* ia = llvm::InlineAsm::get(fty, text, constraints, true, false, llvm::InlineAsm::AD_ATT);
    result = B_.CreateCall(ia, inputs);
    if (t.os == "windows") {
        // NTSTATUS is the 32-bit `eax`, sign-extended.
        result = B_.CreateSExt(B_.CreateTrunc(result, llvm::Type::getInt32Ty(ctx_)), i64);
    }
    return result;
}

void Gen::callInit(const std::string& key, llvm::Value* self, const std::vector<hir::ExprPtr>& args, const Span& span) {
    llvm::Function* f = functionNamed(key);
    if (f == nullptr) {
        error(span, std::format("unknown function `{}`", key));
        return;
    }
    std::vector<Type> params{Type::makePointer(Type::makeVoid())};
    std::vector<const hir::Expr*> av;
    for (const hir::ExprPtr& a : args) {
        params.push_back(a->type);
        av.push_back(a.get());
    }
    genCallCommon(fnTypeFor(params, Type::makeVoid()), f, av, self, Type::makeVoid(), nullptr, span);
}

llvm::Value* Gen::genCallCommon(llvm::FunctionType* fty, llvm::Value* callee,
                                const std::vector<const hir::Expr*>& args, llvm::Value* self, const Type& ret,
                                llvm::Value** mem_result, const Span& at) {
    std::vector<llvm::Value*> av;
    llvm::Value* sret = nullptr;
    if (isMem(ret)) {
        sret = makeTemp(ret);
        av.push_back(sret);
    }
    if (self != nullptr) {
        av.push_back(self);
    }
    for (const hir::Expr* a : args) {
        if (isMem(a->type)) {
            // The callee owns its argument: pass a copy.
            llvm::AllocaInst* tmp = makeTemp(a->type);
            genInto(*a, tmp);
            av.push_back(tmp);
        } else {
            av.push_back(genScalar(*a));
        }
    }
    setLoc(at);
    llvm::CallInst* call = B_.CreateCall(fty, callee, av);
    if (mem_result != nullptr) {
        *mem_result = sret;
    }
    return call;
}

llvm::Value* Gen::genCall(const hir::Expr& e, llvm::Value** mem_result) {
    setLoc(e.span);
    std::vector<const hir::Expr*> args;
    switch (e.kind) {
    case hir::ExprKind::Syscall: return genSyscall(e);
    case hir::ExprKind::Call:
    case hir::ExprKind::CallVirtual: {
        llvm::Function* f = functionNamed(e.text);
        if (f == nullptr) {
            error(e.span, std::format("call of unknown function `{}`", e.text));
            return llvm::UndefValue::get(sty(e.type)->isVoidTy() ? llvm::Type::getInt32Ty(ctx_) : sty(e.type));
        }
        for (const hir::ExprPtr& a : e.args) {
            args.push_back(a.get());
        }
        if (e.kind == hir::ExprKind::Call) {
            llvm::Value* r = genCallCommon(f->getFunctionType(), f, args, nullptr, e.type, mem_result, e.span);
            if (!hm_.panic_function.empty() && e.text == hm_.panic_function) {
                // The registered `panic` never returns (plan 4.12): the rest is unreachable.
                B_.CreateUnreachable();
                B_.SetInsertPoint(llvm::BasicBlock::Create(ctx_, "after.panic", cur_fn_));
            }
            return r;
        }
        // Virtual: args[0] is `self`; the target comes from the vtable slot.
        llvm::Value* self = genScalar(*args[0]);
        const auto it = hir_fns_.find(e.text);
        std::vector<Type> params;
        if (it != hir_fns_.end()) {
            for (std::uint32_t i = 0; i < it->second->param_count; ++i) {
                params.push_back(it->second->locals[i].type);
            }
        }
        llvm::FunctionType* fty = f->getFunctionType();
        llvm::Value* vptr = B_.CreateAlignedLoad(ptrTy(), self, llvm::Align(ptrBits() / 8));
        llvm::Value* slot = B_.CreateInBoundsGEP(llvm::Type::getInt8Ty(ctx_), vptr, {B_.getInt64(std::uint64_t(e.index) * (ptrBits() / 8))});
        llvm::Value* fn = B_.CreateAlignedLoad(ptrTy(), slot, llvm::Align(ptrBits() / 8));
        std::vector<const hir::Expr*> rest(args.begin() + 1, args.end());
        return genCallCommon(fty, fn, rest, self, e.type, mem_result, e.span);
    }
    case hir::ExprKind::CallIndirect: {
        llvm::Value* fn = genScalar(*e.lhs);
        std::vector<Type> params = e.lhs->type.params;
        llvm::FunctionType* fty = fnTypeFor(params, e.type);
        for (const hir::ExprPtr& a : e.args) {
            args.push_back(a.get());
        }
        return genCallCommon(fty, fn, args, nullptr, e.type, mem_result, e.span);
    }
    case hir::ExprKind::CallItable: {
        const hir::Interface* iface = interfaceNamed(e.lhs->type.path.empty() ? std::string{} : e.lhs->type.path.front());
        if (iface == nullptr || e.index >= iface->methods.size()) {
            error(e.span, "call through an unknown interface");
            return llvm::UndefValue::get(llvm::Type::getInt32Ty(ctx_));
        }
        const hir::InterfaceMethod& m = iface->methods[e.index];
        llvm::Value* ia = genMem(*e.lhs);
        llvm::Value* data = B_.CreateAlignedLoad(ptrTy(), ia, llvm::Align(ptrBits() / 8));
        llvm::Value* itable = B_.CreateAlignedLoad(ptrTy(), byteOffset(ia, ptrBits() / 8), llvm::Align(ptrBits() / 8));
        llvm::Value* slot = B_.CreateInBoundsGEP(llvm::Type::getInt8Ty(ctx_), itable, {B_.getInt64(std::uint64_t(e.index) * (ptrBits() / 8))});
        llvm::Value* fn = B_.CreateAlignedLoad(ptrTy(), slot, llvm::Align(ptrBits() / 8));
        std::vector<Type> params;
        params.push_back(Type::makePointer(Type::makeVoid()));  // self
        for (const Type& p : m.params) {
            params.push_back(p);
        }
        llvm::FunctionType* fty = fnTypeFor(params, e.type);
        for (const hir::ExprPtr& a : e.args) {
            args.push_back(a.get());
        }
        return genCallCommon(fty, fn, args, data, e.type, mem_result, e.span);
    }
    default: break;
    }
    return nullptr;
}

llvm::Value* Gen::genNew(const hir::Expr& e) {
    const Type& t = e.target;
    const auto [size, align] = sizeAlign(t);
    llvm::Type* i64 = llvm::Type::getInt64Ty(ctx_);
    llvm::Value* mem = nullptr;
    const std::string iface_needed = "alloc";
    if (e.lhs->type.kind == TypeKind::Interface) {
        const hir::Interface* iface = interfaceNamed(e.lhs->type.path.front());
        std::uint32_t idx = 0;
        bool found = false;
        if (iface != nullptr) {
            for (std::uint32_t i = 0; i < iface->methods.size(); ++i) {
                if (iface->methods[i].name == iface_needed) {
                    idx = i;
                    found = true;
                }
            }
        }
        if (!found) {
            error(e.span, "allocator interface has no `alloc` method");
            return llvm::Constant::getNullValue(ptrTy());
        }
        llvm::Value* ia = genMem(*e.lhs);
        llvm::Value* data = B_.CreateAlignedLoad(ptrTy(), ia, llvm::Align(ptrBits() / 8));
        llvm::Value* itable = B_.CreateAlignedLoad(ptrTy(), byteOffset(ia, ptrBits() / 8), llvm::Align(ptrBits() / 8));
        llvm::Value* slot = B_.CreateInBoundsGEP(llvm::Type::getInt8Ty(ctx_), itable, {B_.getInt64(std::uint64_t(idx) * (ptrBits() / 8))});
        llvm::Value* fn = B_.CreateAlignedLoad(ptrTy(), slot, llvm::Align(ptrBits() / 8));
        llvm::FunctionType* fty = llvm::FunctionType::get(ptrTy(), {ptrTy(), llvm::Type::getIntNTy(ctx_, ptrBits()),
                                                                     llvm::Type::getIntNTy(ctx_, ptrBits())},
                                                          false);
        mem = B_.CreateCall(fty, fn, {data, llvm::ConstantInt::get(llvm::Type::getIntNTy(ctx_, ptrBits()), size),
                                      llvm::ConstantInt::get(llvm::Type::getIntNTy(ctx_, ptrBits()), align)});
    } else {
        // A struct/class value or pointer: find the `impl` that provides `alloc`.
        const Type& obj = e.lhs->type.kind == TypeKind::Pointer && e.lhs->type.elem ? *e.lhs->type.elem : e.lhs->type;
        std::string fname;
        for (const hir::Impl& im : hm_.impls) {
            if (im.type != obj.path.front()) {
                continue;
            }
            const hir::Interface* iface = interfaceNamed(im.interface);
            if (iface == nullptr) {
                continue;
            }
            for (std::size_t i = 0; i < iface->methods.size() && i < im.methods.size(); ++i) {
                if (iface->methods[i].name == iface_needed) {
                    fname = im.methods[i];
                }
            }
        }
        llvm::Function* f = fname.empty() ? nullptr : functionNamed(fname);
        if (f == nullptr) {
            error(e.span, "allocator type has no `alloc` implementation");
            return llvm::Constant::getNullValue(ptrTy());
        }
        llvm::Value* self = e.lhs->type.kind == TypeKind::Pointer ? genScalar(*e.lhs) : genAddr(*e.lhs);
        llvm::Type* uty = llvm::Type::getIntNTy(ctx_, ptrBits());
        mem = B_.CreateCall(f, {self, llvm::ConstantInt::get(uty, size), llvm::ConstantInt::get(uty, align)});
    }
    (void)i64;
    // `alloc` may return nil: then `new` yields nil and writes nothing.
    llvm::BasicBlock* init_bb = llvm::BasicBlock::Create(ctx_, "new.init", cur_fn_);
    llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(ctx_, "new.end", cur_fn_);
    llvm::BasicBlock* from_bb = B_.GetInsertBlock();
    B_.CreateCondBr(B_.CreateICmpNE(mem, llvm::Constant::getNullValue(ptrTy())), init_bb, end_bb);
    B_.SetInsertPoint(init_bb);
    hir::Expr lit;  // reuse StructLit initialization
    zeroMem(mem, size, align);
    if (t.kind == TypeKind::Class) {
        if (llvm::Constant* vp = vtablePointer(t.path.front())) {
            B_.CreateAlignedStore(vp, mem, llvm::Align(ptrBits() / 8));
        }
    }
    if (!e.text.empty()) {
        callInit(e.text, mem, e.args, e.span);
    }
    const TypeLayout* l = layoutOf(t);
    for (const hir::FieldInit& f : e.fields) {
        llvm::Value* addr = l != nullptr ? byteOffset(mem, l->fields[f.index].offset) : mem;
        if (isMem(f.value->type)) {
            genInto(*f.value, addr);
        } else {
            storeScalar(f.value->type, genScalar(*f.value), addr);
        }
    }
    B_.CreateBr(end_bb);
    B_.SetInsertPoint(end_bb);
    llvm::PHINode* phi = B_.CreatePHI(ptrTy(), 2);
    phi->addIncoming(llvm::Constant::getNullValue(ptrTy()), from_bb);
    phi->addIncoming(mem, init_bb);
    return phi;
}

namespace {

[[nodiscard]] llvm::AtomicOrdering orderingOf(std::uint32_t order) {
    switch (order) {
    case 1: return llvm::AtomicOrdering::Acquire;
    case 2: return llvm::AtomicOrdering::Release;
    case 3: return llvm::AtomicOrdering::AcquireRelease;
    case 4: return llvm::AtomicOrdering::SequentiallyConsistent;
    default: return llvm::AtomicOrdering::Monotonic;
    }
}

/// Failure ordering of a compare-exchange: no release semantics, not stronger than success.
[[nodiscard]] llvm::AtomicOrdering failureOrdering(llvm::AtomicOrdering success) {
    switch (success) {
    case llvm::AtomicOrdering::SequentiallyConsistent: return llvm::AtomicOrdering::SequentiallyConsistent;
    case llvm::AtomicOrdering::Acquire:
    case llvm::AtomicOrdering::AcquireRelease: return llvm::AtomicOrdering::Acquire;
    default: return llvm::AtomicOrdering::Monotonic;
    }
}

}  // namespace

llvm::Value* Gen::genAtomic(const hir::Expr& e) {
    llvm::Value* ptr = genScalar(*e.lhs);
    const Type& t = e.lhs->type.elem ? *e.lhs->type.elem : e.type;
    llvm::Type* mt = mty(t);  // bool is stored as i8
    const llvm::Align align(std::max<std::uint64_t>(DL_.getTypeStoreSize(mt).getFixedValue(), 1));
    const llvm::AtomicOrdering ord = orderingOf(e.index);
    const auto toMem = [&](llvm::Value* v) {
        return sty(t)->isIntegerTy(1) ? B_.CreateZExt(v, mt) : v;
    };
    const auto fromMem = [&](llvm::Value* v) -> llvm::Value* {
        return sty(t)->isIntegerTy(1) ? B_.CreateICmpNE(v, llvm::ConstantInt::get(mt, 0)) : v;
    };
    switch (e.kind) {
    case hir::ExprKind::AtomicLoad: {
        llvm::LoadInst* l = B_.CreateAlignedLoad(mt, ptr, align);
        l->setAtomic(ord);
        return fromMem(l);
    }
    case hir::ExprKind::AtomicStore: {
        llvm::StoreInst* s = B_.CreateAlignedStore(toMem(genScalar(*e.rhs)), ptr, align);
        s->setAtomic(ord);
        return nullptr;
    }
    case hir::ExprKind::AtomicXchg:
        return fromMem(B_.CreateAtomicRMW(llvm::AtomicRMWInst::Xchg, ptr, toMem(genScalar(*e.rhs)), align, ord));
    case hir::ExprKind::AtomicAdd:
        return B_.CreateAtomicRMW(llvm::AtomicRMWInst::Add, ptr, genScalar(*e.rhs), align, ord);
    case hir::ExprKind::AtomicCas: {
        llvm::Value* expected = toMem(genScalar(*e.rhs));
        llvm::Value* desired = toMem(genScalar(*e.args[0]));
        llvm::Value* r = B_.CreateAtomicCmpXchg(ptr, expected, desired, align, ord, failureOrdering(ord));
        return fromMem(B_.CreateExtractValue(r, 0));
    }
    default: break;
    }
    return nullptr;
}

llvm::Value* Gen::genAsm(const hir::Expr& e) {
    // Template placeholders `{k}` follow the operand order of the call; LLVM numbers outputs
    // first, then inputs: build the mapping.
    std::vector<int> llvm_index(e.asm_operands.size(), 0);
    int outs = 0;
    for (const hir::AsmOperand& op : e.asm_operands) {
        if (op.kind != hir::AsmOperand::Kind::In) {
            ++outs;
        }
    }
    int out_i = 0;
    int in_i = 0;
    for (std::size_t k = 0; k < e.asm_operands.size(); ++k) {
        if (e.asm_operands[k].kind == hir::AsmOperand::Kind::In) {
            llvm_index[k] = outs + in_i++;
        } else {
            llvm_index[k] = out_i++;
            if (e.asm_operands[k].kind == hir::AsmOperand::Kind::InOut) {
                ++in_i;  // the tied input
            }
        }
    }
    std::string text;
    for (std::size_t i = 0; i < e.text.size(); ++i) {
        const char c = e.text[i];
        if (c == '$') {
            text += "$$";
        } else if (c == '{' && i + 1 < e.text.size() && std::isdigit(static_cast<unsigned char>(e.text[i + 1])) != 0) {
            std::size_t j = i + 1;
            int n = 0;
            while (j < e.text.size() && std::isdigit(static_cast<unsigned char>(e.text[j])) != 0) {
                n = n * 10 + (e.text[j] - '0');
                ++j;
            }
            if (j < e.text.size() && e.text[j] == '}' && static_cast<std::size_t>(n) < e.asm_operands.size()) {
                text += "$" + std::to_string(llvm_index[n]);
                i = j;
                continue;
            }
            text += c;
        } else {
            text += c;
        }
    }

    std::vector<llvm::Type*> out_types;
    std::vector<llvm::Type*> in_types;
    std::vector<llvm::Value*> in_values;
    std::vector<const hir::AsmOperand*> out_ops;
    std::string out_constraints;
    std::string in_constraints;
    const auto regSpec = [&](const hir::AsmOperand& op, const char* prefix) {
        return op.reg == "reg" ? std::string(prefix) + "r" : std::string(prefix) + "{" + op.reg + "}";
    };
    for (const hir::AsmOperand& op : e.asm_operands) {
        if (op.kind == hir::AsmOperand::Kind::In) {
            continue;
        }
        out_constraints += (out_constraints.empty() ? "" : ",") + regSpec(op, "=");
        out_types.push_back(sty(op.value->type));
        out_ops.push_back(&op);
    }
    int out_idx = 0;
    for (const hir::AsmOperand& op : e.asm_operands) {
        if (op.kind == hir::AsmOperand::Kind::In) {
            in_constraints += (in_constraints.empty() ? "" : ",") + regSpec(op, "");
            in_types.push_back(sty(op.value->type));
            in_values.push_back(genScalar(*op.value));
        } else {
            if (op.kind == hir::AsmOperand::Kind::InOut) {
                in_constraints += (in_constraints.empty() ? "" : ",") + std::to_string(out_idx);
                in_types.push_back(sty(op.value->type));
                in_values.push_back(genScalar(*op.value));
            }
            ++out_idx;
        }
    }
    std::string constraints = out_constraints;
    if (!in_constraints.empty()) {
        constraints += (constraints.empty() ? "" : ",") + in_constraints;
    }
    for (const std::string& c : e.clobbers) {
        constraints += (constraints.empty() ? "" : ",") + std::string("~{") + c + "}";
    }
    if (!e.nomem) {
        constraints += (constraints.empty() ? "" : ",") + std::string("~{memory}");
    }
    llvm::Type* ret = llvm::Type::getVoidTy(ctx_);
    if (out_types.size() == 1) {
        ret = out_types[0];
    } else if (out_types.size() > 1) {
        ret = llvm::StructType::get(ctx_, out_types);
    }
    llvm::FunctionType* fty = llvm::FunctionType::get(ret, in_types, false);
    const bool intel = in_.config.target.arch == "x86_64";
    llvm::InlineAsm* ia = llvm::InlineAsm::get(fty, text, constraints, true, false,
                                               intel ? llvm::InlineAsm::AD_Intel : llvm::InlineAsm::AD_ATT);
    llvm::CallInst* call = B_.CreateCall(ia, in_values);
    for (std::size_t i = 0; i < out_ops.size(); ++i) {
        llvm::Value* v = out_ops.size() == 1 ? static_cast<llvm::Value*>(call) : B_.CreateExtractValue(call, i);
        storeScalar(out_ops[i]->value->type, v, genAddr(*out_ops[i]->value));
    }
    if (e.noreturn) {
        B_.CreateUnreachable();
        llvm::BasicBlock* dead = llvm::BasicBlock::Create(ctx_, "after.asm", cur_fn_);
        B_.SetInsertPoint(dead);
    }
    return nullptr;
}

llvm::Value* Gen::genScalar(const hir::Expr& e) {
    setLoc(e.span);
    switch (e.kind) {
    case hir::ExprKind::IntConst: {
        const Type& t = e.type.kind == TypeKind::Enum && e.type.elem ? *e.type.elem : e.type;
        if (t.kind == TypeKind::Float) {
            return llvm::ConstantFP::get(sty(t), static_cast<double>(static_cast<std::int64_t>(e.int_value)));
        }
        return llvm::ConstantInt::get(sty(t), e.int_value);
    }
    case hir::ExprKind::FloatConst: return llvm::ConstantFP::get(sty(e.type), e.float_value);
    case hir::ExprKind::BoolConst: return B_.getInt1(e.bool_value);
    case hir::ExprKind::StrConst: return stringGlobal(e.text);
    case hir::ExprKind::Nil: return llvm::Constant::getNullValue(ptrTy());
    case hir::ExprKind::FuncRef: {
        llvm::Function* f = functionNamed(e.text);
        if (f == nullptr) {
            error(e.span, std::format("unknown function `{}`", e.text));
            return llvm::Constant::getNullValue(ptrTy());
        }
        return f;
    }
    case hir::ExprKind::LocalRef:
    case hir::ExprKind::GlobalRef:
    case hir::ExprKind::Deref:
    case hir::ExprKind::Field:
    case hir::ExprKind::Index:
        return loadScalar(e.type, genAddr(e));
    case hir::ExprKind::AddrOf: {
        if (e.lhs->kind == hir::ExprKind::GlobalRef || B_.GetInsertBlock() == nullptr) {
            // Constant context: fold through genConst.
            if (B_.GetInsertBlock() == nullptr) {
                return genConst(e);
            }
        }
        return genAddr(*e.lhs);
    }
    case hir::ExprKind::Unary: {
        llvm::Value* v = genScalar(*e.lhs);
        if (e.unary_op == UnaryOp::Not || e.unary_op == UnaryOp::BitNot) {
            return B_.CreateNot(v);
        }
        return e.type.kind == TypeKind::Float ? B_.CreateFNeg(v) : B_.CreateNeg(v);
    }
    case hir::ExprKind::Binary: return genBinary(e);
    case hir::ExprKind::Select: {
        llvm::Value* c = genScalar(*e.lhs);
        llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(ctx_, "sel.then", cur_fn_);
        llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(ctx_, "sel.else", cur_fn_);
        llvm::BasicBlock* end_bb = llvm::BasicBlock::Create(ctx_, "sel.end", cur_fn_);
        B_.CreateCondBr(c, then_bb, else_bb);
        B_.SetInsertPoint(then_bb);
        llvm::Value* a = genScalar(*e.rhs);
        llvm::BasicBlock* then_end = B_.GetInsertBlock();
        B_.CreateBr(end_bb);
        B_.SetInsertPoint(else_bb);
        llvm::Value* b = genScalar(*e.args[0]);
        llvm::BasicBlock* else_end = B_.GetInsertBlock();
        B_.CreateBr(end_bb);
        B_.SetInsertPoint(end_bb);
        llvm::PHINode* phi = B_.CreatePHI(a->getType(), 2);
        phi->addIncoming(a, then_end);
        phi->addIncoming(b, else_end);
        return phi;
    }
    case hir::ExprKind::Cast: return genCast(e);
    case hir::ExprKind::New: return genNew(e);
    case hir::ExprKind::Call:
    case hir::ExprKind::CallIndirect:
    case hir::ExprKind::CallVirtual:
    case hir::ExprKind::CallItable:
    case hir::ExprKind::Syscall: {
        llvm::Value* mem = nullptr;
        return genCall(e, &mem);
    }
    case hir::ExprKind::AtomicLoad:
    case hir::ExprKind::AtomicStore:
    case hir::ExprKind::AtomicXchg:
    case hir::ExprKind::AtomicCas:
    case hir::ExprKind::AtomicAdd:
        return genAtomic(e);
    case hir::ExprKind::Fence:
        B_.CreateFence(orderingOf(e.index));
        return nullptr;
    case hir::ExprKind::Asm:
        return genAsm(e);
    case hir::ExprKind::External:
    case hir::ExprKind::ExternalCall:
        error(e.span, std::format("unresolved external reference `{}.{}`", e.text, e.member));
        return llvm::UndefValue::get(isUnknownType(e.type) ? llvm::Type::getInt32Ty(ctx_) : sty(e.type));
    default:
        error(e.span, "unsupported expression in code generation");
        return llvm::UndefValue::get(llvm::Type::getInt32Ty(ctx_));
    }
}

// ---------------------------------------------------------------------------------------------
// Entry point and driver

void Gen::emitEntry() {
    const TargetInfo& t = in_.config.target;
    const hir::Function* main_fn = nullptr;
    const hir::Function* start_fn = nullptr;
    for (const hir::Function& f : hm_.functions) {
        if (f.module.empty() && f.name == "main" && f.has_body) {
            main_fn = &f;
        }
        if (!hm_.start_function.empty() && f.name == hm_.start_function && f.has_body) {
            start_fn = &f;
        }
    }
    if (main_fn == nullptr) {
        return;
    }
    const bool mach_o = t.os == "macos";
    const auto symOf = [mach_o](const hir::Function& f) {
        return (mach_o ? "_" : "") + (f.symbol.empty() ? f.name : f.symbol);
    };
    // `register(start)` runs first; the stub never returns, so the clobbered return registers and
    // link register are harmless. Both calls keep the stack 16-byte aligned at the call site.
    const std::string call_start = start_fn == nullptr ? "" : "\tcall " + symOf(*start_fn) + "\n";
    const std::string sym = symOf(*main_fn);
    const bool returns_int = main_fn->ret.kind == TypeKind::Int;
    std::string text;
    if (t.os == "linux" && t.arch == "x86_64") {
        text = ".text\n.globl _start\n_start:\n\txorl %ebp, %ebp\n\tandq $-16, %rsp\n" + call_start + "\tcall " + sym + "\n";
        text += returns_int ? "\tmovl %eax, %edi\n" : "\txorl %edi, %edi\n";
        text += "\tmovl $231, %eax\n\tsyscall\n";   // exit_group: ends the worker threads too
    } else if (t.os == "linux") {
        text = ".text\n.globl _start\n_start:\n";
        if (start_fn != nullptr) {
            text += "\tbl " + symOf(*start_fn) + "\n";
        }
        text += "\tbl " + sym + "\n";
        if (!returns_int) {
            text += "\tmov x0, #0\n";
        }
        text += "\tmov x8, #94\n\tsvc #0\n";   // exit_group
    } else if (t.os == "macos" && t.arch == "x86_64") {
        text = ".text\n.globl _start\n_start:\n\txorl %ebp, %ebp\n\tandq $-16, %rsp\n" + call_start + "\tcall " + sym + "\n";
        text += returns_int ? "\tmovl %eax, %edi\n" : "\txorl %edi, %edi\n";
        text += "\tmovl $0x2000001, %eax\n\tsyscall\n";
    } else if (t.os == "macos") {
        text = ".text\n.globl _start\n.p2align 2\n_start:\n";
        if (start_fn != nullptr) {
            text += "\tbl " + symOf(*start_fn) + "\n";
        }
        text += "\tbl " + sym + "\n";
        if (!returns_int) {
            text += "\tmov x0, #0\n";
        }
        text += "\tmov x16, #1\n\tsvc #0x80\n";
    } else if (t.os == "windows") {
        // The process ends through ntdll's `NtTerminateProcess` (the linker driver always imports
        // it for Windows executables): its exit status is `main`'s result, or 0 for a void `main`.
        text = ".text\n.globl mainCRTStartup\nmainCRTStartup:\n\tsubq $40, %rsp\n" + call_start + "\tcall " + sym + "\n";
        text += returns_int ? "\tmovl %eax, %edx\n" : "\txorl %edx, %edx\n";
        text += "\tmovq $-1, %rcx\n\tcall NtTerminateProcess\n\tint3\n";
        // Without a C runtime nothing else defines the stack probe LLVM calls for frames over 4 KiB
        // (it touches every page so the guard page extends the stack) nor the marker MSVC-style
        // object code references when floating point appears.
        text += ".globl __chkstk\n__chkstk:\n\tpushq %rcx\n\tpushq %rax\n\tcmpq $0x1000, %rax\n"
                "\tleaq 24(%rsp), %rcx\n\tjb 2f\n1:\n\tsubq $0x1000, %rcx\n\ttestq %rcx, (%rcx)\n"
                "\tsubq $0x1000, %rax\n\tcmpq $0x1000, %rax\n\tja 1b\n2:\n\tsubq %rax, %rcx\n"
                "\ttestq %rcx, (%rcx)\n\tpopq %rax\n\tpopq %rcx\n\tret\n";
        text += ".data\n.globl _fltused\n_fltused:\n\t.long 0\n";
    } else {
        return;
    }
    mod_.appendModuleInlineAsm(text);
    out_.has_entry = true;
}

bool Gen::run() {
    for (const hir::Global& g : hm_.globals) {
        declareGlobal(g);
    }
    for (const hir::Function& f : hm_.functions) {
        declareFunction(f);
    }
    buildTables();

    if (in_.config.debug) {
        di_ = std::make_unique<llvm::DIBuilder>(mod_);
        llvm::DIFile* file = di_->createFile(in_.module_name, ".");
        cu_ = di_->createCompileUnit(llvm::dwarf::DW_LANG_C, file, "xlang", in_.config.opt_level > 0, "", 0);
        mod_.addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
        mod_.addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4);
    }
    for (const hir::Global& g : hm_.globals) {
        if (definedHere(in_, g.module)) {
            defineGlobal(g);
        }
    }
    for (const hir::Function& f : hm_.functions) {
        if (definedHere(in_, f.module)) {
            defineFunction(f);
        }
    }
    if (in_.emit_entry && (!in_.only_module || in_.only_module->empty())) {
        emitEntry();
    }
    if (di_) {
        di_->finalize();
    }
    if (failed_) {
        return false;
    }
    std::string verify_error;
    llvm::raw_string_ostream os(verify_error);
    if (llvm::verifyModule(mod_, &os)) {
        os.flush();
        if (std::getenv("XLANG_DUMP_IR") != nullptr) {
            mod_.print(llvm::errs(), nullptr);
        }
        in_.diagnostics.error("E0714", Span{},
                              "internal error: generated module failed verification: " + verify_error.substr(0, 600),
                              "code generation");
        return false;
    }
    return true;
}

}  // namespace

std::unique_ptr<Artifact> generate(const CodegenInput& input) {
    auto impl = std::make_unique<Artifact::Impl>();
    std::string error;
    impl->machine = createMachine(input.config, impl->triple, error);
    if (!impl->machine) {
        input.diagnostics.error("E0714", Span{}, error, "code generation");
        return nullptr;
    }
    impl->context = std::make_unique<llvm::LLVMContext>();
    impl->module = std::make_unique<llvm::Module>(input.module_name, *impl->context);
    impl->module->setTargetTriple(llvm::Triple(impl->triple));
    impl->module->setDataLayout(impl->machine->createDataLayout());
    Gen gen(input, *impl);
    if (!gen.run()) {
        return nullptr;
    }
    return std::make_unique<Artifact>(std::move(impl));
}

}  // namespace xlang::codegen
