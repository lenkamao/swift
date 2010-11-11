//===--- Lexer.cpp - Swift Language Lexer ---------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Lexer and Token interfaces.
//
//===----------------------------------------------------------------------===//

#include "swift/Parse/Lexer.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
using namespace swift;

//===----------------------------------------------------------------------===//
// Setup and Helper Methods
//===----------------------------------------------------------------------===//

Lexer::Lexer(unsigned BufferID, llvm::SourceMgr &SM) : SourceMgr(SM) {
  Buffer = SM.getMemoryBuffer(BufferID);
  CurPtr = Buffer->getBufferStart();
}

void Lexer::Warning(const char *Loc, const llvm::Twine &Message) {
  SourceMgr.PrintMessage(llvm::SMLoc::getFromPointer(Loc), Message, "warning");
}

void Lexer::Error(const char *Loc, const llvm::Twine &Message) {
  SourceMgr.PrintMessage(llvm::SMLoc::getFromPointer(Loc), llvm::Twine(Message),
                         "error");
}


void Lexer::FormToken(tok::TokenKind Kind, const char *TokStart, Token &Result){
  Result.setToken(Kind, llvm::StringRef(TokStart, CurPtr-TokStart));
}

//===----------------------------------------------------------------------===//
// Lexer Subroutines
//===----------------------------------------------------------------------===//

/// SkipSlashSlashComment - Skip to the end of the line of a // comment.
void Lexer::SkipSlashSlashComment() {
  assert(CurPtr[0] == '/' && CurPtr[-1] == '/' && "Not a // comment");
  while (1) {
    switch (*CurPtr++) {
    case '\n':
    case '\r':
      return;  // If we found the end of the line, return.
    default:
      break;   // Otherwise, eat other characters.
    case 0:
      // If this is a random nul character in the middle of a buffer, skip it as
      // whitespace.
      if (CurPtr-1 != Buffer->getBufferEnd()) {
        Warning(CurPtr-1, "nul character embedded in middle of file");
        break;
      }
        
      // Otherwise, we have a // comment at end of file, warn and return.
      --CurPtr;
      Warning(CurPtr-1, "no newline at end of // comment");
      return;
    }
  }
}


/// LexIdentifier - Match [a-zA-Z_][a-zA-Z_$0-9]*
void Lexer::LexIdentifier(Token &Result) {
  const char *TokStart = CurPtr-1;
  assert((isalpha(*TokStart) || *TokStart == '_') && "Unexpected start");
  
  // Lex [a-zA-Z_$0-9]*
  while (isalnum(*CurPtr) || *CurPtr == '_' || *CurPtr == '$')
    ++CurPtr;
  
  tok::TokenKind Kind =
  llvm::StringSwitch<tok::TokenKind>(llvm::StringRef(TokStart, CurPtr-TokStart))
    .Case("__builtin_int32_type", tok::kw___builtin_int32_type)
    .Case("oneof", tok::kw_oneof)
    .Case("struct", tok::kw_struct)
    .Case("var", tok::kw_var)
    .Case("func", tok::kw_func)
    .Case("typealias", tok::kw_typealias)
    .Default(tok::identifier);
  
  return FormToken(Kind, TokStart, Result);
}

/// LexPunctuationIdentifier - Match identifiers formed out of punctuation.
void Lexer::LexPunctuationIdentifier(Token &Result) {
  const char *TokStart = CurPtr-1;
  const char *PunctuatorCharacters = "/=-+*%<>!&|^";
  
  CurPtr += strspn(CurPtr, PunctuatorCharacters);
  
  // Match various reserved words.
  if (CurPtr-TokStart == 1) {
    switch (TokStart[0]) {
    case '=': return FormToken(tok::equal, TokStart, Result);
    }
  } else if (CurPtr-TokStart == 2) {
    switch ((TokStart[0] << 8) | TokStart[1]) {
    case ('-' << 8) | '>': // ->
      return FormToken(tok::arrow, TokStart, Result);
    }
  }
  
  return FormToken(tok::identifier, TokStart, Result);
}

/// LexDollarIdent - Match $[0-9a-zA-Z_$]*
void Lexer::LexDollarIdent(Token &Result) {
  const char *TokStart = CurPtr-1;
  assert(*TokStart == '$');
  
  // Lex [a-zA-Z_$0-9]*
  while (isalnum(*CurPtr) || *CurPtr == '_' || *CurPtr == '$')
    ++CurPtr;
  
  return FormToken(tok::dollarident, TokStart, Result);
}


/// LexDigit - Match [0-9]+
void Lexer::LexDigit(Token &Result) {
  const char *TokStart = CurPtr-1;
  assert(isdigit(*TokStart) && "Unexpected start");

  // Lex [0-9]*
  while (isdigit(*CurPtr))
    ++CurPtr;
  
  return FormToken(tok::numeric_constant, TokStart, Result);
}


//===----------------------------------------------------------------------===//
// Main Lexer Loop
//===----------------------------------------------------------------------===//

void Lexer::Lex(Token &Result) {
  assert(CurPtr >= Buffer->getBufferStart() &&
         CurPtr <= Buffer->getBufferEnd() && "Cur Char Pointer out of range!");
Restart:
  // Remember the start of the token so we can form the text range.
  const char *TokStart = CurPtr;
  
  switch (*CurPtr++) {
  default:
    Error(CurPtr-1, "invalid character in source file");
    return FormToken(tok::unknown, TokStart, Result);

  case ' ':
  case '\t':
  case '\n':
  case '\r':
    goto Restart;  // Skip whitespace.
  case 0:
    // If this is a random nul character in the middle of a buffer, skip it as
    // whitespace.
    if (CurPtr-1 != Buffer->getBufferEnd()) {
      Warning(CurPtr-1, "nul character embedded in middle of file");
      goto Restart;
    }
      
    // Otherwise, this is the end of the buffer.  Return EOF.
    return FormToken(tok::eof, TokStart, Result);

  case '(': return FormToken(tok::l_paren,  TokStart, Result);
  case ')': return FormToken(tok::r_paren,  TokStart, Result);
  case '{': return FormToken(tok::l_brace,  TokStart, Result);
  case '}': return FormToken(tok::r_brace,  TokStart, Result);
  case '[': return FormToken(tok::l_square, TokStart, Result);
  case ']': return FormToken(tok::r_square, TokStart, Result);

  case '.': return FormToken(tok::period,   TokStart, Result);
  case ',': return FormToken(tok::comma,    TokStart, Result);
  case ';': return FormToken(tok::semi,     TokStart, Result);
      
  case ':':
    if (CurPtr[0] != ':')
      return FormToken(tok::colon, TokStart, Result);
    ++CurPtr;
    return FormToken(tok::coloncolon, TokStart, Result);
      
  // Punctuator identifier characters.
  case '/':
    if (CurPtr[0] == '/') {  // "//"
      SkipSlashSlashComment();
      goto Restart;
    }
    // '/' starts an identifier.
    return LexPunctuationIdentifier(Result);

  case '=':
  case '-':
  case '+':
  case '*': 
  case '%':
  case '<':
  case '>':
  case '!':
  case '&':
  case '|':
  case '^':
    return LexPunctuationIdentifier(Result);

  case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
  case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
  case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
  case 'V': case 'W': case 'X': case 'Y': case 'Z':
  case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
  case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
  case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
  case 'v': case 'w': case 'x': case 'y': case 'z':
  case '_':
    return LexIdentifier(Result);
  
  case '$':
    return LexDollarIdent(Result);
      
  case '0': case '1': case '2': case '3': case '4':
  case '5': case '6': case '7': case '8': case '9':
    return LexDigit(Result);
  }
}

