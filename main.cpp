#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Frontend/FrontendActions.h>
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <iostream>
#include <sstream>

using namespace clang;
using namespace clang::ast_matchers;

std::map<std::string, std::string> discoveredClasses;

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

std::vector<std::string> toRemoveParameters = {
        "class "
};

void printParameters(const clang::CXXMethodDecl* method, std::ostream& o) {
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

        if(type == "_Bool") {
            type = "bool";
        }

        o << type;

        parameter++;
        if(parameter != parameters.end()) {
            o << ", ";
        }
    }
}

void printMethods(const clang::CXXRecordDecl* c, std::ostream& o) {
    std::vector<CXXMethodDecl*> constructors;
    std::map<std::string, std::vector<CXXMethodDecl*>> methods;

    for(CXXMethodDecl* method : c->methods()) {
        //Private or protected methods
        if(method->getAccess() != AS_public) {
            continue;
        }

        if(method->isDeleted()) {
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
        auto methodName = method->getNameAsString();
        auto index = methodName.find('<');
        if(index != std::string::npos) {
            methodName = methodName.substr(0, index);
        }
        if(methodName == c->getNameAsString()) {
            if(!method->isImplicit()) {
                constructors.push_back(method);
            }
            continue;
        }

        methods[methodName].push_back(method);
    }

    if(!constructors.empty()) {
        o << "    .setConstructors<";

        auto it = constructors.begin();

        while(it != constructors.end()) {
            o << getClassName(c) << "(";

            printParameters(*it, o);


            o << ")";
            it++;

            if(it != constructors.end()) {
                o << ", ";
            }
        }

        o << ">()" << std::endl;
    }

    for(auto method : methods) {
        if(method.second.size() > 1) {
            o << "    .addOverloadedFunctions(\"" << method.first << "\", ";

            bool first = true;
            for(auto overload : method.second) {
                if(first) {
                    first = false;
                }
                else {
                    o << ", ";
                }

                o << "(" << getClassName(c) << "::*)(";

                printParameters(overload, o);

                o << ") &" << overload->getQualifiedNameAsString();
            }

            o << ")" << std::endl;
        }
        else {
            o << "    .addFunction(\"" << method.first << "\", &" << (*method.second.begin())->getQualifiedNameAsString() << ")" << std::endl;
        }
    }
}

class ClassParser : public MatchFinder::MatchCallback {
    public :
        void run(const MatchFinder::MatchResult& result) override {
            if (auto* fs = result.Nodes.getNodeAs<clang::CXXRecordDecl>("lcClasses")) {
                if(!fs->hasDefinition()) {
                    return;
                }

                if(fs->isEmpty()) {
                    return;
                }

                const std::string className = getClassName(fs);

                if(className.find("lc::") == -1) {
                    return;
                }

                if(discoveredClasses.count(className) > 0) {
                    return;
                }
                std::ostringstream oss;

                std::string luaClassName = className;
                std::string::size_type n = 0;
                while (( n = luaClassName.find("::", n )) != std::string::npos) {
                    luaClassName.replace(n, 2, ".");
                    n += 1;
                }

                if(fs->isTemplated()) {
                    oss << "//TODO: templated" << std::endl;
                }

                oss << "state[\"" << luaClassName << "\"].setClass(kaguya::UserdataMetatable<";

                std::vector<clang::CXXRecordDecl*> bases;

                for(auto base : fs->bases()) {
                    auto cxxRecordBase = base.getType()->getAsCXXRecordDecl();
                    if(cxxRecordBase) {
                        bases.push_back(cxxRecordBase);
                    }
                }

                switch(bases.size()) {
                    case 0:
                        oss << className;
                        break;

                    case 1:
                        oss << getClassName(*(bases.begin())) << ", " << className;
                        break;

                    default:
                        oss << "kaguya::MultipleInheritance, kaguya::MultipleBase<";

                        auto it = bases.begin();
                        oss << getClassName(*it);
                        it++;

                        while(it != bases.end()) {
                            oss << ", " << getClassName(*it);
                            it++;
                        }
                        oss << ">";
                        break;
                }
                oss << ">()" << std::endl;

                printMethods(fs, oss);

                oss << ");" << std::endl;

                discoveredClasses[className] = oss.str();
            }
        }
};

int main(int argc, const char** argv) {
    llvm::cl::OptionCategory category("cpp2lua");
    clang::tooling::CommonOptionsParser op(argc, argv, category);
    clang::tooling::ClangTool tool(op.getCompilations(), op.getSourcePathList());


    ClassParser parser;
    MatchFinder finder;
    finder.addMatcher(cxxRecordDecl(isClass()).bind("lcClasses"), &parser);

    tool.run(clang::tooling::newFrontendActionFactory(&finder).get());

    for(auto pair : discoveredClasses) {
        std::cout << pair.second << std::endl;
    }

    return 0;
}