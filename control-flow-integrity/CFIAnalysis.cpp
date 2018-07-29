#include "CFIAnalysis.h"

using namespace cfi;
using namespace llvm;

char ControlFlowIntegrityPass::ID = 0;
bool ControlFlowIntegrityPass::PostPatchingRequired = false;
graph::Graph ControlFlowIntegrityPass::graph = {};

bool ControlFlowIntegrityPass::runOnModule(Module &M) {
  auto *prev = getAnalysisIfAvailable<ControlFlowIntegrityPass>();
  if (prev) {
    dbgs() << "Analysis is available!\n";
    this->graph = prev->graph;
  } else {
    dbgs() << "Analysis NOT available!\n";
    //this->graph = {};
  }

  for (auto &F : M) {
    auto undoValues = this->applyCFI(F);
    addProtection(composition::Manifest("cfi", &F, [](const composition::Manifest &m) {}, {}, false, undoValues));
  }
  return true;
}

std::set<llvm::Value *> ControlFlowIntegrityPass::applyCFI(Function &F) {
  std::set<llvm::Value *> undoValues{};
  dbgs() << "CFI Callback called\n";
  std::string funcName = F.getName().str();
  dbgs() << "Running on: " << funcName << ".\n";
  bool first_instr = true;
  for (auto &BB : F) {
    for (auto &I: BB) {
      if (first_instr) {
        LLVMContext &Ctx = F.getContext();

        FunctionType *registerType = TypeBuilder<void(char *), false>::get(Ctx);
        auto registerFunction = cast<Function>(F.getParent()->getOrInsertFunction("registerFunction", registerType));

        IRBuilder<> builder(&I);
        builder.SetInsertPoint(&BB, builder.GetInsertPoint());

        Value *strPtr = builder.CreateGlobalStringPtr(funcName);
        undoValues.insert(builder.CreateCall(registerFunction, strPtr));
        first_instr = false;

        // Function is in the sensitive list
        if (F.hasFnAttribute(CONTROL_FLOW_INTEGRITY)) {
          FunctionType *verifyType = TypeBuilder<void(), false>::get(Ctx);
          auto verifyFunction = cast<Function>(F.getParent()->getOrInsertFunction("verifyStack", verifyType));

          // Insert call
          builder.SetInsertPoint(&BB, builder.GetInsertPoint());
          undoValues.insert(builder.CreateCall(verifyFunction));
        }
      }
      if (auto *callInstruction = dyn_cast<CallInst>(&I)) {
        Function *called = callInstruction->getCalledFunction();
        if (called) {
          std::string calledName = called->getName().str();
          graph::Vertex calledVertex;
          if (called->hasFnAttribute(CONTROL_FLOW_INTEGRITY)) {
            calledVertex = graph::Vertex(calledName, true);
          } else {
            calledVertex = graph::Vertex(calledName);
          }
          auto funcVertex = graph::Vertex(funcName);
          graph.addEdge(funcVertex, calledVertex);
        }
      }
      if (isa<ReturnInst>(&I)) {
        LLVMContext &Ctx = F.getContext();

        FunctionType *registerType = TypeBuilder<void(char *), false>::get(Ctx);
        auto
            deregisterFunction = cast<Function>(F.getParent()->getOrInsertFunction("deregisterFunction", registerType));

        IRBuilder<> builder(&I);
        builder.SetInsertPoint(&BB, builder.GetInsertPoint());

        // Insert a call to our function.
        Value *strPtr = builder.CreateGlobalStringPtr(funcName);
        undoValues.insert(builder.CreateCall(deregisterFunction, strPtr));
      }
    }
  }
  return undoValues;
}

void ControlFlowIntegrityPass::getAnalysisUsage(AnalysisUsage &usage) const {
  usage.setPreservesAll();
}

static RegisterPass<ControlFlowIntegrityPass>
    X("control-flow-integrity-analysis", "Control Flow Integrity Analysis Pass");