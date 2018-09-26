#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Frontend/FrontendActions.h>
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <iostream>

using namespace clang;
using namespace clang::ast_matchers;

std::map<std::string, const clang::CXXRecordDecl*> discoveredClasses;

std::string getClassName(const clang::CXXRecordDecl* c) {
    auto ctx = c->getDeclContext();

    SmallVector<const DeclContext*, 8> contexts;

    while (ctx) {
        if(ctx->isStdNamespace()) {
            break;
        }
        if (isa<NamedDecl>(ctx))
            contexts.push_back(ctx);
        ctx = ctx->getParent();
    }

    std::string className;

    for (const DeclContext *DC : llvm::reverse(contexts)) {
        if (const auto* ND = dyn_cast<NamespaceDecl>(DC)) {
            className += ND->getNameAsString() + "::";
        }
    }

    className += c->getNameAsString();

    return className;
}

class LoopPrinter : public MatchFinder::MatchCallback {
    public :
        void run(const MatchFinder::MatchResult& result) override {
            if (auto* fs = result.Nodes.getNodeAs<clang::CXXRecordDecl>("lcClasses")) {
                if(!fs->hasDefinition()) {
                    return;
                }

                std::string className = getClassName(fs);

                if(className.find("lc::") == -1) {
                    return;
                }

                discoveredClasses[className] = fs;
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

    tool.run(clang::tooling::newFrontendActionFactory(&finder).get());

    for(const auto& c : discoveredClasses) {
        std::cout << c.first << std::endl;

        for (auto base : c.second->bases()) {
            if(base.getType()->getAsCXXRecordDecl()) {
                std::cout << "- Base: " << getClassName(base.getType()->getAsCXXRecordDecl()) << std::endl;
            }
        }
    }

    return 0;
}