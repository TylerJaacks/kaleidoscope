#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

enum Token {
  tok_eof = -1,

  tok_def = -2,
  tok_extern = -3,

  tok_identifier = -4,
  tok_number = -5
};

static std::string IdentifierStr;
static double NumVal;

static int gettok() {
  static int LastChar = ' ';

  // Whitespace
  while (isspace(LastChar))
    LastChar = getchar();

  // Identifier: [a-zA-z][a-zA-Z0-9]*
  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;

    while (isalnum((LastChar = getchar())))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def")
      return tok_def;

    if (IdentifierStr == "extern")
      return tok_extern;

    return tok_identifier;
  }

  // Number: [0-9.]+
  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;

    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);

    return tok_number;
  }

  // Handle comments.
  if (LastChar == '#') {
    do
      LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return gettok();
  }

  // Check for end of file.
  if (LastChar == EOF)
    return tok_eof;

  int ThisChar = LastChar;

  LastChar = getchar();

  return ThisChar;
}

// ExprAst - Base class for all expression nodes.
class ExprAST {
public:
  virtual ~ExprAST() {}
  virtual llvm::Value *codegen() = 0;
};

// NumberExprAST - Expression class for numbers.
class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
  virtual llvm::Value *codegen();
};

// VariableExprAST - Expression class for variables.
class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(const std::string &Name) : Name(Name) {}
  virtual llvm::Value *codegen();
};

// BinaryExprAST - Expression class for binary operators.
class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : Op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  virtual llvm::Value *codegen();
};

// CallExprAST - Expression class for functions calls.
class CallExprAST : public ExprAST {
  std::string Calle;
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const std::string &Calle,
              std::vector<std::unique_ptr<ExprAST>> Args)
      : Calle(Calle), Args(std::move(Args)) {}
  virtual llvm::Value *codegen();
};

class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;

public:
  PrototypeAST(const std::string &Name, std::vector<std::string> Args)
      : Name(Name), Args(std::move(Args)) {}

  const std::string &getName() const { return Name; }

  virtual llvm::Function *codegen();
};

class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}

    virtual llvm::Function *codegen();
};

//----------------------------------------------------------------------------//
//                                 Parser
//----------------------------------------------------------------------------//

static int CurTok;

static int getNextToken() { return CurTok = gettok(); }

static std::map<char, int> BinopPrecedence;

static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  int TokPrec = BinopPrecedence[CurTok];

  if (TokPrec <= 0)
    return -1;

  return TokPrec;
}

std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "[LogError]: %s\n", Str);

  return nullptr;
}

std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);

  return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();

static std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);

  getNextToken();

  return std::move(Result);
}

static std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken();

  auto V = ParseExpression();

  if (!V) {
    return nullptr;
  }

  if (CurTok != ')') {
    return LogError("expected ')'");
  }

  getNextToken();

  return V;
}

static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;

  getNextToken();

  if (CurTok != '(')
    return std::make_unique<VariableExprAST>(IdName);

  getNextToken();

  std::vector<std::unique_ptr<ExprAST>> Args;

  if (CurTok != ')') {
    while (1) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == ')')
        break;

      if (CurTok != ',')
        return LogError("Expected ')' or ',' in the argument list.");

      getNextToken();
    }
  }

  getNextToken();

  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

static std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError("Unkown token when expecting expression.");
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  }
}

static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
  while (1) {
    int TokPrec = GetTokPrecedence();

    if (TokPrec < ExprPrec)
      return LHS;

    int BinOp = CurTok;

    getNextToken();

    auto RHS = ParsePrimary();

    if (!RHS)
      return nullptr;

    int NextPrec = GetTokPrecedence();

    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));

      if (!RHS)
        return nullptr;
    }

    LHS =
        std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

static std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();

  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}

static std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogErrorP("Expected function in prototype.");

  std::string FnName = IdentifierStr;

  getNextToken();

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype.");

  std::vector<std::string> ArgNames;

  while (getNextToken() == tok_identifier) {
    ArgNames.push_back(IdentifierStr);
  }

  if (CurTok != ')')
    return LogErrorP("Expected ')' in prototype.");

  getNextToken();

  return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

static std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken();

  auto Proto = ParsePrototype();

  if (!Proto)
    return nullptr;

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));

  return nullptr;
}

static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    auto Proto = std::make_unique<PrototypeAST>("", std::vector<std::string>());

    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }

  return nullptr;
}

static std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken();

  return ParsePrototype();
}

//----------------------------------------------------------------------------//
//                            Top-Level parsing
//----------------------------------------------------------------------------//

static void HandleDefinition() {
  if (auto FnAST = ParseDefinition()) {
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read function definitions:");

      FnIR->print(llvm::errs());

      fprintf(stderr, "\n");
    }
  } else {
    getNextToken();
  }
}

static void HandleExtern() {
  if (auto ProtoAST = ParseExtern()) {
    if (auto *FnIR = ProtoAST->codegen()) {
      fprintf(stderr, "Read extern.");

      FnIR->print(llvm::errs());

      fprintf(stderr, "\n");
    }
  } else {
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  if (auto FnAST = ParseTopLevelExpr()) {
    if (auto *FnIR = FnAST->codegen()) {
      fprintf(stderr, "Read top level expression.");

      FnIR->print(llvm::errs());

      fprintf(stderr, "\n");

      FnIR->eraseFromParent();
    }
  } else {
    getNextToken();
  }
}

// ---------------------------------------------------

static std::unique_ptr<llvm::LLVMContext> llvmContext;
static std::unique_ptr<llvm::IRBuilder<>> irBuilder;

static std::unique_ptr<llvm::Module> module;
static std::map<std::string, llvm::Value *> namedValues;

llvm::Value *LogErrorV(const char *Str) {
  LogError(Str);

  return nullptr;
}

llvm::Value *NumberExprAST::codegen() {
  return llvm::ConstantFP::get(*llvmContext, llvm::APFloat(Val));
}

llvm::Value *VariableExprAST::codegen() {
  llvm::Value *V = namedValues[Name];

  if (!V)
    LogError("Unkown variable name.");

  return V;
}

llvm::Value *BinaryExprAST::codegen() {
  llvm::Value *L = LHS->codegen();
  llvm::Value *R = LHS->codegen();

  if (!L || !R)
    return nullptr;

  // TODO: Add additional binary operators.
  switch (Op) {
    case '+':
      return irBuilder->CreateFAdd(L, R, "addtmp");
    case '-':
      return irBuilder->CreateFSub(L, R, "subtmp");
    case '*':
      return irBuilder->CreateFMul(L, R, "multmp");
    case '<':
      L = irBuilder->CreateFCmpULT(L, R, "cmptmp");

      return irBuilder->CreateUIToFP(L, llvm::Type::getDoubleTy(*llvmContext));
    default:
      return LogErrorV("Invalid binary operator.");
  }
}

llvm::Value *CallExprAST::codegen() {
  llvm::Function *CalleF = module->getFunction(Calle);

  if (!CalleF)
    return LogErrorV("Unkown function refrenced.");

  if (CalleF->arg_size() != Args.size())
    return LogErrorV("Incorrect number of arguments passed.");
  
  std::vector<llvm::Value*> ArgsV;

  for (unsigned int i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());

    if (!Args.back())
      return nullptr;
  }

  return irBuilder->CreateCall(CalleF, ArgsV, "calltmp");
}

llvm::Function *PrototypeAST::codegen() {
  std::vector<llvm::Type*> Doubles(Args.size(), llvm::Type::getDoubleTy(*llvmContext));

  llvm::FunctionType *functionType = llvm::FunctionType::get(llvm::Type::getDoubleTy(*llvmContext), Doubles, false);

  llvm::Function *function = llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, Name, module.get());

  unsigned index = 0;

  for (auto &Arg : function->args()) {
    Arg.setName(Args[index++]);
  }
  
  return function;
}

llvm::Function *FunctionAST::codegen() {
  llvm::Function *function = module->getFunction(Proto->getName());

  if (!function) function = Proto->codegen();

  if (!function) return nullptr;

  if (!function->empty()) return (llvm::Function*) LogErrorV("Function cannot be redefined.");

  llvm::BasicBlock *BB = llvm::BasicBlock::Create(*llvmContext, "entry", function);
  
  irBuilder->SetInsertPoint(BB);

  namedValues.clear();

  for (auto &Arg : function->args())
    namedValues[Arg.getName()] = &Arg;

  if (llvm::Value *RetVal = Body->codegen()) {
    irBuilder->CreateRet(RetVal);

    llvm::verifyFunction(*function);

    return function;
  }

  function->eraseFromParent();

  return nullptr;
}


static void InitializeModule() {
  llvmContext = std::make_unique<llvm::LLVMContext>();
  module = std::make_unique<llvm::Module>("My cool Jit!", *llvmContext);

  irBuilder = std::make_unique<llvm::IRBuilder<>>(*llvmContext);
}

//----------------------------------------------------------------------------//
//                            Main driver code.
//----------------------------------------------------------------------------//

static void MainLoop() {
  while (1) {
    fprintf(stderr, "ready> ");

    switch (CurTok) {
    case tok_eof:
      return;
    case ';':
      getNextToken();
      break;
    case tok_def:
      HandleDefinition();
      break;
    case tok_extern:
      HandleExtern();
      break;
    default:
      HandleTopLevelExpression();
      break;
    }
  }
}

int main(int argc, char **argv) {
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;

  fprintf(stderr, "ready> ");
  getNextToken();

  InitializeModule();

  MainLoop();

  module->print(llvm::errs(), nullptr);

  return 0;
}