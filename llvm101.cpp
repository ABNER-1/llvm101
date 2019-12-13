#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>
#include <random>
#include <iostream>

using namespace llvm;

using std::vector;


vector<int>
generate_vec(int size) {
    static std::default_random_engine e(67);
    vector<int> vec(size);
    for (int i = 0; i < size; ++i) {
        vec[i] = e() % 10000 + 1;
    }
    return vec;
}

class LLVM_Environment {
 public:
    LLVM_Environment() {
        InitializeNativeTarget();
        LLVMInitializeNativeAsmPrinter();
        LLVMInitializeNativeAsmParser();
    }
    ~LLVM_Environment() {
        // free all resources
        llvm_shutdown();
    }
};

enum class Op { Plus, Minus, Multiply, Divide, Modular };

using func_t = void (*)(int size, const int* va, const int* vb, const int* vc, int* vd);

class Engine {
 public:
    Engine()
        : Owner(make_unique<Module>("llvm101", ctx)), mod(Owner.get()), builder(ctx) {}
    func_t get_function(Op op1, Op op2);

 private:
    LLVMContext ctx;
    std::unique_ptr<Module> Owner;
    Module* mod;
    IRBuilder<> builder;
    std::unique_ptr<ExecutionEngine> EE;
};


llvm::Value*
do_operator(IRBuilder<> &builder, Op op, llvm::Value* l, llvm::Value * r) {

    switch (op){
        case Op::Plus:
            return builder.CreateAdd(l, r);
        case Op::Divide:
            return builder.CreateSDiv(l, r);
        case Op::Multiply:
            return builder.CreateMul(l, r);
        case Op::Minus :
            return builder.CreateSub(l, r);
        case Op::Modular :
            return builder.CreateSRem(l, r);
        default:
            return nullptr;
    }

}

// excise now!
func_t
Engine::get_function(Op op1, Op op2) {
    static int counter = 0;
    auto func_name = "func_" + std::to_string(counter++);
    
    FunctionType* func_type = FunctionType::get(
        Type::getVoidTy(ctx),
        {Type::getInt32Ty(ctx), Type::getInt32PtrTy(ctx), Type::getInt32PtrTy(ctx), Type::getInt32PtrTy(ctx), Type::getInt32PtrTy(ctx)},
        false
        );


    Function* foo = [&] () {

        Function* function = Function::Create(func_type, Function::ExternalLinkage, func_name, mod);
        BasicBlock * init_b = BasicBlock::Create(ctx, "InitBlock", function);
        BasicBlock * if_b = BasicBlock::Create(ctx, "IfBranch", function);
        BasicBlock * then_b = BasicBlock::Create(ctx, "ThenBranch", function);
        BasicBlock * post_b = BasicBlock::Create(ctx, "PostBranch", function);

        builder.SetInsertPoint(init_b);

        // get all input parameters
        auto iter = function->arg_begin();
        Argument * size = iter++;
        size->setName("size");

        Argument *input_a = iter++;
        input_a->setName("a");
        Argument *input_b = iter++;
        input_b->setName("b");
        Argument *input_c = iter++;
        input_c->setName("c");
        Argument *input_d = iter++;
        input_d->setName("d");

        assert(iter == function->arg_end());

        // init i
        auto i = builder.CreateAlloca(Type::getInt32Ty(ctx), nullptr, "i");
        builder.CreateStore(builder.getInt32(0), i);
        builder.CreateBr(if_b);

        // condition branch
        builder.SetInsertPoint(if_b);
        auto condition_i = builder.CreateLoad(i, "condition_i");
        auto if_cond = builder.CreateICmpSLT(condition_i, size, "i_slt_size");
        builder.CreateCondBr(if_cond, then_b, post_b);

        // then branch
        builder.SetInsertPoint(then_b);
        auto body_i = builder.CreateLoad(i, "body_i");
        auto tmp_a_ptr = builder.CreateGEP(input_a, body_i, "tmp_a_ptr");
        auto tmp_b_ptr = builder.CreateGEP(input_b, body_i, "tmp_b_ptr");
        auto tmp_c_ptr = builder.CreateGEP(input_c, body_i, "tmp_c_ptr");
        auto tmp_d_ptr = builder.CreateGEP(input_d, body_i, "tmp_d_ptr");

        auto tmp_a = builder.CreateLoad(tmp_a_ptr, "tmp_a");
        auto tmp_b = builder.CreateLoad(tmp_b_ptr, "tmp_b");
        auto tmp_c = builder.CreateLoad(tmp_c_ptr, "tmp_c");

        auto a_op1_b = do_operator(builder, op1, tmp_a,tmp_b);
        a_op1_b->setName("a_op1_b");
        auto res =do_operator(builder, op2, a_op1_b, tmp_c);
        res->setName("tmp_result");
        builder.CreateStore(res, tmp_d_ptr);

        builder.CreateStore(builder.CreateAdd(body_i, builder.getInt32(1), "next_i"), i);
        builder.CreateBr(if_b);

        // post branch
        builder.SetInsertPoint(post_b);
        builder.CreateRetVoid();

        return function;
    }();


    EE = std::unique_ptr<ExecutionEngine>(EngineBuilder(std::move(Owner)).create());
    outs() << *mod;
    auto fptr_ = EE->getFunctionAddress(func_name);
    return reinterpret_cast<func_t>(fptr_);
}

int
main() {
    LLVM_Environment llvm_environment;
    int size = 10000000;
    vector<int> vec_a = generate_vec(size);
    vector<int> vec_b = generate_vec(size);
    vector<int> vec_c = generate_vec(size);
    Engine eng;
    func_t func_multiply_plus = eng.get_function(Op::Multiply, Op::Plus);

    vector<int> vec_d(size);
    func_multiply_plus(size, vec_a.data(), vec_b.data(), vec_c.data(), vec_d.data());

    for (int i = 0; i < size; ++i) {
        auto ans = vec_d[i];
        auto ref = vec_a[i] * vec_b[i] + vec_c[i];
        if (ref != ans) {
            std::cout << "error at i=" << i << " ans=" << ans << " ref=" << ref 
             << " a=" << vec_a[i]
             << " b=" << vec_b[i]
             << " c=" << vec_c[i]
             << std::endl;
            exit(-1);
        }
    }
    std::cout << "all is ok" << std::endl;
    return 0;
}
