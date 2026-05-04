#include "codegen/generator.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>

#include <cassert>
#include <stdexcept>
#include <string_view>

#include "index.h"
#include "llvm/IR/Constants.h"
#include "project.h"
#include "refanal/ir.h"
#include "semanal/types.h"

namespace acu::codegen {
namespace {
class Generator {
public:
    Generator(
        llvm::LLVMContext& context,
        const refanal::ir::Module& module,
        const Project& project,
        const llvm::DataLayout& layout
    )
        : context_(&context),
          ir_module_(&module),
          project_(&project),
          layout_(&layout) {}

    std::unique_ptr<llvm::Module> generate() {
        llvm_module_ = std::make_unique<llvm::Module>(
            ir_module_->name().join(), *context_
        );
        llvm_module_->setDataLayout(*layout_);

        functions_.clear();
        functions_.reserve(ir_module_->funcs().size());
        for (const auto& ir_func : ir_module_->funcs()) {
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
                    ir_func.mangle_name(),
                    *llvm_module_
                )
            );
        }
        for (const auto& ir_func : ir_module_->used_funcs()) {
            std::vector<llvm::Type*> param_types;
            for (const auto& param : ir_func.params(*project_))
                param_types.push_back(get_rep_type(param.type));
            llvm::FunctionType* func_type = llvm::FunctionType::get(
                get_rep_type(ir_func.return_type(*project_)), param_types, false
            );
            used_funcs_.push_back(
                llvm::Function::Create(
                    func_type,
                    llvm::Function::ExternalLinkage,
                    ir_func.mangle_name(*project_),
                    *llvm_module_
                )
            );
        }
        for (auto i : ir_module_->funcs().indices()) {
            if (!ir_module_->func(i).is_extern()) {
                generate_func(ir_module_->func(i), functions_[i]);
            }
        }
        return std::move(llvm_module_);
    }

private:
    void generate_func(
        const refanal::ir::Func& ir_func, llvm::Function* llvm_func
    );

    llvm::Type* get_base_type(types::TypeId type_id) {
        const auto& t = ir_module_->types().get(type_id);
        return t.data.visit(
            [&](const types::Type::None&) -> llvm::Type* {
                return llvm::Type::getVoidTy(*context_);
            },
            [&](const types::Type::Nothing&) -> llvm::Type* {
                return llvm::Type::getVoidTy(*context_);
            },
            [&](const types::Type::Bool&) -> llvm::Type* {
                return llvm::Type::getInt1Ty(*context_);
            },
            [&](const types::Type::Int& i) -> llvm::Type* {
                return llvm::Type::getIntNTy(*context_, i.bits);
            },
            [&](const types::Type::Float& f) -> llvm::Type* {
                return (f == types::Type::Float::F32)
                           ? llvm::Type::getFloatTy(*context_)
                           : llvm::Type::getDoubleTy(*context_);
            },
            [&](const types::Type::Ptr&) -> llvm::Type* {
                return llvm::PointerType::get(*context_, 0);
            },
            [&](const types::Type::Func&) -> llvm::Type* {
                return llvm::PointerType::get(*context_, 0);
            },
            [&](const types::Type::Array& a) -> llvm::Type* {
                return llvm::ArrayType::get(get_rep_type(a.item), a.length);
            },
            [&](const types::Type::Struct& s) -> llvm::Type* {
                if (auto* st =
                        llvm::StructType::getTypeByName(*context_, s.name)) {
                    return st;
                }
                std::vector<llvm::Type*> fields;
                fields.reserve(s.fields.size());
                for (const auto& f : s.fields) {
                    fields.push_back(get_rep_type(f.type));
                }
                return llvm::StructType::create(*context_, fields, s.name);
            },
            [&](const types::Type::UsedStruct& us) -> llvm::Type* {
                if (auto* st =
                        llvm::StructType::getTypeByName(*context_, us.name())) {
                    return st;
                }
                std::vector<llvm::Type*> fields;
                fields.reserve(us.fields().size());
                for (const auto& f : us.fields()) {
                    fields.push_back(get_rep_type(f.type));
                }
                return llvm::StructType::create(*context_, fields, us.name());
            },
            [&](const auto&) -> llvm::Type* {
                return llvm::Type::getVoidTy(*context_);
            }
        );
    }

    llvm::Type* get_rep_type(types::SpecType st) {
        llvm::Type* base = get_base_type(st.type);
        if (st.specifier == types::Specifier::Var)
            return llvm::PointerType::get(*context_, 0);
        if (st.specifier == types::Specifier::Let && !is_small(base))
            return llvm::PointerType::get(*context_, 0);
        return base;
    }

    bool is_small(llvm::Type* type) {
        if (type->isVoidTy()) return true;
        if (!type->isSized()) return false;
        if (type->isArrayTy())
            return false;  // todo: нужно ли это (убирает копии маленьких
                           // массивов)
        return layout_->getTypeAllocSize(type) <= 16;
    }

    bool is_ref(types::SpecType st) {
        return st.specifier == types::Specifier::Var ||
               (st.specifier == types::Specifier::Let &&
                !is_small(get_base_type(st.type)));
    }

    llvm::LLVMContext* context_;
    const refanal::ir::Module* ir_module_;
    const Project* project_;
    const llvm::DataLayout* layout_;
    std::unique_ptr<llvm::Module> llvm_module_;
    IndexVector<llvm::Function*, refanal::ir::FuncRef> functions_;
    IndexVector<llvm::Function*, refanal::ir::UsedFuncRef> used_funcs_;

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
          builder_(*generator.context_),
          local_types_(ir_func.locals().size(), LocalType::Unknown) {}

    void generate() {
        if (ir_func->blocks().empty()) return;
        set_local_types();

        blocks_.reserve(ir_func->blocks().size());
        for (auto i : ir_func->blocks().indices()) {
            blocks_.push_back(
                llvm::BasicBlock::Create(
                    *generator->context_,
                    "block" + std::to_string(i.index),
                    llvm_func
                )
            );
        }
        builder_.SetInsertPoint(blocks_[{0}]);

        local_values_.resize(ir_func->locals().size());
        for (auto i : ir_func->locals().indices()) {
            if (local_types_[i] == LocalType::Operand) continue;
            const auto& local = ir_func->local(i);
            local_values_[i] = builder_.CreateAlloca(
                get_rep_type(local.type), nullptr, local.name
            );
        }

        for (auto i : ir_func->params().indices()) {
            if (local_types_[i] == LocalType::Operand) {
                local_values_[i] = llvm_func->getArg(i.index);
            } else {
                builder_.CreateStore(
                    llvm_func->getArg(i.index),
                    local_values_[refanal::ir::LocalRef {i.index}]
                );
            }
        }

        for (auto b_idx : ir_func->blocks().indices()) {
            builder_.SetInsertPoint(blocks_[b_idx]);
            const auto& block = ir_func->block(b_idx);
            for (auto stmt_ref : block.statements) {
                generate_statement(ir_func->statement(stmt_ref));
            }
            if (block.terminator) {
                generate_terminator(*block.terminator);
            }
        }

        llvm::verifyFunction(*llvm_func);
    }

private:
    void set_local_types() {
        for (const auto& block : ir_func->blocks()) {
            auto process_operand = [&](refanal::ir::OperandRef ref) {
                const auto& operand = ir_func->operand(ref);
                if (auto place = operand.data.get_if<refanal::ir::Place>()) {
                    auto type = ir_func->local(place->local).type;
                    types::SpecType operand_type = {
                        .type = type.type, .specifier = operand.specifier
                    };
                    if (place->projections.empty() &&
                        (is_ref(operand_type) && is_ref(type) ||
                         !is_ref(operand_type)) &&
                        local_types_[place->local] != LocalType::Alloca) {
                        local_types_[place->local] = LocalType::Operand;
                    } else {
                        local_types_[place->local] = LocalType::Alloca;
                    }
                }
            };
            for (auto ref : block.statements) {
                const auto& statement = ir_func->statement(ref);
                if (auto assign =
                        statement.data
                            .get_if<refanal::ir::Statement::Assign>()) {
                    // todo: при генерации отладочной информации нужно оставлять
                    // alloca для переменных (local с названием)
                    if (!assign->place.projections.empty() ||
                        local_types_[assign->place.local] !=
                            LocalType::Unknown) {
                        local_types_[assign->place.local] = LocalType::Alloca;
                    } else {
                        local_types_[assign->place.local] = LocalType::Operand;
                    }
                    assign->rvalue.data.visit(
                        [&](const refanal::ir::RValue::Use u) {
                            process_operand(u.operand);
                        },
                        [&](const refanal::ir::RValue::Unary u) {
                            process_operand(u.operand);
                        },
                        [&](const refanal::ir::RValue::Binary& b) {
                            process_operand(b.left);
                            process_operand(b.right);
                        },
                        [&](const refanal::ir::RValue::Comparison& c) {
                            process_operand(c.left);
                            process_operand(c.right);
                        },
                        [&](const refanal::ir::RValue::Call& c) {
                            process_operand(c.callee);
                            for (auto op : ir_func->operands(c.args)) {
                                process_operand(op);
                            }
                        },
                        [&](const refanal::ir::RValue::AddressOf a) {
                            local_types_[a.place.local] = LocalType::Alloca;
                        },
                        [&](const refanal::ir::RValue::Cast c) {
                            process_operand(c.operand);
                        },
                        [&](const refanal::ir::RValue::CreateStruct& c) {
                            for (auto op : ir_func->operands(c.args)) {
                                process_operand(op);
                            }
                        },
                        [&](const refanal::ir::RValue::Array& a) {
                            for (auto op : ir_func->operands(a.items)) {
                                process_operand(op);
                            }
                        }
                    );
                }
            }
            block.terminator->data.visit(
                [&](const refanal::ir::Terminator::Branch& b) {
                    process_operand(b.condition);
                },
                [&](const refanal::ir::Terminator::Return& r) {
                    if (r.value) {
                        process_operand(*r.value);
                    }
                },
                [&](const auto) {}
            );
        }
    }

    void generate_statement(const refanal::ir::Statement& stmt) {
        stmt.data.visit(
            [&](const refanal::ir::Statement::Assign& a) {
                auto type = ir_func->place_type(
                    a.place, generator->ir_module_->types()
                );
                llvm::Value* val = generate_rvalue(a.rvalue, type);
                if (local_types_[a.place.local] == LocalType::Operand) {
                    assert(a.place.projections.empty());
                    local_values_[a.place.local] = val;
                } else {
                    llvm::Value* ptr = generate_place_ptr(a.place);
                    builder_.CreateStore(val, ptr);
                }
            },
            [&](const refanal::ir::Statement::Nop&) {}
        );
    }

    void generate_terminator(const refanal::ir::Terminator& term) {
        term.data.visit(
            [&](const refanal::ir::Terminator::Jump& j) {
                builder_.CreateBr(blocks_[j.target]);
            },
            [&](const refanal::ir::Terminator::Branch& b) {
                builder_.CreateCondBr(
                    get_operand_value(b.condition),
                    blocks_[b.true_target],
                    blocks_[b.false_target]
                );
            },
            [&](const refanal::ir::Terminator::Return& r) {
                if (r.value) {
                    builder_.CreateRet(get_operand_value(*r.value));
                } else {
                    builder_.CreateRetVoid();
                }
            },
            [&](const refanal::ir::Terminator::Unreachable&) {
                builder_.CreateUnreachable();
            }
        );
    }

    llvm::Value* generate_rvalue(
        const refanal::ir::RValue& rv, types::SpecType type
    ) {
        return rv.data.visit(
            [&](const refanal::ir::RValue::Use& u) -> llvm::Value* {
                return get_operand_value(u.operand);
            },
            [&](const refanal::ir::RValue::Unary& u) -> llvm::Value* {
                llvm::Value* v = get_operand_value(u.operand);
                switch (u.op) {
                    case refanal::ir::UnaryOp::Neg:
                        return v->getType()->isFloatingPointTy()
                                   ? builder_.CreateFNeg(v)
                                   : builder_.CreateNeg(v);
                    case refanal::ir::UnaryOp::Not:
                    case refanal::ir::UnaryOp::BitNot:
                        return builder_.CreateNot(v);
                }
                return nullptr;
            },
            [&](const refanal::ir::RValue::Binary& b) -> llvm::Value* {
                llvm::Value* l = get_operand_value(b.left);
                llvm::Value* r = get_operand_value(b.right);
                bool f = l->getType()->isFloatingPointTy();
                bool is_signed = true;
                if (auto* i = get_type(get_operand_type(b.left).type)
                                  .data.get_if<types::Type::Int>()) {
                    is_signed = i->is_signed;
                }

                switch (b.op) {
                    case refanal::ir::BinaryOp::Add:
                        return f ? builder_.CreateFAdd(l, r)
                                 : builder_.CreateAdd(l, r);
                    case refanal::ir::BinaryOp::Sub:
                        return f ? builder_.CreateFSub(l, r)
                                 : builder_.CreateSub(l, r);
                    case refanal::ir::BinaryOp::Mul:
                        return f ? builder_.CreateFMul(l, r)
                                 : builder_.CreateMul(l, r);
                    case refanal::ir::BinaryOp::Div:
                        if (f) return builder_.CreateFDiv(l, r);
                        return is_signed ? builder_.CreateSDiv(l, r)
                                         : builder_.CreateUDiv(l, r);
                    case refanal::ir::BinaryOp::Mod:
                        if (f) return builder_.CreateFRem(l, r);
                        return is_signed ? builder_.CreateSRem(l, r)
                                         : builder_.CreateURem(l, r);
                    case refanal::ir::BinaryOp::LShift:
                        return builder_.CreateShl(l, r);
                    case refanal::ir::BinaryOp::RShift:
                        return is_signed ? builder_.CreateAShr(l, r)
                                         : builder_.CreateLShr(l, r);
                    case refanal::ir::BinaryOp::BitAnd:
                        return builder_.CreateAnd(l, r);
                    case refanal::ir::BinaryOp::BitOr:
                        return builder_.CreateOr(l, r);
                    case refanal::ir::BinaryOp::BitXor:
                        return builder_.CreateXor(l, r);
                }
                return nullptr;
            },
            [&](const refanal::ir::RValue::Comparison& c) -> llvm::Value* {
                llvm::Value* l = get_operand_value(c.left);
                llvm::Value* r = get_operand_value(c.right);
                bool f = l->getType()->isFloatingPointTy();
                bool is_signed = true;
                if (auto* i = get_type(get_operand_type(c.left).type)
                                  .data.get_if<types::Type::Int>()) {
                    is_signed = i->is_signed;
                }

                if (f) {
                    switch (c.op) {
                        case refanal::ir::ComparisonOp::Less:
                            return builder_.CreateFCmpOLT(l, r);
                        case refanal::ir::ComparisonOp::Greater:
                            return builder_.CreateFCmpOGT(l, r);
                        case refanal::ir::ComparisonOp::LessEqual:
                            return builder_.CreateFCmpOLE(l, r);
                        case refanal::ir::ComparisonOp::GreaterEqual:
                            return builder_.CreateFCmpOGE(l, r);
                        case refanal::ir::ComparisonOp::Equal:
                            return builder_.CreateFCmpOEQ(l, r);
                        case refanal::ir::ComparisonOp::NotEqual:
                            return builder_.CreateFCmpONE(l, r);
                    }
                } else {
                    switch (c.op) {
                        case refanal::ir::ComparisonOp::Less:
                            return is_signed ? builder_.CreateICmpSLT(l, r)
                                             : builder_.CreateICmpULT(l, r);
                        case refanal::ir::ComparisonOp::Greater:
                            return is_signed ? builder_.CreateICmpSGT(l, r)
                                             : builder_.CreateICmpUGT(l, r);
                        case refanal::ir::ComparisonOp::LessEqual:
                            return is_signed ? builder_.CreateICmpSLE(l, r)
                                             : builder_.CreateICmpULE(l, r);
                        case refanal::ir::ComparisonOp::GreaterEqual:
                            return is_signed ? builder_.CreateICmpSGE(l, r)
                                             : builder_.CreateICmpUGE(l, r);
                        case refanal::ir::ComparisonOp::Equal:
                            return builder_.CreateICmpEQ(l, r);
                        case refanal::ir::ComparisonOp::NotEqual:
                            return builder_.CreateICmpNE(l, r);
                    }
                }
                return nullptr;
            },
            [&](const refanal::ir::RValue::Call& c) -> llvm::Value* {
                llvm::Value* callee = get_operand_value(c.callee);
                const auto& t = get_type(get_operand_type(c.callee).type);
                const types::Type::Func* func_type_info = nullptr;

                if (auto* f = t.data.get_if<types::Type::Func>()) {
                    func_type_info = f;
                } else if (auto* p = t.data.get_if<types::Type::Ptr>()) {
                    func_type_info = generator->ir_module_->types()
                                         .get(p->type.type)
                                         .data.get_if<types::Type::Func>();
                }
                if (!func_type_info) return nullptr;

                auto s_args = ir_func->operands(c.args);
                std::vector<llvm::Value*> args;
                args.reserve(s_args.size());
                for (auto s_arg : s_args) {
                    args.push_back(get_operand_value(s_arg));
                }

                std::vector<llvm::Type*> param_types;
                param_types.reserve(func_type_info->params.size());
                for (const auto& p : func_type_info->params) {
                    param_types.push_back(get_rep_type(p.type));
                }
                llvm::FunctionType* llvm_func_type = llvm::FunctionType::get(
                    get_rep_type(func_type_info->return_type),
                    param_types,
                    false
                );

                return builder_.CreateCall(llvm_func_type, callee, args);
            },
            [&](const refanal::ir::RValue::AddressOf& a) -> llvm::Value* {
                return generate_place_ptr(a.place);
            },
            [&](const refanal::ir::RValue::Cast& c) -> llvm::Value* {
                auto op_type = get_operand_type(c.operand);
                const auto& src_type = get_type(op_type.type);
                const auto& dst_type = get_type(type.type);

                if (src_type.data.is<types::Type::Array>() &&
                    dst_type.data.is<types::Type::Ptr>()) {
                    return get_operand_value(c.operand);
                }

                llvm::Value* val = get_operand_value(c.operand);
                llvm::Type* src_llvm_type = val->getType();
                llvm::Type* dst_llvm_type = get_rep_type(type);

                if (src_llvm_type == dst_llvm_type) return val;

                auto* src_int = src_type.data.get_if<types::Type::Int>();
                auto* dst_int = dst_type.data.get_if<types::Type::Int>();
                auto* src_float = src_type.data.get_if<types::Type::Float>();
                auto* dst_float = dst_type.data.get_if<types::Type::Float>();

                if (dst_type.data.is<types::Type::Bool>()) {
                    if (src_llvm_type->isIntegerTy()) {
                        return builder_.CreateICmpNE(
                            val, llvm::ConstantInt::get(src_llvm_type, 0)
                        );
                    }
                    if (src_llvm_type->isFloatingPointTy()) {
                        return builder_.CreateFCmpUNE(
                            val, llvm::ConstantFP::get(src_llvm_type, 0.0)
                        );
                    }
                    if (src_llvm_type->isPointerTy()) {
                        return builder_.CreateIsNotNull(val);
                    }
                }

                if (src_int && dst_int) {
                    return builder_.CreateIntCast(
                        val, dst_llvm_type, src_int->is_signed
                    );
                }
                if (src_int && dst_float) {
                    if (src_int->is_signed) {
                        return builder_.CreateSIToFP(val, dst_llvm_type);
                    } else {
                        return builder_.CreateUIToFP(val, dst_llvm_type);
                    }
                }
                if (src_float && dst_int) {
                    if (dst_int->is_signed) {
                        return builder_.CreateFPToSI(val, dst_llvm_type);
                    } else {
                        return builder_.CreateFPToUI(val, dst_llvm_type);
                    }
                }
                if (src_float && dst_float) {
                    return builder_.CreateFPCast(val, dst_llvm_type);
                }
                if (src_llvm_type->isPointerTy() &&
                    dst_llvm_type->isIntegerTy()) {
                    return builder_.CreatePtrToInt(val, dst_llvm_type);
                }
                if (src_llvm_type->isIntegerTy() &&
                    dst_llvm_type->isPointerTy()) {
                    return builder_.CreateIntToPtr(val, dst_llvm_type);
                }
                if (src_llvm_type->isPointerTy() &&
                    dst_llvm_type->isPointerTy()) {
                    return builder_.CreatePointerCast(val, dst_llvm_type);
                }
                return builder_.CreateBitCast(val, dst_llvm_type);
            },
            [&](const refanal::ir::RValue::CreateStruct& cs) -> llvm::Value* {
                llvm::Type* st = get_base_type(cs.type);
                llvm::Value* sv = llvm::UndefValue::get(st);
                auto s_args = ir_func->operands(cs.args);
                for (uint32_t i = 0; i < s_args.size(); ++i) {
                    sv = builder_.CreateInsertValue(
                        sv, get_operand_value(s_args[i]), {i}
                    );
                }
                return sv;
            },
            [&](const refanal::ir::RValue::Array& a) -> llvm::Value* {
                auto s_items = ir_func->operands(a.items);
                if (s_items.empty()) return nullptr;
                llvm::Type* item_type =
                    get_operand_value(s_items[0])->getType();
                llvm::Type* at =
                    llvm::ArrayType::get(item_type, s_items.size());
                llvm::Value* av = llvm::UndefValue::get(at);
                for (uint32_t i = 0; i < s_items.size(); ++i) {
                    av = builder_.CreateInsertValue(
                        av, get_operand_value(s_items[i]), {i}
                    );
                }
                return av;
            }
        );
    }

    llvm::Value* generate_place_ptr(const refanal::ir::Place& place) {
        llvm::Value* ptr = local_values_[place.local];
        auto projections = ir_func->projections(place.projections);
        types::SpecType current_type = ir_func->local(place.local).type;
        for (auto proj : projections) {
            if (is_ref(current_type)) {
                ptr = builder_.CreateLoad(get_rep_type(current_type), ptr);
            }
            proj.data.visit(
                [&](refanal::ir::Projection::Index i) {
                    auto index = get_operand_value(i.index);
                    if (auto ptr_type = get_type(current_type.type)
                                            .data.get_if<types::Type::Ptr>()) {
                        // ptr = builder_.CreateLoad(builder_.getPtrTy(), ptr);
                        ptr = builder_.CreateGEP(
                            get_rep_type(ptr_type->type), ptr, index
                        );
                        current_type = ptr_type->type;
                    } else {
                        const auto& arr_type =
                            get_type(current_type.type)
                                .data.get<types::Type::Array>();
                        ptr = builder_.CreateInBoundsGEP(
                            get_base_type(current_type.type),
                            ptr,
                            {builder_.getInt32(0), index}
                        );
                        current_type = arr_type.item;
                    }
                },
                [&](refanal::ir::Projection::Field f) {
                    const auto& struct_type =
                        get_type(current_type.type)
                            .data.get<types::Type::Struct>();
                    ptr = builder_.CreateStructGEP(
                        get_base_type(current_type.type), ptr, f.field
                    );
                    current_type = struct_type.fields[f.field].type;
                },
                [&](refanal::ir::Projection::Deref) {
                    const auto& ptr_type = get_type(current_type.type)
                                               .data.get<types::Type::Ptr>();
                    if (ptr_type.type.specifier == types::Specifier::Val) {
                        current_type = {
                            .type = ptr_type.type.type,
                            .specifier = types::Specifier::Var
                        };
                    } else {
                        ptr = builder_.CreateLoad(
                            get_rep_type(ptr_type.type), ptr
                        );
                        current_type = ptr_type.type;
                    }
                }
            );
        }
        return ptr;
    }

    llvm::Value* get_operand_value(refanal::ir::OperandRef ref) {
        const auto& op = ir_func->operand(ref);
        return op.data.visit(
            [&](const refanal::ir::Const& c) -> llvm::Value* {
                auto value = generate_const(c);
                auto type = generator->ir_module_->const_type(c);
                if (is_ref({.type = type.type, .specifier = op.specifier})) {
                    if (c.value.is<std::string_view>()) return value;
                    auto temp = builder_.CreateAlloca(get_rep_type(type));
                    builder_.CreateStore(value, temp);
                    return temp;
                }
                if (c.value.is<std::string_view>()) {
                    auto rep_type = get_rep_type(type);
                    auto temp = builder_.CreateAlloca(rep_type);
                    builder_.CreateStore(
                        builder_.CreateLoad(rep_type, value), temp
                    );
                    return temp;
                }
                return value;
            },
            [&](const refanal::ir::Place& p) -> llvm::Value* {
                if (local_types_[p.local] == LocalType::Operand) {
                    assert(p.projections.empty());
                    auto type = ir_func->local(p.local).type;
                    types::SpecType load_type = {
                        .type = type.type, .specifier = op.specifier
                    };
                    if (is_ref(load_type) == is_ref(type)) {
                        return local_values_[p.local];
                    }
                    assert(is_ref(type) && !is_ref(load_type));
                    return builder_.CreateLoad(
                        get_rep_type(load_type), local_values_[p.local]
                    );
                }
                llvm::Value* ptr = generate_place_ptr(p);
                auto type =
                    ir_func->place_type(p, generator->ir_module_->types());
                types::SpecType load_type = {
                    .type = type.type, .specifier = op.specifier
                };
                bool load_type_is_ref = is_ref(load_type);
                bool place_type_is_ref = is_ref(type);
                if (load_type_is_ref == place_type_is_ref) {
                    return builder_.CreateLoad(get_rep_type(type), ptr);
                }
                if (load_type_is_ref) {
                    return ptr;
                }
                if (place_type_is_ref) {
                    return builder_.CreateLoad(
                        get_rep_type(load_type),
                        builder_.CreateLoad(get_rep_type(type), ptr)
                    );
                }
                throw std::runtime_error("internal error");
            }
        );
    }

    types::SpecType get_operand_type(refanal::ir::OperandRef ref) {
        return ir_func->operand(ref).data.visit(
            [&](refanal::ir::Place p) {
                return ir_func->place_type(p, generator->ir_module_->types());
            },
            [&](const refanal::ir::Const& c) {
                return generator->ir_module_->const_type(c);
            }
        );
    }

    llvm::Value* generate_const(const refanal::ir::Const& c) {
        return c.value.visit(
            [&](bool b) -> llvm::Value* { return builder_.getInt1(b); },
            [&](std::int64_t i) -> llvm::Value* {
                return builder_.getInt64(i);
            },
            [&](double d) -> llvm::Value* {
                return llvm::ConstantFP::get(builder_.getDoubleTy(), d);
            },
            [&](char32_t ch) -> llvm::Value* { return builder_.getInt32(ch); },
            [&](std::string_view s) -> llvm::Value* {
                return builder_.CreateGlobalString(s);
            },
            [&](refanal::ir::FuncRef f) -> llvm::Value* {
                return generator->functions_[f];
            },
            [&](refanal::ir::UsedFuncRef f) -> llvm::Value* {
                return generator->used_funcs_[f];
            }
        );
    }

    llvm::Type* get_base_type(types::TypeId type_id) {
        return generator->get_base_type(type_id);
    }

    llvm::Type* get_rep_type(types::SpecType type) {
        return generator->get_rep_type(type);
    }

    const types::Type& get_type(types::TypeId id) {
        return generator->ir_module_->types().get(id);
    }

    bool is_ref(types::SpecType type) { return generator->is_ref(type); }

    Generator* generator;
    const refanal::ir::Func* ir_func;
    llvm::Function* llvm_func;
    llvm::IRBuilder<> builder_;
    IndexVector<llvm::Value*, refanal::ir::LocalRef> local_values_;
    IndexVector<llvm::BasicBlock*, refanal::ir::BlockRef> blocks_;
    enum class LocalType : std::uint8_t { Unknown, Operand, Alloca };
    IndexVector<LocalType, refanal::ir::LocalRef> local_types_;
};

void Generator::generate_func(
    const refanal::ir::Func& ir_func, llvm::Function* llvm_func
) {
    FuncGenerator generator(*this, ir_func, *llvm_func);
    generator.generate();
}
}  // namespace

std::unique_ptr<llvm::Module> generate(
    llvm::LLVMContext& context,
    const refanal::ir::Module& module,
    const Project& project,
    std::optional<llvm::DataLayout> layout
) {
    if (layout) {
        Generator generator(context, module, project, *layout);
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

    Generator generator(context, module, project, dl);
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

}  // namespace acu::codegen
