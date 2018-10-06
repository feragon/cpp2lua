#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Frontend/FrontendActions.h>
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <iostream>
#include <sstream>

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

class ClassParser : public MatchFinder::MatchCallback {
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

std::vector<std::string> toRemoveParameters = {
        "class ", "std::"
};

void printParameters(const clang::CXXMethodDecl* method) {
    auto parameters = method->parameters();
    auto parameter = parameters.begin();
    while(parameter != parameters.end()) {
        std::string type = (*parameter)->getType().getAsString();

        for(const auto& toRemoveParameter : toRemoveParameters) {
            size_t pos;
            if((pos = type.find(toRemoveParameter, 0)) != std::string::npos) {
                type.erase(pos, toRemoveParameter.size());
            }
        }

        std::cout << type;

        parameter++;
        if(parameter != parameters.end()) {
            std::cout << ", ";
        }
    }
}

void printMethods(const clang::CXXRecordDecl* c) {
    std::vector<CXXMethodDecl*> constructors;
    std::map<std::string, std::vector<CXXMethodDecl*>> methods;

    for(CXXMethodDecl* method : c->methods()) {
        //Private or protected methods
        if(method->getAccess() != AS_public) {
            continue;
        }

        //Operator
        if(method->isOverloadedOperator()) {
            continue;
        }

        //Destructor
        if(method->getNameAsString()[0] == '~') {
            continue;
        }

        //Constructor
        if(method->getNameAsString() == c->getNameAsString()) {
            constructors.push_back(method);
            continue;
        }

        methods[method->getNameAsString()].push_back(method);
    }

    if(!constructors.empty()) {
        std::cout << "    .setConstructors<";

        auto it = constructors.begin();

        while(it != constructors.end()) {
            std::cout << (*it)->getNameAsString() << "(";

            printParameters(*it);


            std::cout << ")";
            it++;

            if(it != constructors.end()) {
                std::cout << ", ";
            }
        }

        std::cout << ">()" << std::endl;
    }

    for(auto method : methods) {
        if(method.second.size() > 1) {
            std::cout << "//  .addOverloadedFunctions(\"" << method.first << "\")" << std::endl;
        }
        else {
            std::cout << "    .addFunction(\"" << method.first << "\", &" << (*method.second.begin())->getQualifiedNameAsString().substr(5) << ")" << std::endl;
        }
    }
}

int main(int argc, const char** argv) {
    llvm::cl::OptionCategory category("cpp2lua");
    clang::tooling::CommonOptionsParser op(argc, argv, category);
    clang::tooling::ClangTool tool(op.getCompilations(), op.getSourcePathList());


    ClassParser parser;
    MatchFinder finder;
    finder.addMatcher(cxxRecordDecl(isClass()).bind("lcClasses"), &parser);

    tool.run(clang::tooling::newFrontendActionFactory(&finder).get());


    std::cout << "kaguya::State state;" << std::endl;
    for(auto c : discoveredClasses) {
        std::string className = c.first;

        std::string::size_type n = 0;
        while (( n = className.find("::", n )) != std::string::npos) {
            className.replace(n, 2, ".");
            n += 1;
        }

        std::cout << "state[\"" << className << "\"].setClass(kaguya::UserdataMetatable<";

        std::vector<clang::CXXRecordDecl*> bases;

        for(auto base : c.second->bases()) {
            auto cxxRecordBase = base.getType()->getAsCXXRecordDecl();
            if(cxxRecordBase) {
                bases.push_back(cxxRecordBase);
            }
        }

        switch(bases.size()) {
            case 0:
                std::cout << c.first;
                break;

            case 1:
                std::cout << getClassName(*(bases.begin())) << ", " << c.first;
                break;

            default:
                std::cout << "MultipleInheritance, kaguya::MultipleBase<";

                auto it = bases.begin();
                std::cout << getClassName(*it);
                it++;

                while(it != bases.end()) {
                    std::cout << ", " << getClassName(*it);
                    it++;
                }
                std::cout << ">";
                break;
        }
        std::cout << ">()" << std::endl;

        printMethods(c.second);

        std::cout << ");" << std::endl;
        std::cout << std::endl;
    }

    return 0;
}