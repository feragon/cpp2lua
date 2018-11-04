#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Frontend/FrontendActions.h>
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <iostream>
#include <sstream>

using namespace clang;
using namespace clang::ast_matchers;

struct DiscoveredNameSpace {
    std::map<std::string, DiscoveredNameSpace> children;
};

std::set<std::string> discoveredClasses;
std::vector<std::string> classes;
std::map<std::string, DiscoveredNameSpace> discoveredNamespaces;

std::string getClassName(const clang::CXXRecordDecl* c) {
    auto ctx = c->getDeclContext();

    SmallVector<const DeclContext*, 8> contexts;

    while (ctx) {
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
        "class ", "struct "
};

std::string getType(QualType qualType) {
    std::string type = qualType.getAsString();

    for(const auto& toRemoveParameter : toRemoveParameters) {
        size_t pos;
        if((pos = type.find(toRemoveParameter, 0)) != std::string::npos) {
            type.erase(pos, toRemoveParameter.size());
        }
    }

    if(type == "_Bool") {
        type = "bool";
    }
    return type;
}

void printParameters(const clang::CXXMethodDecl* method, std::ostream& o) {
    auto parameters = method->parameters();
    auto parameter = parameters.begin();
    while(parameter != parameters.end()) {
        o << getType((*parameter)->getType());

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
            if(!method->isImplicit() && !c->isAbstract() && !((CXXConstructorDecl*)c)->isMoveConstructor()) {
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

                o << "static_cast<" << getType(overload->getReturnType()) << "(" << getClassName(c) << "::*)(";

                printParameters(overload, o);

                o << ")";

                if(overload->isConst()) {
                    o << " const";
                }

                o << ">(&" << overload->getQualifiedNameAsString() << ")";
            }

            o << ")";
        }
        else {
            o << "    .add";
            if((*method.second.begin())->isStatic()) {
                o << "Static";
            }

            o << "Function(\"" << method.first << "\", &" << (*method.second.begin())->getQualifiedNameAsString() << ")";
        }

        o << std::endl;
    }
}

void parseClass(const clang::CXXRecordDecl* fs) {
    if(!fs->isCompleteDefinition()) {
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
    std::string::size_type oldN = 0;
    std::string::size_type n = 0;
    std::map<std::string, DiscoveredNameSpace>* namespaces = &discoveredNamespaces;

    while (( n = luaClassName.find("::", n )) != std::string::npos) {
        luaClassName.replace(n, 2, "\"][\"");

        namespaces = &((*namespaces)[luaClassName.substr(oldN, n - oldN)].children);

        n += 4;
        oldN = n;
    }

    if(fs->isTemplated()) {
        oss << "//TODO: templated" << std::endl;
    }

    oss << "state[\"" << luaClassName << "\"].setClass(kaguya::UserdataMetatable<";

    std::vector<clang::CXXRecordDecl*> bases;

    for(auto base : fs->bases()) {
        auto cxxRecordBase = base.getType()->getAsCXXRecordDecl();
        if(cxxRecordBase && getClassName(cxxRecordBase).substr(0, 5) != "std::") {
            bases.push_back(cxxRecordBase);
            if(discoveredClasses.count(getClassName(cxxRecordBase)) == 0) {
                parseClass(cxxRecordBase);
            }
        }
    }

    switch(bases.size()) {
        case 0:
            oss << className;
            break;

        case 1:
            oss << className << ", " << getClassName(*(bases.begin()));
            break;

        default:
            oss << className << ", kaguya::MultipleBase<";

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

    classes.push_back(oss.str());
    discoveredClasses.insert(className);
}

class ClassParser : public MatchFinder::MatchCallback {
    public :
        void run(const MatchFinder::MatchResult& result) override {
            if (auto* fs = result.Nodes.getNodeAs<clang::CXXRecordDecl>("lcClasses")) {
                parseClass(fs);
            }
        }
};

void printDiscoveredNamespaces(const std::map<std::string, DiscoveredNameSpace>& namespaces, std::string base = "") {
    for(auto pair : namespaces) {
        auto newBase = base + "[\"" + pair.first + "\"]";
        std::cout << "    state" << newBase << " = kaguya::NewTable();" << std::endl;

        printDiscoveredNamespaces(pair.second.children, newBase);
        if(pair.second.children.size() > 0) {
            std::cout << std::endl;
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

    printDiscoveredNamespaces(discoveredNamespaces);

    for(auto c : classes) {
        std::cout << c << std::endl;
    }

    return 0;
}