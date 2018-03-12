// clang-5.0 -emit-llvm something.c -c something.bc
// mkdir build; cd build
// cmake ..
// make
// opt-5.0 -load control-flow-integrity/libFunctionPass.so -functionpass < ../something.bc > /dev/null
#include <cstdio>
#include <fstream>
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/TypeBuilder.h"
#include "Graph.h"

using namespace llvm;

static cl::opt<std::string> InputSensitiveFcts("i",
                                          cl::desc("Specify filename containing the list of sensitive functions"),
                                          cl::value_desc("filename"));


void readInput(std::vector<std::string> *res) {
    std::ifstream ifs;
    std::string line;
    ifs.open(InputSensitiveFcts.c_str());

    while (std::getline(ifs, line)) {
        res->push_back(line);
    }

    ifs.close();
}

namespace {
    struct OurFunctionPass : public FunctionPass {
        static char ID;
        Graph graph;
        std::vector<std::string> sensitiveList;

        OurFunctionPass() : FunctionPass(ID) {}

        bool doInitialization(Module &M) override {
            graph = Graph();

            errs() << "Input: " << InputSensitiveFcts << "\n";
            readInput(&sensitiveList);
            for (auto iter = sensitiveList.begin(); iter < sensitiveList.end(); iter++) {
                errs() << "Sensitive: '" << *iter << "'\n";
            }
            errs() << "\n";
            return false;
        }

        bool doFinalization(Module &M) override {
            graph.writeGraphFile();
            return false;
        }

        bool runOnFunction(Function &function) override {
            std::string funcName = function.getName().str();
            Vertex funcVertex = Vertex(funcName);
            bool first_instr = true;
            bool modified = false; // runOnFunction return value
            for (BasicBlock &block : function) {
                for (Instruction &instruction: block) {
                    if (first_instr) {
                        LLVMContext &Ctx = function.getContext();

                        FunctionType *registerType = TypeBuilder<void(char *), false>::get(Ctx);
                        Function *registerFunction = cast<Function>(function.getParent()->
                                getOrInsertFunction("registerFunction", registerType));

                        IRBuilder<> builder(&instruction);
                        builder.SetInsertPoint(&block, builder.GetInsertPoint());

                        Value *strPtr = builder.CreateGlobalStringPtr(funcName.c_str());
                        builder.CreateCall(registerFunction, strPtr);
                        modified = true;
                        first_instr = false;

                        // Function is in the sensitive list
                        if (find(sensitiveList.begin(), sensitiveList.end(), funcName) != sensitiveList.end()) {
                            FunctionType *verifyType = TypeBuilder<void(), false>::get(Ctx);
                            Function *verifyFunction = cast<Function>(function.getParent()->
                                    getOrInsertFunction("verifyStack", verifyType));

                            // Insert call
                            builder.SetInsertPoint(&block, builder.GetInsertPoint());
                            builder.CreateCall(verifyFunction);
                        }
                    }
                    if (auto *callInstruction = dyn_cast<CallInst>(&instruction)) {
                        Function *called = callInstruction->getCalledFunction();
                        if (called) {
                            std::string calledName = called->getName().str();
                            Vertex calledVertex;
                            if (find(sensitiveList.begin(), sensitiveList.end(), calledName) != sensitiveList.end()) {
                                calledVertex = Vertex(calledName, true);
                            } else {
                                calledVertex = Vertex(calledName);
                            }
                            graph.addEdge(funcVertex, calledVertex);
                        }
                    }
                    if (auto *callInstruction = dyn_cast<ReturnInst>(&instruction)) {
                        LLVMContext &Ctx = function.getContext();

                        FunctionType *registerType = TypeBuilder<void(char *), false>::get(Ctx);
                        Function *deregisterFunction = cast<Function>(
                                function.getParent()->getOrInsertFunction("deregisterFunction", registerType)
                        );

                        IRBuilder<> builder(&instruction);
                        builder.SetInsertPoint(&block, builder.GetInsertPoint());

                        // Insert a call to our function.
                        Value *strPtr = builder.CreateGlobalStringPtr(funcName.c_str());
                        builder.CreateCall(deregisterFunction, strPtr);
                        modified = true;
                    }
                }
            }
            return modified;
        }
    };
}
char OurFunctionPass::ID = 0;
static RegisterPass<OurFunctionPass> X("functionpass", "Function Pass", false, false);
