#include "codegen/generator.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include <stdexcept>

#include "index.h"
#include "llvm/IR/Constants.h"
#include "refanal/ir.h"
#include "semanal/types.h"

namespace acu::codegen {
namespace {
class Generator {
public:
    Generator(
        llvm::LLVMContext& context,
        const refanal::ir::Module& module,
        const llvm::DataLayout& layout
    )
        : context_(context), ir_module_(module), layout_(layout) {}

    std::unique_ptr<llvm::Module> generate() {
        llvm_module_ = std::make_unique<llvm::Module>("acu_module", context_);
        llvm_module_->setDataLayout(layout_);

        functions_.clear();
        functions_.reserve(ir_module_.funcs().size());
        for (const auto& ir_func : ir_module_.funcs()) {
            std::vector<llvm::Type*> param_types;
            for (const auto& param : ir_func.params())
                param_types.push_back(get_rep_type(param.type));
            llvm::FunctionType* func_type = llvm::FunctionType::get(
                get_rep_type(ir_func.return_type()), param_types, false
            );
            functions_.push_back(
                llvm::Function::Create(
                    func_type,
                    llvm::Function::ExternalLinkage,
                    ir_func.name(),
                    *llvm_module_
                )
            );
        }
        for (auto i : ir_module_.funcs().indices())
            generate_func(ir_module_.func(i), functions_[i]);
        return std::move(llvm_module_);
    }

private:
    void generate_func(
        const refanal::ir::Func& ir_func, llvm::Function* llvm_func
    );

    llvm::Type* get_base_type(types::TypeId type_id) {
        const auto& t = ir_module_.types().get(type_id);
        return t.data.visit(
            [&](const types::Type::None&) -> llvm::Type* {
                return llvm::Type::getVoidTy(context_);
            },
            [&](const types::Type::Nothing&) -> llvm::Type* {
                return llvm::Type::getVoidTy(context_);
            },
            [&](const types::Type::Bool&) -> llvm::Type* {
                return llvm::Type::getInt1Ty(context_);
            },
            [&](const types::Type::Int& i) -> llvm::Type* {
                return llvm::Type::getIntNTy(context_, i.bits);
            },
            [&](const types::Type::Float& f) -> llvm::Type* {
                return (f == types::Type::Float::F32)
                           ? llvm::Type::getFloatTy(context_)
                           : llvm::Type::getDoubleTy(context_);
            },
            [&](const types::Type::Ptr&) -> llvm::Type* {
                return llvm::PointerType::get(context_, 0);
            },
            [&](const types::Type::Func&) -> llvm::Type* {
                return llvm::PointerType::get(context_, 0);
            },
            [&](const types::Type::Array& a) -> llvm::Type* {
                return llvm::ArrayType::get(get_rep_type(a.item), a.length);
            },
            [&](const types::Type::Struct& s) -> llvm::Type* {
                if (auto* st =
                        llvm::StructType::getTypeByName(context_, s.name)) {
                    return st;
                }
                std::vector<llvm::Type*> fields;
                fields.reserve(s.fields.size());
                for (const auto& f : s.fields) {
                    fields.push_back(get_rep_type(f.type));
                }
                return llvm::StructType::create(context_, fields, s.name);
            },
            [&](const auto&) -> llvm::Type* {
                return llvm::Type::getVoidTy(context_);
            }
        );
    }

    llvm::Type* get_rep_type(types::SpecType st) {
        llvm::Type* base = get_base_type(st.type);
        if (st.specifier == types::Specifier::Var)
            return llvm::PointerType::get(context_, 0);
        if (st.specifier == types::Specifier::Let && !is_small(base))
            return llvm::PointerType::get(context_, 0);
        return base;
    }

    bool is_small(llvm::Type* type) {
        if (type->isVoidTy()) return true;
        if (!type->isSized()) return false;
        return layout_.getTypeAllocSize(type) <= 16;
    }

    bool is_ref(types::SpecType st) {
        return st.specifier == types::Specifier::Var ||
               (st.specifier == types::Specifier::Let &&
                !is_small(get_base_type(st.type)));
    }

    llvm::LLVMContext& context_;
    const refanal::ir::Module& ir_module_;
    const llvm::DataLayout& layout_;
    std::unique_ptr<llvm::Module> llvm_module_;
    IndexVector<llvm::Function*, refanal::ir::FuncRef> functions_;

    friend class FuncGenerator;
};

class FuncGenerator {
public:
    FuncGenerator(
        Generator& generator,
        const refanal::ir::Func& ir_func,
        llvm::Function& llvm_func
    )
        : generator(&generator),
          ir_func(&ir_func),
          llvm_func(&llvm_func),
          builder_(generator.context_) {}

    void generate() {
        if (ir_func->blocks().empty()) return;
        blocks_.reserve(ir_func->blocks().size());
        for (auto i : ir_func->blocks().indices()) {
            blocks_.push_back(
                llvm::BasicBlock::Create(
                    generator->context_,
                    "block" + std::to_string(i.index),
                    llvm_func
                )
            );
        }
        inst_values_.resize(ir_func->insts().size());

        builder_.SetInsertPoint(&llvm_func->getEntryBlock());
        IndexVector<llvm::Value*, refanal::ir::ParamRef> args;
        for (auto i : ir_func->params().indices()) {
            auto param = ir_func->param(i);
            llvm_func->getArg(i.index)->setName(param.name);
            args.push_back(builder_.CreateAlloca(
                get_rep_type(param.type),
                nullptr,
                std::string(param.name) + ".arg"
            ));
        }
        for (auto i : ir_func->params().indices()) {
            builder_.CreateStore(llvm_func->getArg(i.index), args[i]);
        }

        for (auto b_idx : ir_func->blocks().indices()) {
            builder_.SetInsertPoint(blocks_[b_idx]);
            for (auto inst_ref : ir_func->block(b_idx).insts) {
                const auto& inst = ir_func->inst(inst_ref);
                inst_values_[inst_ref] = generate_inst(inst, args);
            }
        }
        llvm::verifyFunction(*llvm_func);
    }

private:
    llvm::Value* generate_inst(
        const refanal::ir::Inst& inst,
        const IndexVector<llvm::Value*, refanal::ir::ParamRef>& args
    ) {
        return inst.data.visit(
            [&](const refanal::ir::Inst::Const& c) -> llvm::Value* {
                return c.value.visit(
                    [&](bool b) -> llvm::Value* {
                        return llvm::ConstantInt::get(
                            llvm::Type::getInt1Ty(generator->context_), b
                        );
                    },
                    [&](std::int64_t i) -> llvm::Value* {
                        return llvm::ConstantInt::get(
                            get_base_type(inst.type.type), i
                        );
                    },
                    [&](double d) -> llvm::Value* {
                        return llvm::ConstantFP::get(
                            get_base_type(inst.type.type), d
                        );
                    },
                    [&](char32_t ch) -> llvm::Value* {
                        return llvm::ConstantInt::get(
                            llvm::Type::getInt32Ty(generator->context_), ch
                        );
                    },
                    [&](std::string_view s) -> llvm::Value* {
                        return builder_.CreateGlobalString(s);
                    },
                    [&](refanal::ir::FuncRef f) -> llvm::Value* {
                        return generator->functions_[f];
                    }
                );
            },
            [&](const refanal::ir::Inst::VarDecl& v) -> llvm::Value* {
                llvm::IRBuilder<> eb(
                    &llvm_func->getEntryBlock(),
                    llvm_func->getEntryBlock().begin()
                );
                llvm::Type* bt = get_base_type(inst.type.type);
                if (!is_ref(inst.type)) {
                    return eb.CreateAlloca(
                        bt,
                        nullptr,
                        llvm::StringRef(v.name.data(), v.name.size())
                    );
                } else {
                    llvm::Value* slot = eb.CreateAlloca(
                        builder_.getPtrTy(),
                        nullptr,
                        llvm::StringRef(v.name.data(), v.name.size())
                    );
                    return slot;
                }
            },
            [&](const refanal::ir::Inst::LoadVar& l) -> llvm::Value* {
                const auto& var_info = ir_func->inst(l.var);
                return get_value_from_ptr(
                    inst_values_[l.var], var_info.type, inst.type
                );
            },
            [&](const refanal::ir::Inst::LoadParam& lp) -> llvm::Value* {
                auto param = ir_func->param(lp.param);
                return get_value_from_ptr(
                    args[lp.param], param.type, inst.type
                );
            },
            [&](const refanal::ir::Inst::Store& s) -> llvm::Value* {
                llvm::Value* dest_storage = inst_values_[s.var];
                const auto& dest_info = ir_func->inst(s.var);
                auto value = get_value(s.value, dest_info.type.specifier);
                return builder_.CreateStore(value, dest_storage);
            },
            [&](const refanal::ir::Inst::Binary& b) -> llvm::Value* {
                llvm::Value* l = get_value(b.left, types::Specifier::Val);
                llvm::Value* r = get_value(b.right, types::Specifier::Val);
                bool f = l->getType()->isFloatingPointTy();
                const auto& left_type = get_type(b.left);
                bool is_signed = true;
                if (auto* i = left_type.data.get_if<types::Type::Int>()) {
                    is_signed = i->is_signed;
                }

                switch (b.op) {
                    case refanal::ir::Inst::BinaryOp::Add:
                        return f ? builder_.CreateFAdd(l, r)
                                 : builder_.CreateAdd(l, r);
                    case refanal::ir::Inst::BinaryOp::Sub:
                        return f ? builder_.CreateFSub(l, r)
                                 : builder_.CreateSub(l, r);
                    case refanal::ir::Inst::BinaryOp::Mul:
                        return f ? builder_.CreateFMul(l, r)
                                 : builder_.CreateMul(l, r);
                    case refanal::ir::Inst::BinaryOp::Div:
                        if (f) return builder_.CreateFDiv(l, r);
                        return is_signed ? builder_.CreateSDiv(l, r)
                                         : builder_.CreateUDiv(l, r);
                    case refanal::ir::Inst::BinaryOp::Mod:
                        if (f) return builder_.CreateFRem(l, r);
                        return is_signed ? builder_.CreateSRem(l, r)
                                         : builder_.CreateURem(l, r);
                    case refanal::ir::Inst::BinaryOp::LShift:
                        return builder_.CreateShl(l, r);
                    case refanal::ir::Inst::BinaryOp::RShift:
                        return is_signed ? builder_.CreateAShr(l, r)
                                         : builder_.CreateLShr(l, r);
                    case refanal::ir::Inst::BinaryOp::BitAnd:
                        return builder_.CreateAnd(l, r);
                    case refanal::ir::Inst::BinaryOp::BitOr:
                        return builder_.CreateOr(l, r);
                    case refanal::ir::Inst::BinaryOp::BitXor:
                        return builder_.CreateXor(l, r);
                }
                return nullptr;
            },
            [&](const refanal::ir::Inst::Comparison& c) -> llvm::Value* {
                llvm::Value* l = get_value(c.left, types::Specifier::Val);
                llvm::Value* r = get_value(c.right, types::Specifier::Val);
                bool f = l->getType()->isFloatingPointTy();
                const auto& left_type = get_type(c.left);
                bool is_signed = true;
                if (auto* i = left_type.data.get_if<types::Type::Int>()) {
                    is_signed = i->is_signed;
                }

                if (f) {
                    switch (c.op) {
                        case refanal::ir::Inst::ComparisonOp::Less:
                            return builder_.CreateFCmpOLT(l, r);
                        case refanal::ir::Inst::ComparisonOp::Greater:
                            return builder_.CreateFCmpOGT(l, r);
                        case refanal::ir::Inst::ComparisonOp::LessEqual:
                            return builder_.CreateFCmpOLE(l, r);
                        case refanal::ir::Inst::ComparisonOp::GreaterEqual:
                            return builder_.CreateFCmpOGE(l, r);
                        case refanal::ir::Inst::ComparisonOp::Equal:
                            return builder_.CreateFCmpOEQ(l, r);
                        case refanal::ir::Inst::ComparisonOp::NotEqual:
                            return builder_.CreateFCmpONE(l, r);
                    }
                } else {
                    switch (c.op) {
                        case refanal::ir::Inst::ComparisonOp::Less:
                            return is_signed ? builder_.CreateICmpSLT(l, r)
                                             : builder_.CreateICmpULT(l, r);
                        case refanal::ir::Inst::ComparisonOp::Greater:
                            return is_signed ? builder_.CreateICmpSGT(l, r)
                                             : builder_.CreateICmpUGT(l, r);
                        case refanal::ir::Inst::ComparisonOp::LessEqual:
                            return is_signed ? builder_.CreateICmpSLE(l, r)
                                             : builder_.CreateICmpULE(l, r);
                        case refanal::ir::Inst::ComparisonOp::GreaterEqual:
                            return is_signed ? builder_.CreateICmpSGE(l, r)
                                             : builder_.CreateICmpUGE(l, r);
                        case refanal::ir::Inst::ComparisonOp::Equal:
                            return builder_.CreateICmpEQ(l, r);
                        case refanal::ir::Inst::ComparisonOp::NotEqual:
                            return builder_.CreateICmpNE(l, r);
                    }
                }
                return nullptr;
            },
            [&](const refanal::ir::Inst::Call& c) -> llvm::Value* {
                llvm::Value* callee = get_value(c.value, types::Specifier::Val);

                const auto& t = get_type(c.value);
                const types::Type::Func* func_type_info = nullptr;

                if (auto* f = t.data.get_if<types::Type::Func>()) {
                    func_type_info = f;
                } else if (auto* p = t.data.get_if<types::Type::Ptr>()) {
                    func_type_info = generator->ir_module_.types()
                                         .get(p->type.type)
                                         .data.get_if<types::Type::Func>();
                }

                if (!func_type_info) return nullptr;

                std::vector<llvm::Value*> args;
                args.reserve(c.args.count);
                auto arg_refs = ir_func->inst_refs(c.args);
                for (size_t i = 0; i < c.args.count; i++) {
                    args.push_back(get_value(
                        arg_refs[i], func_type_info->params[i].specifier
                    ));
                }

                std::vector<llvm::Type*> param_types;
                param_types.reserve(func_type_info->params.size());
                for (const auto& p : func_type_info->params) {
                    param_types.push_back(get_rep_type(p));
                }
                llvm::FunctionType* llvm_func_type = llvm::FunctionType::get(
                    get_rep_type(func_type_info->return_type),
                    param_types,
                    false
                );

                return builder_.CreateCall(llvm_func_type, callee, args);
            },
            [&](const refanal::ir::Inst::Jump& j) -> llvm::Value* {
                return builder_.CreateBr(blocks_[j.target]);
            },
            [&](const refanal::ir::Inst::Branch& b) -> llvm::Value* {
                return builder_.CreateCondBr(
                    get_value(b.condition, types::Specifier::Val),
                    blocks_[b.true_target],
                    blocks_[b.false_target]
                );
            },
            [&](const refanal::ir::Inst::Return& r) -> llvm::Value* {
                if (r.value) {
                    return builder_.CreateRet(
                        get_value(*r.value, ir_func->return_type().specifier)
                    );
                } else {
                    return builder_.CreateRetVoid();
                }
            },
            [&](const refanal::ir::Inst::Cast& cast) -> llvm::Value* {
                auto from_type_id = ir_func->inst(cast.value).type.type;
                auto to_type_id = inst.type.type;
                if (from_type_id == to_type_id) {
                    return get_value(cast.value, inst.type.specifier);
                }
                auto& from_type =
                    generator->ir_module_.types().get(from_type_id);
                auto& to_type = generator->ir_module_.types().get(to_type_id);
                if (from_type.data.is<types::Type::Array>() &&
                    to_type.data.is<types::Type::Ptr>()) {
                    return get_value(cast.value, types::Specifier::Var);
                }

                llvm::Value* val = get_value(cast.value, types::Specifier::Val);
                llvm::Type* to_llvm_type = get_base_type(to_type_id);

                return to_type.data.visit(
                    [&](const types::Type::Bool&) -> llvm::Value* {
                        if (to_type.data.is<types::Type::Int>())
                            return builder_.CreateICmpNE(
                                val, llvm::ConstantInt::get(val->getType(), 0)
                            );
                        if (to_type.data.is<types::Type::Float>())
                            return builder_.CreateFCmpONE(
                                val, llvm::ConstantFP::getZero(val->getType())
                            );
                        if (to_type.data.is<types::Type::Ptr>())
                            return builder_.CreateICmpNE(
                                val,
                                llvm::ConstantPointerNull::get(
                                    llvm::cast<llvm::PointerType>(
                                        val->getType()
                                    )
                                )
                            );
                        throw std::runtime_error("unsupported cast to bool");
                    },
                    [&](const types::Type::Int& to_i) -> llvm::Value* {
                        if (to_type.data.is<types::Type::Int>()) {
                            return builder_.CreateIntCast(
                                val, to_llvm_type, to_i.is_signed
                            );
                        }
                        if (to_type.data.is<types::Type::Float>()) {
                            return to_i.is_signed ? builder_.CreateFPToSI(
                                                        val, to_llvm_type
                                                    )
                                                  : builder_.CreateFPToUI(
                                                        val, to_llvm_type
                                                    );
                        }
                        if (to_type.data.is<types::Type::Ptr>()) {
                            return builder_.CreatePtrToInt(val, to_llvm_type);
                        }
                        throw std::runtime_error("unsupported cast to int");
                    },
                    [&](const types::Type::Float& to_f) -> llvm::Value* {
                        if (to_type.data.is<types::Type::Int>()) {
                            const auto& from_i =
                                from_type.data.get<types::Type::Int>();
                            return from_i.is_signed ? builder_.CreateSIToFP(
                                                          val, to_llvm_type
                                                      )
                                                    : builder_.CreateUIToFP(
                                                          val, to_llvm_type
                                                      );
                        }
                        if (to_type.data.is<types::Type::Float>()) {
                            return builder_.CreateFPCast(val, to_llvm_type);
                        }
                        throw std::runtime_error("unsupported cast to float");
                    },
                    [&](const types::Type::Ptr&) -> llvm::Value* {
                        if (to_type.data.is<types::Type::Ptr>()) {
                            return val;
                        }
                        if (to_type.data.is<types::Type::Int>()) {
                            return builder_.CreateIntToPtr(val, to_llvm_type);
                        }
                        throw std::runtime_error("unsupported cast to ptr");
                    },
                    [&](const auto&) -> llvm::Value* {
                        throw std::runtime_error("unsupported cast");
                    }
                );
            },
            [&](const refanal::ir::Inst::CreateStruct& cs) -> llvm::Value* {
                llvm::Type* st = get_base_type(cs.struct_type);
                llvm::Value* sv = llvm::UndefValue::get(st);
                auto args = ir_func->inst_refs(cs.args);
                const auto& struct_def = generator->ir_module_.types()
                                             .get(cs.struct_type)
                                             .data.get<types::Type::Struct>();
                for (uint32_t i = 0; i < args.size(); ++i) {
                    sv = builder_.CreateInsertValue(
                        sv,
                        get_value(args[i], struct_def.fields[i].type.specifier),
                        {i}
                    );
                }
                return sv;
            },
            [&](const refanal::ir::Inst::GetField& gf) -> llvm::Value* {
                const auto& base_info = ir_func->inst(gf.value);
                const auto& struct_type =
                    get_type(base_info).data.get<types::Type::Struct>();
                if (is_ref(base_info.type)) {
                    llvm::Value* ptr = inst_values_[gf.value];
                    llvm::Type* base_type = get_base_type(base_info.type.type);
                    llvm::Value* field_ptr =
                        builder_.CreateStructGEP(base_type, ptr, gf.index);
                    return get_value_from_ptr(
                        field_ptr, struct_type.fields[gf.index].type, inst.type
                    );
                } else {
                    llvm::Value* val =
                        get_value(gf.value, types::Specifier::Val);
                    return builder_.CreateExtractValue(val, {gf.index});
                }
            },
            [&](const refanal::ir::Inst::SetField& sf) -> llvm::Value* {
                llvm::Value* base_ptr = inst_values_[sf.var];
                const auto& base_info = ir_func->inst(sf.var);
                llvm::Type* base_type = get_base_type(base_info.type.type);
                llvm::Value* field_ptr =
                    builder_.CreateStructGEP(base_type, base_ptr, sf.index);
                const auto& struct_type =
                    get_type(base_info).data.get<types::Type::Struct>();
                llvm::Value* val = get_value(
                    sf.value, struct_type.fields[sf.index].type.specifier
                );
                return builder_.CreateStore(val, field_ptr);
            },
            [&](const refanal::ir::Inst::AddressOf& ao) -> llvm::Value* {
                return get_value(ao.value, types::Specifier::Var);
            },
            [&](const refanal::ir::Inst::Deref& d) -> llvm::Value* {
                return get_value(d.value, types::Specifier::Val);
            },
            [&](const refanal::ir::Inst::GetItem& gi) -> llvm::Value* {
                llvm::Value* idx = get_value(gi.index, types::Specifier::Val);
                auto item_type = get_item_type(gi.value);
                const auto& base_info = ir_func->inst(gi.value);
                llvm::Type* base_llvm_type = get_base_type(base_info.type.type);

                if (base_llvm_type->isArrayTy()) {
                    llvm::Value* array_ptr =
                        get_value(gi.value, types::Specifier::Var);
                    llvm::Value* item_ptr = builder_.CreateGEP(
                        base_llvm_type, array_ptr, {builder_.getInt32(0), idx}
                    );
                    return get_value_from_ptr(item_ptr, item_type, inst.type);
                } else {
                    llvm::Value* ptr =
                        get_value(gi.value, types::Specifier::Val);
                    llvm::Value* item_ptr = builder_.CreateGEP(
                        get_base_type(item_type.type), ptr, idx
                    );
                    return get_value_from_ptr(item_ptr, item_type, inst.type);
                }
            },
            [&](const refanal::ir::Inst::SetItem& si) -> llvm::Value* {
                llvm::Value* idx = get_value(si.index, types::Specifier::Val);
                auto item_type = get_item_type(si.var);
                llvm::Value* v = get_value(si.value, item_type.specifier);
                const auto& base_info = ir_func->inst(si.var);
                llvm::Type* base_llvm_type = get_base_type(base_info.type.type);

                if (base_llvm_type->isArrayTy()) {
                    llvm::Value* array_ptr =
                        get_value(si.var, types::Specifier::Var);
                    llvm::Value* item_ptr = builder_.CreateGEP(
                        base_llvm_type, array_ptr, {builder_.getInt32(0), idx}
                    );
                    return builder_.CreateStore(v, item_ptr);
                } else {
                    llvm::Value* ptr = get_value(si.var, types::Specifier::Val);
                    llvm::Value* item_ptr = builder_.CreateGEP(
                        get_base_type(item_type.type), ptr, idx
                    );
                    return builder_.CreateStore(v, item_ptr);
                }
            },
            [&](const refanal::ir::Inst::Unary& u) -> llvm::Value* {
                llvm::Value* v = get_value(u.value, types::Specifier::Val);
                switch (u.op) {
                    case refanal::ir::Inst::UnaryOp::Neg:
                        return v->getType()->isFloatingPointTy()
                                   ? builder_.CreateFNeg(v)
                                   : builder_.CreateNeg(v);
                    case refanal::ir::Inst::UnaryOp::Not:
                    case refanal::ir::Inst::UnaryOp::BitNot:
                        return builder_.CreateNot(v);
                }
                return nullptr;
            },
            [&](const refanal::ir::Inst::Array& arr) -> llvm::Value* {
                llvm::Type* at = get_base_type(inst.type.type);
                auto item_spec = get_type(inst)
                                     .data.get<types::Type::Array>()
                                     .item.specifier;
                llvm::Value* av = llvm::UndefValue::get(at);
                auto items = ir_func->inst_refs(arr.items);
                for (uint32_t k = 0; k < items.size(); ++k) {
                    av = builder_.CreateInsertValue(
                        av, get_value(items[k], item_spec), {k}
                    );
                }
                return av;
            },
            [&](const auto&) -> llvm::Value* { return nullptr; }
        );
    }

    llvm::Type* get_base_type(types::TypeId type_id) {
        return generator->get_base_type(type_id);
    }

    llvm::Type* get_rep_type(types::SpecType type) {
        return generator->get_rep_type(type);
    }

    bool is_ref(types::SpecType type) { return generator->is_ref(type); }

    const types::Type& get_type(refanal::ir::InstRef ref) {
        return get_type(ir_func->inst(ref));
    }

    const types::Type& get_type(const refanal::ir::Inst& inst) {
        types::TypeId type = inst.type.type;
        return generator->ir_module_.types().get(type);
    }

    llvm::Value* get_value(refanal::ir::InstRef ref, types::Specifier spec) {
        auto& inst = ir_func->inst(ref);
        types::SpecType result_type = {
            .type = inst.type.type, .specifier = spec
        };
        llvm::Value* value = inst_values_[ref];
        if (is_ref(result_type)) {
            if (is_ref(inst.type)) {
                return value;
            } else {
                llvm::IRBuilder<> eb(
                    &llvm_func->getEntryBlock(),
                    llvm_func->getEntryBlock().begin()
                );
                llvm::Value* temp =
                    eb.CreateAlloca(get_base_type(result_type.type));
                builder_.CreateStore(value, temp);
                return temp;
            }
        } else {
            if (is_ref(inst.type)) {
                return builder_.CreateLoad(
                    get_base_type(result_type.type), value
                );
            } else {
                return value;
            }
        }
    }
    llvm::Value* get_value_from_ptr(
        llvm::Value* ptr, types::SpecType ptr_type, types::SpecType value_type
    ) {
        if (is_ref(value_type)) {
            if (is_ref(ptr_type)) {
                return builder_.CreateLoad(get_rep_type(value_type), ptr);
            }
            return ptr;
        } else {
            auto value = builder_.CreateLoad(get_rep_type(value_type), ptr);
            if (is_ref(ptr_type)) {
                return builder_.CreateLoad(get_rep_type(value_type), value);
            }
            return value;
        }
    }
    types::SpecType get_item_type(refanal::ir::InstRef ref) {
        auto& type = get_type(ref);
        if (auto ptr = type.data.get_if<types::Type::Ptr>()) {
            return ptr->type;
        } else {
            return type.data.get<types::Type::Array>().item;
        }
    }
    Generator* generator;
    const refanal::ir::Func* ir_func;
    llvm::Function* llvm_func;
    llvm::IRBuilder<> builder_;
    IndexVector<llvm::Value*, refanal::ir::InstRef> inst_values_;
    IndexVector<llvm::BasicBlock*, refanal::ir::BlockRef> blocks_;
};

void Generator::generate_func(
    const refanal::ir::Func& ir_func, llvm::Function* llvm_func
) {
    FuncGenerator generator(*this, ir_func, *llvm_func);
    generator.generate();
}
}

std::unique_ptr<llvm::Module> generate(
    llvm::LLVMContext& context,
    const refanal::ir::Module& module,
    std::optional<llvm::DataLayout> layout
) {
    if (layout) {
        Generator generator(context, module, *layout);
        return generator.generate();
    }

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    std::string error;
    auto triple = llvm::Triple(llvm::sys::getDefaultTargetTriple());
    const llvm::Target* target =
        llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
        throw std::runtime_error("Failed to lookup target: " + error);
    }

    llvm::TargetOptions opt;
    auto machine =
        std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
            triple, "generic", "", opt, llvm::Reloc::PIC_
        ));
    auto dl = machine->createDataLayout();

    Generator generator(context, module, dl);
    return generator.generate();
}

void optimize(llvm::Module& module, llvm::OptimizationLevel level) {
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;

    llvm::PassBuilder pb;

    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);

    llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(level);
    mpm.run(module, mam);
}

void emit_object_file(llvm::Module& module, const std::string& filename) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    std::string error;
    auto triple = llvm::Triple(llvm::sys::getDefaultTargetTriple());
    const llvm::Target* target =
        llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
        throw std::runtime_error("Failed to lookup target: " + error);
    }

    llvm::TargetOptions opt;
    auto* machine = target->createTargetMachine(
        triple, "generic", "", opt, llvm::Reloc::PIC_
    );

    module.setDataLayout(machine->createDataLayout());
    module.setTargetTriple(triple);

    std::error_code ec;
    llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);
    if (ec) {
        throw std::runtime_error("Could not open file: " + ec.message());
    }

    llvm::legacy::PassManager pass;
    if (machine->addPassesToEmitFile(
            pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile
        )) {
        throw std::runtime_error(
            "TargetMachine can't emit a file of this type"
        );
    }

    pass.run(module);
    dest.flush();
}

}
