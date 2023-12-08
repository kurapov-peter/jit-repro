
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/RuntimeDyld.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>

#include <fstream>
#include <sstream>

using namespace llvm;

std::unique_ptr<Module> readIRFromFile(LLVMContext &ctx,
                                       std::string filename = "main.ll")
{
    SMDiagnostic error;
    auto m = parseIRFile(filename, error, ctx);
    if (!m)
    {
        llvm::errs() << "Could not read from mail.ll (" << error.getMessage()
                     << ")\n";
    }
    return m;
}

int main(int argc, char *argv[])
{
    auto target_err = InitializeNativeTarget();
    if (!target_err)
    {
        errs() << "Could not initialize native target!\n";
    }
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    auto EF = cantFail(orc::SelfExecutorProcessControl::Create());
    std::unique_ptr<orc::ExecutionSession> ES =
        std::make_unique<orc::ExecutionSession>(std::move(EF));

    auto target_machine_builder_check =
        orc::JITTargetMachineBuilder::detectHost();
    if (!target_machine_builder_check)
    {
        errs() << "Could not create JITTargetMachineBuilder!\n";
        exit(1);
    }
    orc::JITTargetMachineBuilder target_machine_builder =
        std::move(*target_machine_builder_check);

    auto dl_check = target_machine_builder.getDefaultDataLayoutForTarget();
    if (!dl_check)
    {
        errs() << "Could not init Data Layout!\n";
        exit(1);
    }
    std::unique_ptr<DataLayout> dl =
        std::make_unique<DataLayout>(std::move(*dl_check));

    std::unique_ptr<orc::MangleAndInterner> mangle =
        std::make_unique<orc::MangleAndInterner>(*ES, *dl);

    std::unique_ptr<orc::RTDyldObjectLinkingLayer> object_layer =
        std::make_unique<orc::RTDyldObjectLinkingLayer>(
            *ES, []()
            { return std::make_unique<SectionMemoryManager>(); });
    std::unique_ptr<orc::IRCompileLayer> compiler_layer =
        std::make_unique<orc::IRCompileLayer>(
            *ES, *object_layer,
            std::make_unique<orc::ConcurrentIRCompiler>(
                std::move(target_machine_builder)));

    auto dylib_check = ES->createJITDylib("<main>");
    if (!dylib_check)
    {
        errs() << "Could not create JIT Dylib!\n";
        exit(1);
    }
    orc::JITDylib *dylib = &(*dylib_check);
    dylib->addGenerator(
        cantFail(orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            dl->getGlobalPrefix())));

    std::unique_ptr<LLVMContext> ctx = std::make_unique<LLVMContext>();
    auto ir = readIRFromFile(*ctx, "main.ll");
    ir->print(llvm::errs(), nullptr);

    ir->setDataLayout(*dl);
    orc::ThreadSafeContext tsctx(std::move(ctx));
    orc::ThreadSafeModule tsm(std::move(ir), tsctx);
    auto err = compiler_layer->add(*dylib, std::move(tsm));

    auto entry_point = ES->lookup({dylib}, (*mangle)("vadd_entry"));
    if (!entry_point)
    {
        errs() << "Failed to find the entry point!\n";
        exit(1);
    }
    auto *callable = jitTargetAddressToPointer<void (*)(
        float *, float *, float *, int64_t, void *, int64_t)>(
        entry_point->getAddress());

    constexpr size_t arr_size = 4;
    float A[arr_size] = {0, 1, 2, 3};
    float B[arr_size] = {0, 1, 2, 3};
    float C[arr_size] = {0, 0, 0, 0};
    int Size = arr_size;

    std::string spv;

    errs() << "Getting spirv from the input...\n";
    if (argc != 2)
    {
        errs() << "Please provide spirv binary input as the first argument.\n";
        exit(1);
    }
    std::ifstream spv_file(argv[1]);
    std::stringstream ss;
    ss << spv_file.rdbuf();
    spv = ss.str();
    void *data = spv.data();
    size_t data_size = spv.size();

    callable(A, B, C, Size, data, data_size);

    return 0;
}