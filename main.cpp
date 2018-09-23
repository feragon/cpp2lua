#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Frontend/FrontendActions.h>
#include <iostream>

int main(int argc, const char** argv) {
    llvm::cl::OptionCategory category("cpp2lua");
    clang::tooling::CommonOptionsParser op(argc, argv, category);
    clang::tooling::ClangTool tool(op.getCompilations(), op.getSourcePathList());
    return tool.run(clang::tooling::newFrontendActionFactory<clang::SyntaxOnlyAction>().get());
}