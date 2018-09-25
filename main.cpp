#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Frontend/FrontendActions.h>
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <iostream>

using namespace clang;
using namespace clang::ast_matchers;

class LoopPrinter : public MatchFinder::MatchCallback {
    public :
        void run(const MatchFinder::MatchResult& result) override {
            if (auto* fs = result.Nodes.getNodeAs<clang::CXXRecordDecl>("lcClasses")) {
                auto ctx = fs->getDeclContext();

                SmallVector<const DeclContext*, 8> contexts;

                bool isLCClass = false;

                while (ctx) {
                    if(ctx->isStdNamespace()) {
                        break;
                    }
                    if (isa<NamedDecl>(ctx))
                        contexts.push_back(ctx);
                    ctx = ctx->getParent();
                }

                for (const DeclContext* DC : llvm::reverse(contexts)) {
                    if (const auto* ND = dyn_cast<NamespaceDecl>(DC)) {
                        if(ND->getNameAsString() == "lc") {
                            isLCClass = true;
                            break;
                        }
                    }
                }

                if(!isLCClass) {
                    return;
                }

                for (const DeclContext *DC : llvm::reverse(contexts)) {
                    if (const auto* ND = dyn_cast<NamespaceDecl>(DC)) {
                        std::cout << ND->getNameAsString() << std::endl;
                    }
                }


                std::cout << "Class: " << fs->getNameAsString() << std::endl;


                if(fs->hasDefinition()) {
                    auto bases = fs->bases();

                    for (auto base : bases) {
                        std::cout << "- Base: " << base.getType().getAsString() << std::endl;
                    }
                }
            }
        }
};

int main(int argc, const char** argv) {
    llvm::cl::OptionCategory category("cpp2lua");
    clang::tooling::CommonOptionsParser op(argc, argv, category);
    clang::tooling::ClangTool tool(op.getCompilations(), op.getSourcePathList());


    LoopPrinter printer;
    MatchFinder finder;
    finder.addMatcher(cxxRecordDecl(isClass()).bind("lcClasses"), &printer);

    return tool.run(clang::tooling::newFrontendActionFactory(&finder).get());
}