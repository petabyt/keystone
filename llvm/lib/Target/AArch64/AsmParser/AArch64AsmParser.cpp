//==- AArch64AsmParser.cpp - Parse AArch64 assembly to MCInst instructions -==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/AArch64AddressingModes.h"
#include "MCTargetDesc/AArch64MCExpr.h"
#include "MCTargetDesc/AArch64TargetStreamer.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

#include "keystone/arm64.h"

#include <cstdio>
using namespace llvm_ks;

namespace {

class AArch64Operand;

class AArch64AsmParser : public MCTargetAsmParser {
private:
  StringRef Mnemonic; ///< Instruction mnemonic.

  // Map of register aliases registers via the .req directive.
  StringMap<std::pair<bool, unsigned> > RegisterReqs;

  AArch64TargetStreamer &getTargetStreamer() {
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<AArch64TargetStreamer &>(TS);
  }

  SMLoc getLoc() const { return getParser().getTok().getLoc(); }

  bool parseSysAlias(StringRef Name, SMLoc NameLoc, OperandVector &Operands);
  AArch64CC::CondCode parseCondCodeString(StringRef Cond);
  bool parseCondCode(OperandVector &Operands, bool invertCondCode);
  unsigned matchRegisterNameAlias(StringRef Name, bool isVector);
  int tryParseRegister();
  int tryMatchVectorRegister(StringRef &Kind, bool expected);
  bool parseRegister(OperandVector &Operands);
  bool parseSymbolicImmVal(const MCExpr *&ImmVal);
  bool parseVectorList(OperandVector &Operands);
  bool parseOperand(OperandVector &Operands, bool isCondCode,
                    bool invertCondCode);

  void Warning(SMLoc L, const Twine &Msg) { getParser().Warning(L, Msg); }
  bool Error(SMLoc L, const Twine &Msg) { return getParser().Error(L, Msg); }
  bool showMatchError(SMLoc Loc, unsigned ErrCode);

  bool parseDirectiveWord(unsigned Size, SMLoc L);
  bool parseDirectiveInst(SMLoc L);

  bool parseDirectiveTLSDescCall(SMLoc L);

  bool parseDirectiveLOH(StringRef LOH, SMLoc L);
  bool parseDirectiveLtorg(SMLoc L);

  bool parseDirectiveReq(StringRef Name, SMLoc L);
  bool parseDirectiveUnreq(SMLoc L);

  bool validateInstruction(MCInst &Inst, SmallVectorImpl<SMLoc> &Loc);
  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm, unsigned int &ErrorCode, uint64_t &Address) override;
/// @name Auto-generated Match Functions
/// {

#define GET_ASSEMBLER_HEADER
#include "AArch64GenAsmMatcher.inc"

  /// }

  OperandMatchResultTy tryParseOptionalShiftExtend(OperandVector &Operands);
  OperandMatchResultTy tryParseBarrierOperand(OperandVector &Operands);
  OperandMatchResultTy tryParseMRSSystemRegister(OperandVector &Operands);
  OperandMatchResultTy tryParseSysReg(OperandVector &Operands);
  OperandMatchResultTy tryParseSysCROperand(OperandVector &Operands);
  OperandMatchResultTy tryParsePrefetch(OperandVector &Operands);
  OperandMatchResultTy tryParsePSBHint(OperandVector &Operands);
  OperandMatchResultTy tryParseAdrpLabel(OperandVector &Operands);
  OperandMatchResultTy tryParseAdrLabel(OperandVector &Operands);
  OperandMatchResultTy tryParseFPImm(OperandVector &Operands);
  OperandMatchResultTy tryParseAddSubImm(OperandVector &Operands);
  OperandMatchResultTy tryParseGPR64sp0Operand(OperandVector &Operands);
  bool tryParseVectorRegister(OperandVector &Operands);
  OperandMatchResultTy tryParseGPRSeqPair(OperandVector &Operands);

public:
  enum AArch64MatchResultTy {
    Match_InvalidSuffix = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "AArch64GenAsmMatcher.inc"
  };
  AArch64AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                   const MCInstrInfo &MII, const MCTargetOptions &Options)
    : MCTargetAsmParser(Options, STI) {
    MCAsmParserExtension::Initialize(Parser);
    MCStreamer &S = getParser().getStreamer();
    if (S.getTargetStreamer() == nullptr)
      new AArch64TargetStreamer(S);

    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(getSTI().getFeatureBits()));
  }

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands, unsigned int &ErrorCode) override;
  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc, unsigned int &ErrorCode) override;
  bool ParseDirective(AsmToken DirectiveID) override;
  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  static bool classifySymbolRef(const MCExpr *Expr,
                                AArch64MCExpr::VariantKind &ELFRefKind,
                                MCSymbolRefExpr::VariantKind &DarwinRefKind,
                                int64_t &Addend);
};
} // end anonymous namespace

namespace {

/// AArch64Operand - Instances of this class represent a parsed AArch64 machine
/// instruction.
class AArch64Operand : public MCParsedAsmOperand {
private:
  enum KindTy {
    k_Immediate,
    k_ShiftedImm,
    k_CondCode,
    k_Register,
    k_VectorList,
    k_VectorIndex,
    k_Token,
    k_SysReg,
    k_SysCR,
    k_Prefetch,
    k_ShiftExtend,
    k_FPImm,
    k_Barrier,
    k_PSBHint,
  } Kind;

  SMLoc StartLoc, EndLoc;

  struct TokOp {
    const char *Data;
    unsigned Length;
    bool IsSuffix; // Is the operand actually a suffix on the mnemonic.
  };

  struct RegOp {
    unsigned RegNum;
    bool isVector;
  };

  struct VectorListOp {
    unsigned RegNum;
    unsigned Count;
    unsigned NumElements;
    unsigned ElementKind;
  };

  struct VectorIndexOp {
    unsigned Val;
  };

  struct ImmOp {
    const MCExpr *Val;
  };

  struct ShiftedImmOp {
    const MCExpr *Val;
    unsigned ShiftAmount;
  };

  struct CondCodeOp {
    AArch64CC::CondCode Code;
  };

  struct FPImmOp {
    unsigned Val; // Encoded 8-bit representation.
  };

  struct BarrierOp {
    unsigned Val; // Not the enum since not all values have names.
    const char *Data;
    unsigned Length;
  };

  struct SysRegOp {
    const char *Data;
    unsigned Length;
    uint32_t MRSReg;
    uint32_t MSRReg;
    uint32_t PStateField;
  };

  struct SysCRImmOp {
    unsigned Val;
  };

  struct PrefetchOp {
    unsigned Val;
    const char *Data;
    unsigned Length;
  };

  struct PSBHintOp {
    unsigned Val;
    const char *Data;
    unsigned Length;
  };

  struct ShiftExtendOp {
    AArch64_AM::ShiftExtendType Type;
    unsigned Amount;
    bool HasExplicitAmount;
  };

  struct ExtendOp {
    unsigned Val;
  };

  union {
    struct TokOp Tok;
    struct RegOp Reg;
    struct VectorListOp VectorList;
    struct VectorIndexOp VectorIndex;
    struct ImmOp Imm;
    struct ShiftedImmOp ShiftedImm;
    struct CondCodeOp CondCode;
    struct FPImmOp FPImm;
    struct BarrierOp Barrier;
    struct SysRegOp SysReg;
    struct SysCRImmOp SysCRImm;
    struct PrefetchOp Prefetch;
    struct PSBHintOp PSBHint;
    struct ShiftExtendOp ShiftExtend;
  };

  // Keep the MCContext around as the MCExprs may need manipulated during
  // the add<>Operands() calls.
  MCContext &Ctx;

public:
  AArch64Operand(KindTy K, MCContext &Ctx) : Kind(K), Ctx(Ctx) {}

  AArch64Operand(const AArch64Operand &o) : MCParsedAsmOperand(), Ctx(o.Ctx) {
    Kind = o.Kind;
    StartLoc = o.StartLoc;
    EndLoc = o.EndLoc;
    switch (Kind) {
    case k_Token:
      Tok = o.Tok;
      break;
    case k_Immediate:
      Imm = o.Imm;
      break;
    case k_ShiftedImm:
      ShiftedImm = o.ShiftedImm;
      break;
    case k_CondCode:
      CondCode = o.CondCode;
      break;
    case k_FPImm:
      FPImm = o.FPImm;
      break;
    case k_Barrier:
      Barrier = o.Barrier;
      break;
    case k_Register:
      Reg = o.Reg;
      break;
    case k_VectorList:
      VectorList = o.VectorList;
      break;
    case k_VectorIndex:
      VectorIndex = o.VectorIndex;
      break;
    case k_SysReg:
      SysReg = o.SysReg;
      break;
    case k_SysCR:
      SysCRImm = o.SysCRImm;
      break;
    case k_Prefetch:
      Prefetch = o.Prefetch;
      break;
    case k_PSBHint:
      PSBHint = o.PSBHint;
      break;
    case k_ShiftExtend:
      ShiftExtend = o.ShiftExtend;
      break;
    }
  }

  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const override { return StartLoc; }
  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const override { return EndLoc; }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }

  bool isTokenSuffix() const {
    assert(Kind == k_Token && "Invalid access!");
    return Tok.IsSuffix;
  }

  const MCExpr *getImm() const {
    assert(Kind == k_Immediate && "Invalid access!");
    return Imm.Val;
  }

  const MCExpr *getShiftedImmVal() const {
    assert(Kind == k_ShiftedImm && "Invalid access!");
    return ShiftedImm.Val;
  }

  unsigned getShiftedImmShift() const {
    assert(Kind == k_ShiftedImm && "Invalid access!");
    return ShiftedImm.ShiftAmount;
  }

  AArch64CC::CondCode getCondCode() const {
    assert(Kind == k_CondCode && "Invalid access!");
    return CondCode.Code;
  }

  unsigned getFPImm() const {
    assert(Kind == k_FPImm && "Invalid access!");
    return FPImm.Val;
  }

  unsigned getBarrier() const {
    assert(Kind == k_Barrier && "Invalid access!");
    return Barrier.Val;
  }

  StringRef getBarrierName() const {
    assert(Kind == k_Barrier && "Invalid access!");
    return StringRef(Barrier.Data, Barrier.Length);
  }

  unsigned getReg() const override {
    assert(Kind == k_Register && "Invalid access!");
    return Reg.RegNum;
  }

  unsigned getVectorListStart() const {
    assert(Kind == k_VectorList && "Invalid access!");
    return VectorList.RegNum;
  }

  unsigned getVectorListCount() const {
    assert(Kind == k_VectorList && "Invalid access!");
    return VectorList.Count;
  }

  unsigned getVectorIndex() const {
    assert(Kind == k_VectorIndex && "Invalid access!");
    return VectorIndex.Val;
  }

  StringRef getSysReg() const {
    assert(Kind == k_SysReg && "Invalid access!");
    return StringRef(SysReg.Data, SysReg.Length);
  }

  unsigned getSysCR() const {
    assert(Kind == k_SysCR && "Invalid access!");
    return SysCRImm.Val;
  }

  unsigned getPrefetch() const {
    assert(Kind == k_Prefetch && "Invalid access!");
    return Prefetch.Val;
  }

  unsigned getPSBHint() const {
    assert(Kind == k_PSBHint && "Invalid access!");
    return PSBHint.Val;
  }

  StringRef getPSBHintName() const {
    assert(Kind == k_PSBHint && "Invalid access!");
    return StringRef(PSBHint.Data, PSBHint.Length);
  }

  StringRef getPrefetchName() const {
    assert(Kind == k_Prefetch && "Invalid access!");
    return StringRef(Prefetch.Data, Prefetch.Length);
  }

  AArch64_AM::ShiftExtendType getShiftExtendType() const {
    assert(Kind == k_ShiftExtend && "Invalid access!");
    return ShiftExtend.Type;
  }

  unsigned getShiftExtendAmount() const {
    assert(Kind == k_ShiftExtend && "Invalid access!");
    return ShiftExtend.Amount;
  }

  bool hasShiftExtendAmount() const {
    assert(Kind == k_ShiftExtend && "Invalid access!");
    return ShiftExtend.HasExplicitAmount;
  }

  bool isImm() const override { return Kind == k_Immediate; }
  bool isMem() const override { return false; }
  bool isSImm9() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= -256 && Val < 256);
  }
  bool isSImm7s4() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= -256 && Val <= 252 && (Val & 3) == 0);
  }
  bool isSImm7s8() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= -512 && Val <= 504 && (Val & 7) == 0);
  }
  bool isSImm7s16() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= -1024 && Val <= 1008 && (Val & 15) == 0);
  }

  bool isSymbolicUImm12Offset(const MCExpr *Expr, unsigned Scale) const {
    AArch64MCExpr::VariantKind ELFRefKind;
    MCSymbolRefExpr::VariantKind DarwinRefKind;
    int64_t Addend;
    if (!AArch64AsmParser::classifySymbolRef(Expr, ELFRefKind, DarwinRefKind,
                                           Addend)) {
      // If we don't understand the expression, assume the best and
      // let the fixup and relocation code deal with it.
      return true;
    }

    if (DarwinRefKind == MCSymbolRefExpr::VK_PAGEOFF ||
        ELFRefKind == AArch64MCExpr::VK_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_GOT_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12_NC ||
        ELFRefKind == AArch64MCExpr::VK_TPREL_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_TPREL_LO12_NC ||
        ELFRefKind == AArch64MCExpr::VK_GOTTPREL_LO12_NC ||
        ELFRefKind == AArch64MCExpr::VK_TLSDESC_LO12) {
      // Note that we don't range-check the addend. It's adjusted modulo page
      // size when converted, so there is no "out of range" condition when using
      // @pageoff.
      return Addend >= 0 && (Addend % Scale) == 0;
    } else if (DarwinRefKind == MCSymbolRefExpr::VK_GOTPAGEOFF ||
               DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGEOFF) {
      // @gotpageoff/@tlvppageoff can only be used directly, not with an addend.
      return Addend == 0;
    }

    return false;
  }

  template <int Scale> bool isUImm12Offset() const {
    if (!isImm())
      return false;

    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return isSymbolicUImm12Offset(getImm(), Scale);

    int64_t Val = MCE->getValue();
    return (Val % Scale) == 0 && Val >= 0 && (Val / Scale) < 0x1000;
  }

  bool isImm0_1() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 0 && Val < 2);
  }
  bool isImm0_7() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 0 && Val < 8);
  }
  bool isImm1_8() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val > 0 && Val < 9);
  }
  bool isImm0_15() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 0 && Val < 16);
  }
  bool isImm1_16() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val > 0 && Val < 17);
  }
  bool isImm0_31() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 0 && Val < 32);
  }
  bool isImm1_31() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 1 && Val < 32);
  }
  bool isImm1_32() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 1 && Val < 33);
  }
  bool isImm0_63() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 0 && Val < 64);
  }
  bool isImm1_63() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 1 && Val < 64);
  }
  bool isImm1_64() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 1 && Val < 65);
  }
  bool isImm0_127() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 0 && Val < 128);
  }
  bool isImm0_255() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 0 && Val < 256);
  }
  bool isImm0_65535() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 0 && Val < 65536);
  }
  bool isImm32_63() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 32 && Val < 64);
  }
  bool isLogicalImm32() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    if (Val >> 32 != 0 && Val >> 32 != ~0LL)
      return false;
    Val &= 0xFFFFFFFF;
    return AArch64_AM::isLogicalImmediate(Val, 32);
  }
  bool isLogicalImm64() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    return AArch64_AM::isLogicalImmediate(MCE->getValue(), 64);
  }
  bool isLogicalImm32Not() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = ~MCE->getValue() & 0xFFFFFFFF;
    return AArch64_AM::isLogicalImmediate(Val, 32);
  }
  bool isLogicalImm64Not() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    return AArch64_AM::isLogicalImmediate(~MCE->getValue(), 64);
  }
  bool isShiftedImm() const { return Kind == k_ShiftedImm; }
  bool isAddSubImm() const {
    if (!isShiftedImm() && !isImm())
      return false;

    const MCExpr *Expr;

    // An ADD/SUB shifter is either 'lsl #0' or 'lsl #12'.
    if (isShiftedImm()) {
      unsigned Shift = ShiftedImm.ShiftAmount;
      Expr = ShiftedImm.Val;
      if (Shift != 0 && Shift != 12)
        return false;
    } else {
      Expr = getImm();
    }

    AArch64MCExpr::VariantKind ELFRefKind;
    MCSymbolRefExpr::VariantKind DarwinRefKind;
    int64_t Addend;
    if (AArch64AsmParser::classifySymbolRef(Expr, ELFRefKind,
                                          DarwinRefKind, Addend)) {
      return DarwinRefKind == MCSymbolRefExpr::VK_PAGEOFF
          || DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGEOFF
          || (DarwinRefKind == MCSymbolRefExpr::VK_GOTPAGEOFF && Addend == 0)
          || ELFRefKind == AArch64MCExpr::VK_LO12
          || ELFRefKind == AArch64MCExpr::VK_DTPREL_HI12
          || ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12
          || ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12_NC
          || ELFRefKind == AArch64MCExpr::VK_TPREL_HI12
          || ELFRefKind == AArch64MCExpr::VK_TPREL_LO12
          || ELFRefKind == AArch64MCExpr::VK_TPREL_LO12_NC
          || ELFRefKind == AArch64MCExpr::VK_TLSDESC_LO12;
    }

    // Otherwise it should be a real immediate in range:
    const MCConstantExpr *CE = cast<MCConstantExpr>(Expr);
    return CE->getValue() >= 0 && CE->getValue() <= 0xfff;
  }
  bool isAddSubImmNeg() const {
    if (!isShiftedImm() && !isImm())
      return false;

    const MCExpr *Expr;

    // An ADD/SUB shifter is either 'lsl #0' or 'lsl #12'.
    if (isShiftedImm()) {
      unsigned Shift = ShiftedImm.ShiftAmount;
      Expr = ShiftedImm.Val;
      if (Shift != 0 && Shift != 12)
        return false;
    } else
      Expr = getImm();

    // Otherwise it should be a real negative immediate in range:
    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr);
    return CE != nullptr && CE->getValue() < 0 && -CE->getValue() <= 0xfff;
  }
  bool isCondCode() const { return Kind == k_CondCode; }
  bool isSIMDImmType10() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    return AArch64_AM::isAdvSIMDModImmType10(MCE->getValue());
  }
  bool isBranchTarget26() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return true;
    int64_t Val = MCE->getValue();
    return ((Val & 0x3) == 0);
  }
  bool isPCRelLabel19() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return true;
    int64_t Val = MCE->getValue();
    return ((Val & 0x3) == 0);
  }
  bool isBranchTarget14() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return true;
    int64_t Val = MCE->getValue();
    return ((Val & 0x3) == 0);
  }

  bool
  isMovWSymbol(ArrayRef<AArch64MCExpr::VariantKind> AllowedModifiers) const {
    if (!isImm())
      return false;

    AArch64MCExpr::VariantKind ELFRefKind;
    MCSymbolRefExpr::VariantKind DarwinRefKind;
    int64_t Addend;
    if (!AArch64AsmParser::classifySymbolRef(getImm(), ELFRefKind,
                                             DarwinRefKind, Addend)) {
      return false;
    }
    if (DarwinRefKind != MCSymbolRefExpr::VK_None)
      return false;

    for (unsigned i = 0; i != AllowedModifiers.size(); ++i) {
      if (ELFRefKind == AllowedModifiers[i])
        return Addend == 0;
    }

    return false;
  }

  bool isMovZSymbolG3() const {
    return isMovWSymbol(AArch64MCExpr::VK_ABS_G3);
  }

  bool isMovZSymbolG2() const {
    return isMovWSymbol({AArch64MCExpr::VK_ABS_G2, AArch64MCExpr::VK_ABS_G2_S,
                         AArch64MCExpr::VK_TPREL_G2,
                         AArch64MCExpr::VK_DTPREL_G2});
  }

  bool isMovZSymbolG1() const {
    return isMovWSymbol({
        AArch64MCExpr::VK_ABS_G1, AArch64MCExpr::VK_ABS_G1_S,
        AArch64MCExpr::VK_GOTTPREL_G1, AArch64MCExpr::VK_TPREL_G1,
        AArch64MCExpr::VK_DTPREL_G1,
    });
  }

  bool isMovZSymbolG0() const {
    return isMovWSymbol({AArch64MCExpr::VK_ABS_G0, AArch64MCExpr::VK_ABS_G0_S,
                         AArch64MCExpr::VK_TPREL_G0,
                         AArch64MCExpr::VK_DTPREL_G0});
  }

  bool isMovKSymbolG3() const {
    return isMovWSymbol(AArch64MCExpr::VK_ABS_G3);
  }

  bool isMovKSymbolG2() const {
    return isMovWSymbol(AArch64MCExpr::VK_ABS_G2_NC);
  }

  bool isMovKSymbolG1() const {
    return isMovWSymbol({AArch64MCExpr::VK_ABS_G1_NC,
                         AArch64MCExpr::VK_TPREL_G1_NC,
                         AArch64MCExpr::VK_DTPREL_G1_NC});
  }

  bool isMovKSymbolG0() const {
    return isMovWSymbol(
        {AArch64MCExpr::VK_ABS_G0_NC, AArch64MCExpr::VK_GOTTPREL_G0_NC,
         AArch64MCExpr::VK_TPREL_G0_NC, AArch64MCExpr::VK_DTPREL_G0_NC});
  }

  template<int RegWidth, int Shift>
  bool isMOVZMovAlias() const {
    if (!isImm()) return false;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    uint64_t Value = CE->getValue();

    if (RegWidth == 32)
      Value &= 0xffffffffULL;

    // "lsl #0" takes precedence: in practice this only affects "#0, lsl #0".
    if (Value == 0 && Shift != 0)
      return false;

    return (Value & ~(0xffffULL << Shift)) == 0;
  }

  template<int RegWidth, int Shift>
  bool isMOVNMovAlias() const {
    if (!isImm()) return false;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    uint64_t Value = CE->getValue();

    // MOVZ takes precedence over MOVN.
    for (int MOVZShift = 0; MOVZShift <= 48; MOVZShift += 16)
      if ((Value & ~(0xffffULL << MOVZShift)) == 0)
        return false;

    Value = ~Value;
    if (RegWidth == 32)
      Value &= 0xffffffffULL;

    return (Value & ~(0xffffULL << Shift)) == 0;
  }

  bool isFPImm() const { return Kind == k_FPImm; }
  bool isBarrier() const { return Kind == k_Barrier; }
  bool isSysReg() const { return Kind == k_SysReg; }
  bool isMRSSystemRegister() const {
    if (!isSysReg()) return false;

    return SysReg.MRSReg != -1U;
  }
  bool isMSRSystemRegister() const {
    if (!isSysReg()) return false;
    return SysReg.MSRReg != -1U;
  }
  bool isSystemPStateFieldWithImm0_1() const {
    if (!isSysReg()) return false;
    return (SysReg.PStateField == AArch64PState::PAN ||
            SysReg.PStateField == AArch64PState::UAO);
  }
  bool isSystemPStateFieldWithImm0_15() const {
    if (!isSysReg() || isSystemPStateFieldWithImm0_1()) return false;
    return SysReg.PStateField != -1U;
  }
  bool isReg() const override { return Kind == k_Register && !Reg.isVector; }
  bool isVectorReg() const { return Kind == k_Register && Reg.isVector; }
  bool isVectorRegLo() const {
    return Kind == k_Register && Reg.isVector &&
           AArch64MCRegisterClasses[AArch64::FPR128_loRegClassID].contains(
               Reg.RegNum);
  }
  bool isGPR32as64() const {
    return Kind == k_Register && !Reg.isVector &&
      AArch64MCRegisterClasses[AArch64::GPR64RegClassID].contains(Reg.RegNum);
  }
  bool isWSeqPair() const {
    return Kind == k_Register && !Reg.isVector &&
           AArch64MCRegisterClasses[AArch64::WSeqPairsClassRegClassID].contains(
               Reg.RegNum);
  }
  bool isXSeqPair() const {
    return Kind == k_Register && !Reg.isVector &&
           AArch64MCRegisterClasses[AArch64::XSeqPairsClassRegClassID].contains(
               Reg.RegNum);
  }

  bool isGPR64sp0() const {
    return Kind == k_Register && !Reg.isVector &&
      AArch64MCRegisterClasses[AArch64::GPR64spRegClassID].contains(Reg.RegNum);
  }

  /// Is this a vector list with the type implicit (presumably attached to the
  /// instruction itself)?
  template <unsigned NumRegs> bool isImplicitlyTypedVectorList() const {
    return Kind == k_VectorList && VectorList.Count == NumRegs &&
           !VectorList.ElementKind;
  }

  template <unsigned NumRegs, unsigned NumElements, char ElementKind>
  bool isTypedVectorList() const {
    if (Kind != k_VectorList)
      return false;
    if (VectorList.Count != NumRegs)
      return false;
    if (VectorList.ElementKind != ElementKind)
      return false;
    return VectorList.NumElements == NumElements;
  }

  bool isVectorIndex1() const {
    return Kind == k_VectorIndex && VectorIndex.Val == 1;
  }
  bool isVectorIndexB() const {
    return Kind == k_VectorIndex && VectorIndex.Val < 16;
  }
  bool isVectorIndexH() const {
    return Kind == k_VectorIndex && VectorIndex.Val < 8;
  }
  bool isVectorIndexS() const {
    return Kind == k_VectorIndex && VectorIndex.Val < 4;
  }
  bool isVectorIndexD() const {
    return Kind == k_VectorIndex && VectorIndex.Val < 2;
  }
  bool isToken() const override { return Kind == k_Token; }
  bool isTokenEqual(StringRef Str) const {
    return Kind == k_Token && getToken() == Str;
  }
  bool isSysCR() const { return Kind == k_SysCR; }
  bool isPrefetch() const { return Kind == k_Prefetch; }
  bool isPSBHint() const { return Kind == k_PSBHint; }
  bool isShiftExtend() const { return Kind == k_ShiftExtend; }
  bool isShifter() const {
    if (!isShiftExtend())
      return false;

    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    return (ST == AArch64_AM::LSL || ST == AArch64_AM::LSR ||
            ST == AArch64_AM::ASR || ST == AArch64_AM::ROR ||
            ST == AArch64_AM::MSL);
  }
  bool isExtend() const {
    if (!isShiftExtend())
      return false;

    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::UXTB || ET == AArch64_AM::SXTB ||
            ET == AArch64_AM::UXTH || ET == AArch64_AM::SXTH ||
            ET == AArch64_AM::UXTW || ET == AArch64_AM::SXTW ||
            ET == AArch64_AM::UXTX || ET == AArch64_AM::SXTX ||
            ET == AArch64_AM::LSL) &&
           getShiftExtendAmount() <= 4;
  }

  bool isExtend64() const {
    if (!isExtend())
      return false;
    // UXTX and SXTX require a 64-bit source register (the ExtendLSL64 class).
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return ET != AArch64_AM::UXTX && ET != AArch64_AM::SXTX;
  }
  bool isExtendLSL64() const {
    if (!isExtend())
      return false;
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::UXTX || ET == AArch64_AM::SXTX ||
            ET == AArch64_AM::LSL) &&
           getShiftExtendAmount() <= 4;
  }

  template<int Width> bool isMemXExtend() const {
    if (!isExtend())
      return false;
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::LSL || ET == AArch64_AM::SXTX) &&
           (getShiftExtendAmount() == Log2_32(Width / 8) ||
            getShiftExtendAmount() == 0);
  }

  template<int Width> bool isMemWExtend() const {
    if (!isExtend())
      return false;
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::UXTW || ET == AArch64_AM::SXTW) &&
           (getShiftExtendAmount() == Log2_32(Width / 8) ||
            getShiftExtendAmount() == 0);
  }

  template <unsigned width>
  bool isArithmeticShifter() const {
    if (!isShifter())
      return false;

    // An arithmetic shifter is LSL, LSR, or ASR.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    return (ST == AArch64_AM::LSL || ST == AArch64_AM::LSR ||
            ST == AArch64_AM::ASR) && getShiftExtendAmount() < width;
  }

  template <unsigned width>
  bool isLogicalShifter() const {
    if (!isShifter())
      return false;

    // A logical shifter is LSL, LSR, ASR or ROR.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    return (ST == AArch64_AM::LSL || ST == AArch64_AM::LSR ||
            ST == AArch64_AM::ASR || ST == AArch64_AM::ROR) &&
           getShiftExtendAmount() < width;
  }

  bool isMovImm32Shifter() const {
    if (!isShifter())
      return false;

    // A MOVi shifter is LSL of 0, 16, 32, or 48.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    if (ST != AArch64_AM::LSL)
      return false;
    uint64_t Val = getShiftExtendAmount();
    return (Val == 0 || Val == 16);
  }

  bool isMovImm64Shifter() const {
    if (!isShifter())
      return false;

    // A MOVi shifter is LSL of 0 or 16.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    if (ST != AArch64_AM::LSL)
      return false;
    uint64_t Val = getShiftExtendAmount();
    return (Val == 0 || Val == 16 || Val == 32 || Val == 48);
  }

  bool isLogicalVecShifter() const {
    if (!isShifter())
      return false;

    // A logical vector shifter is a left shift by 0, 8, 16, or 24.
    unsigned Shift = getShiftExtendAmount();
    return getShiftExtendType() == AArch64_AM::LSL &&
           (Shift == 0 || Shift == 8 || Shift == 16 || Shift == 24);
  }

  bool isLogicalVecHalfWordShifter() const {
    if (!isLogicalVecShifter())
      return false;

    // A logical vector shifter is a left shift by 0 or 8.
    unsigned Shift = getShiftExtendAmount();
    return getShiftExtendType() == AArch64_AM::LSL &&
           (Shift == 0 || Shift == 8);
  }

  bool isMoveVecShifter() const {
    if (!isShiftExtend())
      return false;

    // A logical vector shifter is a left shift by 8 or 16.
    unsigned Shift = getShiftExtendAmount();
    return getShiftExtendType() == AArch64_AM::MSL &&
           (Shift == 8 || Shift == 16);
  }

  // Fallback unscaled operands are for aliases of LDR/STR that fall back
  // to LDUR/STUR when the offset is not legal for the former but is for
  // the latter. As such, in addition to checking for being a legal unscaled
  // address, also check that it is not a legal scaled address. This avoids
  // ambiguity in the matcher.
  template<int Width>
  bool isSImm9OffsetFB() const {
    return isSImm9() && !isUImm12Offset<Width / 8>();
  }

  bool isAdrpLabel() const {
    // Validation was handled during parsing, so we just sanity check that
    // something didn't go haywire.
    if (!isImm())
        return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val)) {
      int64_t Val = CE->getValue();
      int64_t Offset = Val - Ctx.getBaseAddress();
      int64_t Min = - (4096 * (1LL << (21 - 1)));
      int64_t Max = 4096 * ((1LL << (21 - 1)) - 1);
      return (Val % 4096) == 0 && Offset >= Min && Offset <= Max;
    }

    return true;
  }

  bool isAdrLabel() const {
    // Validation was handled during parsing, so we just sanity check that
    // something didn't go haywire.
    if (!isImm())
        return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val)) {
      int64_t Val = CE->getValue();
      int64_t Min = - (1LL << (21 - 1));
      int64_t Max = ((1LL << (21 - 1)) - 1);
      return Val >= Min && Val <= Max;
    }

    return true;
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediates when possible.  Null MCExpr = 0.
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addGPR32as64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::GPR64RegClassID].contains(getReg()));

    const MCRegisterInfo *RI = Ctx.getRegisterInfo();
    uint32_t Reg = RI->getRegClass(AArch64::GPR32RegClassID).getRegister(
        RI->getEncodingValue(getReg()));

    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void addVectorReg64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::FPR128RegClassID].contains(getReg()));
    Inst.addOperand(MCOperand::createReg(AArch64::D0 + getReg() - AArch64::Q0));
  }

  void addVectorReg128Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::FPR128RegClassID].contains(getReg()));
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addVectorRegLoOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  template <unsigned NumRegs>
  void addVectorList64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    static const unsigned FirstRegs[] = { AArch64::D0,
                                          AArch64::D0_D1,
                                          AArch64::D0_D1_D2,
                                          AArch64::D0_D1_D2_D3 };
    unsigned FirstReg = FirstRegs[NumRegs - 1];

    Inst.addOperand(
        MCOperand::createReg(FirstReg + getVectorListStart() - AArch64::Q0));
  }

  template <unsigned NumRegs>
  void addVectorList128Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    static const unsigned FirstRegs[] = { AArch64::Q0,
                                          AArch64::Q0_Q1,
                                          AArch64::Q0_Q1_Q2,
                                          AArch64::Q0_Q1_Q2_Q3 };
    unsigned FirstReg = FirstRegs[NumRegs - 1];

    Inst.addOperand(
        MCOperand::createReg(FirstReg + getVectorListStart() - AArch64::Q0));
  }

  void addVectorIndex1Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  void addVectorIndexBOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  void addVectorIndexHOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  void addVectorIndexSOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  void addVectorIndexDOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // If this is a pageoff symrefexpr with an addend, adjust the addend
    // to be only the page-offset portion. Otherwise, just add the expr
    // as-is.
    addExpr(Inst, getImm());
  }

  void addAddSubImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    if (isShiftedImm()) {
      addExpr(Inst, getShiftedImmVal());
      Inst.addOperand(MCOperand::createImm(getShiftedImmShift()));
    } else {
      addExpr(Inst, getImm());
      Inst.addOperand(MCOperand::createImm(0));
    }
  }

  void addAddSubImmNegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");

    const MCExpr *MCE = isShiftedImm() ? getShiftedImmVal() : getImm();
    const MCConstantExpr *CE = cast<MCConstantExpr>(MCE);
    int64_t Val = -CE->getValue();
    unsigned ShiftAmt = isShiftedImm() ? ShiftedImm.ShiftAmount : 0;

    Inst.addOperand(MCOperand::createImm(Val));
    Inst.addOperand(MCOperand::createImm(ShiftAmt));
  }

  void addCondCodeOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getCondCode()));
  }

  void addAdrpLabelOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      addExpr(Inst, getImm());
    else
      Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 12));
  }

  void addAdrLabelOperands(MCInst &Inst, unsigned N) const {
    addImmOperands(Inst, N);
  }

  template<int Scale>
  void addUImm12OffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());

    if (!MCE) {
      Inst.addOperand(MCOperand::createExpr(getImm()));
      return;
    }
    Inst.addOperand(MCOperand::createImm(MCE->getValue() / Scale));
  }

  void addSImm9Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addSImm7s4Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue() / 4));
  }

  void addSImm7s8Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue() / 8));
  }

  void addSImm7s16Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue() / 16));
  }

  void addImm0_1Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm0_7Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm1_8Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm0_15Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm1_16Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    assert(MCE && "Invalid constant immediate operand!");
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm0_31Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm1_31Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm1_32Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm0_63Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm1_63Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm1_64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm0_127Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm0_255Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm0_65535Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addImm32_63Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  void addLogicalImm32Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    uint64_t encoding =
        AArch64_AM::encodeLogicalImmediate(MCE->getValue() & 0xFFFFFFFF, 32);
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  void addLogicalImm64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    uint64_t encoding = AArch64_AM::encodeLogicalImmediate(MCE->getValue(), 64);
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  void addLogicalImm32NotOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    int64_t Val = ~MCE->getValue() & 0xFFFFFFFF;
    uint64_t encoding = AArch64_AM::encodeLogicalImmediate(Val, 32);
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  void addLogicalImm64NotOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    uint64_t encoding =
        AArch64_AM::encodeLogicalImmediate(~MCE->getValue(), 64);
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  void addSIMDImmType10Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    uint64_t encoding = AArch64_AM::encodeAdvSIMDModImmType10(MCE->getValue());
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  void addBranchTarget26Operands(MCInst &Inst, unsigned N) const {
    // Branch operands don't encode the low bits, so shift them off
    // here. If it's a label, however, just put it on directly as there's
    // not enough information now to do anything.
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE) {
      addExpr(Inst, getImm());
      return;
    }
    assert(MCE && "Invalid constant immediate operand!");
    Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 2));
  }

  void addPCRelLabel19Operands(MCInst &Inst, unsigned N) const {
    // Branch operands don't encode the low bits, so shift them off
    // here. If it's a label, however, just put it on directly as there's
    // not enough information now to do anything.
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE) {
      addExpr(Inst, getImm());
      return;
    }
    assert(MCE && "Invalid constant immediate operand!");
    Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 2));
  }

  void addBranchTarget14Operands(MCInst &Inst, unsigned N) const {
    // Branch operands don't encode the low bits, so shift them off
    // here. If it's a label, however, just put it on directly as there's
    // not enough information now to do anything.
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE) {
      addExpr(Inst, getImm());
      return;
    }
    assert(MCE && "Invalid constant immediate operand!");
    Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 2));
  }

  void addFPImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getFPImm()));
  }

  void addBarrierOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getBarrier()));
  }

  void addMRSSystemRegisterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.MRSReg));
  }

  void addMSRSystemRegisterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.MSRReg));
  }

  void addSystemPStateFieldWithImm0_1Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.PStateField));
  }

  void addSystemPStateFieldWithImm0_15Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.PStateField));
  }

  void addSysCROperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getSysCR()));
  }

  void addPrefetchOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getPrefetch()));
  }

  void addPSBHintOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getPSBHint()));
  }

  void addShifterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    unsigned Imm =
        AArch64_AM::getShifterImm(getShiftExtendType(), getShiftExtendAmount());
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addExtendOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    if (ET == AArch64_AM::LSL) ET = AArch64_AM::UXTW;
    unsigned Imm = AArch64_AM::getArithExtendImm(ET, getShiftExtendAmount());
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addExtend64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    if (ET == AArch64_AM::LSL) ET = AArch64_AM::UXTX;
    unsigned Imm = AArch64_AM::getArithExtendImm(ET, getShiftExtendAmount());
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addMemExtendOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    bool IsSigned = ET == AArch64_AM::SXTW || ET == AArch64_AM::SXTX;
    Inst.addOperand(MCOperand::createImm(IsSigned));
    Inst.addOperand(MCOperand::createImm(getShiftExtendAmount() != 0));
  }

  // For 8-bit load/store instructions with a register offset, both the
  // "DoShift" and "NoShift" variants have a shift of 0. Because of this,
  // they're disambiguated by whether the shift was explicit or implicit rather
  // than its size.
  void addMemExtend8Operands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    bool IsSigned = ET == AArch64_AM::SXTW || ET == AArch64_AM::SXTX;
    Inst.addOperand(MCOperand::createImm(IsSigned));
    Inst.addOperand(MCOperand::createImm(hasShiftExtendAmount()));
  }

  template<int Shift>
  void addMOVZMovAliasOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    const MCConstantExpr *CE = cast<MCConstantExpr>(getImm());
    uint64_t Value = CE->getValue();
    Inst.addOperand(MCOperand::createImm((Value >> Shift) & 0xffff));
  }

  template<int Shift>
  void addMOVNMovAliasOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    const MCConstantExpr *CE = cast<MCConstantExpr>(getImm());
    uint64_t Value = CE->getValue();
    Inst.addOperand(MCOperand::createImm((~Value >> Shift) & 0xffff));
  }

  void print(raw_ostream &OS) const override;

  static std::unique_ptr<AArch64Operand>
  CreateToken(StringRef Str, bool IsSuffix, SMLoc S, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_Token, Ctx);
    Op->Tok.Data = Str.data();
    Op->Tok.Length = Str.size();
    Op->Tok.IsSuffix = IsSuffix;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateReg(unsigned RegNum, bool isVector, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_Register, Ctx);
    Op->Reg.RegNum = RegNum;
    Op->Reg.isVector = isVector;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateVectorList(unsigned RegNum, unsigned Count, unsigned NumElements,
                   char ElementKind, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_VectorList, Ctx);
    Op->VectorList.RegNum = RegNum;
    Op->VectorList.Count = Count;
    Op->VectorList.NumElements = NumElements;
    Op->VectorList.ElementKind = ElementKind;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateVectorIndex(unsigned Idx, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_VectorIndex, Ctx);
    Op->VectorIndex.Val = Idx;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateImm(const MCExpr *Val, SMLoc S,
                                                   SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_Immediate, Ctx);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateShiftedImm(const MCExpr *Val,
                                                          unsigned ShiftAmount,
                                                          SMLoc S, SMLoc E,
                                                          MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_ShiftedImm, Ctx);
    Op->ShiftedImm .Val = Val;
    Op->ShiftedImm.ShiftAmount = ShiftAmount;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateCondCode(AArch64CC::CondCode Code, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_CondCode, Ctx);
    Op->CondCode.Code = Code;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateFPImm(unsigned Val, SMLoc S,
                                                     MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_FPImm, Ctx);
    Op->FPImm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateBarrier(unsigned Val,
                                                       StringRef Str,
                                                       SMLoc S,
                                                       MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_Barrier, Ctx);
    Op->Barrier.Val = Val;
    Op->Barrier.Data = Str.data();
    Op->Barrier.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateSysReg(StringRef Str, SMLoc S,
                                                      uint32_t MRSReg,
                                                      uint32_t MSRReg,
                                                      uint32_t PStateField,
                                                      MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_SysReg, Ctx);
    Op->SysReg.Data = Str.data();
    Op->SysReg.Length = Str.size();
    Op->SysReg.MRSReg = MRSReg;
    Op->SysReg.MSRReg = MSRReg;
    Op->SysReg.PStateField = PStateField;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateSysCR(unsigned Val, SMLoc S,
                                                     SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_SysCR, Ctx);
    Op->SysCRImm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreatePrefetch(unsigned Val,
                                                        StringRef Str,
                                                        SMLoc S,
                                                        MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_Prefetch, Ctx);
    Op->Prefetch.Val = Val;
    Op->Barrier.Data = Str.data();
    Op->Barrier.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreatePSBHint(unsigned Val,
                                                       StringRef Str,
                                                       SMLoc S,
                                                       MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_PSBHint, Ctx);
    Op->PSBHint.Val = Val;
    Op->PSBHint.Data = Str.data();
    Op->PSBHint.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateShiftExtend(AArch64_AM::ShiftExtendType ShOp, unsigned Val,
                    bool HasExplicitAmount, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_ShiftExtend, Ctx);
    Op->ShiftExtend.Type = ShOp;
    Op->ShiftExtend.Amount = Val;
    Op->ShiftExtend.HasExplicitAmount = HasExplicitAmount;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }
};

} // end anonymous namespace.

void AArch64Operand::print(raw_ostream &OS) const {
  switch (Kind) {
  case k_FPImm:
    OS << "<fpimm " << getFPImm() << "("
       << AArch64_AM::getFPImmFloat(getFPImm()) << ") >";
    break;
  case k_Barrier: {
    StringRef Name = getBarrierName();
    if (!Name.empty())
      OS << "<barrier " << Name << ">";
    else
      OS << "<barrier invalid #" << getBarrier() << ">";
    break;
  }
  case k_Immediate:
    OS << *getImm();
    break;
  case k_ShiftedImm: {
    unsigned Shift = getShiftedImmShift();
    OS << "<shiftedimm ";
    OS << *getShiftedImmVal();
    OS << ", lsl #" << AArch64_AM::getShiftValue(Shift) << ">";
    break;
  }
  case k_CondCode:
    OS << "<condcode " << getCondCode() << ">";
    break;
  case k_Register:
    OS << "<register " << getReg() << ">";
    break;
  case k_VectorList: {
    OS << "<vectorlist ";
    unsigned Reg = getVectorListStart();
    for (unsigned i = 0, e = getVectorListCount(); i != e; ++i)
      OS << Reg + i << " ";
    OS << ">";
    break;
  }
  case k_VectorIndex:
    OS << "<vectorindex " << getVectorIndex() << ">";
    break;
  case k_SysReg:
    OS << "<sysreg: " << getSysReg() << '>';
    break;
  case k_Token:
    OS << "'" << getToken() << "'";
    break;
  case k_SysCR:
    OS << "c" << getSysCR();
    break;
  case k_Prefetch: {
    StringRef Name = getPrefetchName();
    if (!Name.empty())
      OS << "<prfop " << Name << ">";
    else
      OS << "<prfop invalid #" << getPrefetch() << ">";
    break;
  }
  case k_PSBHint: {
    OS << getPSBHintName();
    break;
  }
  case k_ShiftExtend: {
    OS << "<" << AArch64_AM::getShiftExtendName(getShiftExtendType()) << " #"
       << getShiftExtendAmount();
    if (!hasShiftExtendAmount())
      OS << "<imp>";
    OS << '>';
    break;
  }
  }
}

/// @name Auto-generated Match Functions
/// {

static unsigned MatchRegisterName(StringRef Name);

/// }

static unsigned matchVectorRegName(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("v0", AArch64::Q0)
      .Case("v1", AArch64::Q1)
      .Case("v2", AArch64::Q2)
      .Case("v3", AArch64::Q3)
      .Case("v4", AArch64::Q4)
      .Case("v5", AArch64::Q5)
      .Case("v6", AArch64::Q6)
      .Case("v7", AArch64::Q7)
      .Case("v8", AArch64::Q8)
      .Case("v9", AArch64::Q9)
      .Case("v10", AArch64::Q10)
      .Case("v11", AArch64::Q11)
      .Case("v12", AArch64::Q12)
      .Case("v13", AArch64::Q13)
      .Case("v14", AArch64::Q14)
      .Case("v15", AArch64::Q15)
      .Case("v16", AArch64::Q16)
      .Case("v17", AArch64::Q17)
      .Case("v18", AArch64::Q18)
      .Case("v19", AArch64::Q19)
      .Case("v20", AArch64::Q20)
      .Case("v21", AArch64::Q21)
      .Case("v22", AArch64::Q22)
      .Case("v23", AArch64::Q23)
      .Case("v24", AArch64::Q24)
      .Case("v25", AArch64::Q25)
      .Case("v26", AArch64::Q26)
      .Case("v27", AArch64::Q27)
      .Case("v28", AArch64::Q28)
      .Case("v29", AArch64::Q29)
      .Case("v30", AArch64::Q30)
      .Case("v31", AArch64::Q31)
      .Default(0);
}

static bool isValidVectorKind(StringRef Name) {
  return StringSwitch<bool>(Name.lower())
      .Case(".8b", true)
      .Case(".16b", true)
      .Case(".4h", true)
      .Case(".8h", true)
      .Case(".2s", true)
      .Case(".4s", true)
      .Case(".1d", true)
      .Case(".2d", true)
      .Case(".1q", true)
      // Accept the width neutral ones, too, for verbose syntax. If those
      // aren't used in the right places, the token operand won't match so
      // all will work out.
      .Case(".b", true)
      .Case(".h", true)
      .Case(".s", true)
      .Case(".d", true)
      // Needed for fp16 scalar pairwise reductions
      .Case(".2h", true)
      .Default(false);
}

static void parseValidVectorKind(StringRef Name, unsigned &NumElements,
                                 char &ElementKind) {
  assert(isValidVectorKind(Name));

  ElementKind = Name.lower()[Name.size() - 1];
  NumElements = 0;

  if (Name.size() == 2)
    return;

  // Parse the lane count
  Name = Name.drop_front();
  while (isdigit(Name.front())) {
    NumElements = 10 * NumElements + (Name.front() - '0');
    Name = Name.drop_front();
  }
}

bool AArch64AsmParser::ParseRegister(unsigned &RegNo, SMLoc &StartLoc,
                                     SMLoc &EndLoc, unsigned int &ErrorCode) {
  StartLoc = getLoc();
  RegNo = tryParseRegister();
  EndLoc = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  return (RegNo == (unsigned)-1);
}

// Matches a register name or register alias previously defined by '.req'
unsigned AArch64AsmParser::matchRegisterNameAlias(StringRef Name,
                                                  bool isVector) {
  unsigned RegNum = isVector ? matchVectorRegName(Name)
                             : MatchRegisterName(Name);

  if (RegNum == 0) {
    // Check for aliases registered via .req. Canonicalize to lower case.
    // That's more consistent since register names are case insensitive, and
    // it's how the original entry was passed in from MC/MCParser/AsmParser.
    auto Entry = RegisterReqs.find(Name.lower());
    if (Entry == RegisterReqs.end())
      return 0;
    // set RegNum if the match is the right kind of register
    if (isVector == Entry->getValue().first)
      RegNum = Entry->getValue().second;
  }
  return RegNum;
}

/// tryParseRegister - Try to parse a register name. The token must be an
/// Identifier when called, and if it is a register name the token is eaten and
/// the register is added to the operand list.
int AArch64AsmParser::tryParseRegister() {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  assert(Tok.is(AsmToken::Identifier) && "Token is not an Identifier");

  std::string lowerCase = Tok.getString().lower();
  unsigned RegNum = matchRegisterNameAlias(lowerCase, false);
  // Also handle a few aliases of registers.
  if (RegNum == 0)
    RegNum = StringSwitch<unsigned>(lowerCase)
                 .Case("fp",  AArch64::FP)
                 .Case("lr",  AArch64::LR)
                 .Case("x31", AArch64::XZR)
                 .Case("w31", AArch64::WZR)
                 .Default(0);

  if (RegNum == 0)
    return -1;

  Parser.Lex(); // Eat identifier token.
  return RegNum;
}

/// tryMatchVectorRegister - Try to parse a vector register name with optional
/// kind specifier. If it is a register specifier, eat the token and return it.
int AArch64AsmParser::tryMatchVectorRegister(StringRef &Kind, bool expected)
{
  MCAsmParser &Parser = getParser();
  if (Parser.getTok().isNot(AsmToken::Identifier)) {
    //TokError("vector register expected");
    return -1;
  }

  StringRef Name = Parser.getTok().getString();
  // If there is a kind specifier, it's separated from the register name by
  // a '.'.
  size_t Start = 0, Next = Name.find('.');
  StringRef Head = Name.slice(Start, Next);
  unsigned RegNum = matchRegisterNameAlias(Head, true);

  if (RegNum) {
    if (Next != StringRef::npos) {
      Kind = Name.slice(Next, StringRef::npos);
      if (!isValidVectorKind(Kind)) {
        //TokError("invalid vector kind qualifier");
        return -1;
      }
    }
    Parser.Lex(); // Eat the register token.
    return RegNum;
  }

  //if (expected)
  //  TokError("vector register expected");
  return -1;
}

/// tryParseSysCROperand - Try to parse a system instruction CR operand name.
AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParseSysCROperand(OperandVector &Operands)
{
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();

  if (Parser.getTok().isNot(AsmToken::Identifier)) {
    //Error(S, "Expected cN operand where 0 <= N <= 15");
    return MatchOperand_ParseFail;
  }

  StringRef Tok = Parser.getTok().getIdentifier();
  if (Tok[0] != 'c' && Tok[0] != 'C') {
    //Error(S, "Expected cN operand where 0 <= N <= 15");
    return MatchOperand_ParseFail;
  }

  uint32_t CRNum;
  bool BadNum = Tok.drop_front().getAsInteger(10, CRNum);
  if (BadNum || CRNum > 15) {
    //Error(S, "Expected cN operand where 0 <= N <= 15");
    return MatchOperand_ParseFail;
  }

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(
      AArch64Operand::CreateSysCR(CRNum, S, getLoc(), getContext()));
  return MatchOperand_Success;
}

/// tryParsePrefetch - Try to parse a prefetch operand.
AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParsePrefetch(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  const AsmToken &Tok = Parser.getTok();
  // Either an identifier for named values or a 5-bit immediate.
  bool Hash = Tok.is(AsmToken::Hash);
  if (Hash || Tok.is(AsmToken::Integer)) {
    if (Hash)
      Parser.Lex(); // Eat hash token.
    const MCExpr *ImmVal;
    if (getParser().parseExpression(ImmVal))
      return MatchOperand_ParseFail;

    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE) {
      //TokError("immediate value expected for prefetch operand");
      return MatchOperand_ParseFail;
    }
    unsigned prfop = MCE->getValue();
    if (prfop > 31) {
      //TokError("prefetch operand out of range, [0,31] expected");
      return MatchOperand_ParseFail;
    }

    bool Valid;
    auto Mapper = AArch64PRFM::PRFMMapper();
    StringRef Name = 
        Mapper.toString(MCE->getValue(), getSTI().getFeatureBits(), Valid);
    Operands.push_back(AArch64Operand::CreatePrefetch(prfop, Name,
                                                      S, getContext()));
    return MatchOperand_Success;
  }

  if (Tok.isNot(AsmToken::Identifier)) {
    //TokError("pre-fetch hint expected");
    return MatchOperand_ParseFail;
  }

  bool Valid;
  auto Mapper = AArch64PRFM::PRFMMapper();
  unsigned prfop = 
      Mapper.fromString(Tok.getString(), getSTI().getFeatureBits(), Valid);
  if (!Valid) {
    //TokError("pre-fetch hint expected");
    return MatchOperand_ParseFail;
  }

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(AArch64Operand::CreatePrefetch(prfop, Tok.getString(),
                                                    S, getContext()));
  return MatchOperand_Success;
}

/// tryParsePSBHint - Try to parse a PSB operand, mapped to Hint command
AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParsePSBHint(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier)) {
    //TokError("invalid operand for instruction");
    return MatchOperand_ParseFail;
  }

  bool Valid;
  auto Mapper = AArch64PSBHint::PSBHintMapper();
  unsigned psbhint =
      Mapper.fromString(Tok.getString(), getSTI().getFeatureBits(), Valid);
  if (!Valid) {
    //TokError("invalid operand for instruction");
    return MatchOperand_ParseFail;
  }

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(AArch64Operand::CreatePSBHint(psbhint, Tok.getString(),
                                                   S, getContext()));
  return MatchOperand_Success;
}

/// tryParseAdrpLabel - Parse and validate a source label for the ADRP
/// instruction.
AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParseAdrpLabel(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  const MCExpr *Expr;

  if (Parser.getTok().is(AsmToken::Hash)) {
    Parser.Lex(); // Eat hash token.
  }

  if (parseSymbolicImmVal(Expr))
    return MatchOperand_ParseFail;

  AArch64MCExpr::VariantKind ELFRefKind;
  MCSymbolRefExpr::VariantKind DarwinRefKind;
  int64_t Addend;
  if (classifySymbolRef(Expr, ELFRefKind, DarwinRefKind, Addend)) {
    if (DarwinRefKind == MCSymbolRefExpr::VK_None &&
        ELFRefKind == AArch64MCExpr::VK_INVALID) {
      // No modifier was specified at all; this is the syntax for an ELF basic
      // ADRP relocation (unfortunately).
      Expr =
          AArch64MCExpr::create(Expr, AArch64MCExpr::VK_ABS_PAGE, getContext());
    } else if ((DarwinRefKind == MCSymbolRefExpr::VK_GOTPAGE ||
                DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGE) &&
               Addend != 0) {
      //Error(S, "gotpage label reference not allowed an addend");
      return MatchOperand_ParseFail;
    } else if (DarwinRefKind != MCSymbolRefExpr::VK_PAGE &&
               DarwinRefKind != MCSymbolRefExpr::VK_GOTPAGE &&
               DarwinRefKind != MCSymbolRefExpr::VK_TLVPPAGE &&
               ELFRefKind != AArch64MCExpr::VK_GOT_PAGE &&
               ELFRefKind != AArch64MCExpr::VK_GOTTPREL_PAGE &&
               ELFRefKind != AArch64MCExpr::VK_TLSDESC_PAGE) {
      // The operand must be an @page or @gotpage qualified symbolref.
      //Error(S, "page or gotpage label reference expected");
      return MatchOperand_ParseFail;
    }
  }

  // We have either a label reference possibly with addend or an immediate. The
  // addend is a raw value here. The linker will adjust it to only reference the
  // page.
  SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  Operands.push_back(AArch64Operand::CreateImm(Expr, S, E, getContext()));

  return MatchOperand_Success;
}

/// tryParseAdrLabel - Parse and validate a source label for the ADR
/// instruction.
AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParseAdrLabel(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  const MCExpr *Expr;

  if (Parser.getTok().is(AsmToken::Hash)) {
    Parser.Lex(); // Eat hash token.
  }

  if (getParser().parseExpression(Expr))
    return MatchOperand_ParseFail;

  SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  Operands.push_back(AArch64Operand::CreateImm(Expr, S, E, getContext()));

  return MatchOperand_Success;
}

/// tryParseFPImm - A floating point immediate expression operand.
AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParseFPImm(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();

  bool Hash = false;
  if (Parser.getTok().is(AsmToken::Hash)) {
    Parser.Lex(); // Eat '#'
    Hash = true;
  }

  // Handle negation, as that still comes through as a separate token.
  bool isNegative = false;
  if (Parser.getTok().is(AsmToken::Minus)) {
    isNegative = true;
    Parser.Lex();
  }
  const AsmToken &Tok = Parser.getTok();
  if (Tok.is(AsmToken::Real)) {
    APFloat RealVal(APFloat::IEEEdouble, Tok.getString());
    if (isNegative)
      RealVal.changeSign();

    if (RealVal.bitcastToAPInt().getActiveBits() > 64)
        return MatchOperand_ParseFail;
    uint64_t IntVal = RealVal.bitcastToAPInt().getZExtValue();
    int Val = AArch64_AM::getFP64Imm(APInt(64, IntVal));
    Parser.Lex(); // Eat the token.
    // Check for out of range values. As an exception, we let Zero through,
    // as we handle that special case in post-processing before matching in
    // order to use the zero register for it.
    if (Val == -1 && !RealVal.isPosZero()) {
      //TokError("expected compatible register or floating-point constant");
      return MatchOperand_ParseFail;
    }
    Operands.push_back(AArch64Operand::CreateFPImm(Val, S, getContext()));
    return MatchOperand_Success;
  }
  if (Tok.is(AsmToken::Integer)) {
    int64_t Val;
    if (!isNegative && Tok.getString().startswith("0x")) {
      bool valid;
      Val = Tok.getIntVal(valid);
      if (!valid)
        return MatchOperand_ParseFail;
      if (Val > 255 || Val < 0) {
        //TokError("encoded floating point value out of range");
        return MatchOperand_ParseFail;
      }
    } else {
      APFloat RealVal(APFloat::IEEEdouble, Tok.getString());
      if (RealVal.bitcastToAPInt().getActiveBits() > 64)
          return MatchOperand_ParseFail;
      uint64_t IntVal = RealVal.bitcastToAPInt().getZExtValue();
      // If we had a '-' in front, toggle the sign bit.
      IntVal ^= (uint64_t)isNegative << 63;
      Val = AArch64_AM::getFP64Imm(APInt(64, IntVal));
    }
    Parser.Lex(); // Eat the token.
    Operands.push_back(AArch64Operand::CreateFPImm(Val, S, getContext()));
    return MatchOperand_Success;
  }

  if (!Hash)
    return MatchOperand_NoMatch;

  //TokError("invalid floating point immediate");
  return MatchOperand_ParseFail;
}

/// tryParseAddSubImm - Parse ADD/SUB shifted immediate operand
AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParseAddSubImm(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();

  if (Parser.getTok().is(AsmToken::Hash))
    Parser.Lex(); // Eat '#'
  else if (Parser.getTok().isNot(AsmToken::Integer))
    // Operand should start from # or should be integer, emit error otherwise.
    return MatchOperand_NoMatch;

  const MCExpr *Imm;
  if (parseSymbolicImmVal(Imm))
    return MatchOperand_ParseFail;
  else if (Parser.getTok().isNot(AsmToken::Comma)) {
    uint64_t ShiftAmount = 0;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(Imm);
    if (MCE) {
      int64_t Val = MCE->getValue();
      if (Val > 0xfff && (Val & 0xfff) == 0) {
        Imm = MCConstantExpr::create(Val >> 12, getContext());
        ShiftAmount = 12;
      }
    }
    SMLoc E = Parser.getTok().getLoc();
    Operands.push_back(AArch64Operand::CreateShiftedImm(Imm, ShiftAmount, S, E,
                                                        getContext()));
    return MatchOperand_Success;
  }

  // Eat ','
  Parser.Lex();

  // The optional operand must be "lsl #N" where N is non-negative.
  if (!Parser.getTok().is(AsmToken::Identifier) ||
      !Parser.getTok().getIdentifier().equals_lower("lsl")) {
    //Error(Parser.getTok().getLoc(), "only 'lsl #+N' valid after immediate");
    return MatchOperand_ParseFail;
  }

  // Eat 'lsl'
  Parser.Lex();

  if (Parser.getTok().is(AsmToken::Hash)) {
    Parser.Lex();
  }

  if (Parser.getTok().isNot(AsmToken::Integer)) {
    //Error(Parser.getTok().getLoc(), "only 'lsl #+N' valid after immediate");
    return MatchOperand_ParseFail;
  }

  bool valid;
  int64_t ShiftAmount = Parser.getTok().getIntVal(valid);
  if (!valid)
    return MatchOperand_ParseFail;

  if (ShiftAmount < 0) {
    //Error(Parser.getTok().getLoc(), "positive shift amount required");
    return MatchOperand_ParseFail;
  }
  Parser.Lex(); // Eat the number

  SMLoc E = Parser.getTok().getLoc();
  Operands.push_back(AArch64Operand::CreateShiftedImm(Imm, ShiftAmount,
                                                      S, E, getContext()));
  return MatchOperand_Success;
}

/// parseCondCodeString - Parse a Condition Code string.
AArch64CC::CondCode AArch64AsmParser::parseCondCodeString(StringRef Cond) {
  AArch64CC::CondCode CC = StringSwitch<AArch64CC::CondCode>(Cond.lower())
                    .Case("eq", AArch64CC::EQ)
                    .Case("ne", AArch64CC::NE)
                    .Case("cs", AArch64CC::HS)
                    .Case("hs", AArch64CC::HS)
                    .Case("cc", AArch64CC::LO)
                    .Case("lo", AArch64CC::LO)
                    .Case("mi", AArch64CC::MI)
                    .Case("pl", AArch64CC::PL)
                    .Case("vs", AArch64CC::VS)
                    .Case("vc", AArch64CC::VC)
                    .Case("hi", AArch64CC::HI)
                    .Case("ls", AArch64CC::LS)
                    .Case("ge", AArch64CC::GE)
                    .Case("lt", AArch64CC::LT)
                    .Case("gt", AArch64CC::GT)
                    .Case("le", AArch64CC::LE)
                    .Case("al", AArch64CC::AL)
                    .Case("nv", AArch64CC::NV)
                    .Default(AArch64CC::Invalid);
  return CC;
}

/// parseCondCode - Parse a Condition Code operand.
bool AArch64AsmParser::parseCondCode(OperandVector &Operands,
                                     bool invertCondCode) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  const AsmToken &Tok = Parser.getTok();
  assert(Tok.is(AsmToken::Identifier) && "Token is not an Identifier");

  StringRef Cond = Tok.getString();
  AArch64CC::CondCode CC = parseCondCodeString(Cond);
  if (CC == AArch64CC::Invalid)
    //return TokError("invalid condition code");
    return true;
  Parser.Lex(); // Eat identifier token.

  if (invertCondCode) {
    if (CC == AArch64CC::AL || CC == AArch64CC::NV)
      //return TokError("condition codes AL and NV are invalid for this instruction");
      return true;
    CC = AArch64CC::getInvertedCondCode(AArch64CC::CondCode(CC));
  }

  Operands.push_back(
      AArch64Operand::CreateCondCode(CC, S, getLoc(), getContext()));
  return false;
}

/// tryParseOptionalShift - Some operands take an optional shift argument. Parse
/// them if present.
AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParseOptionalShiftExtend(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  std::string LowerID = Tok.getString().lower();
  AArch64_AM::ShiftExtendType ShOp =
      StringSwitch<AArch64_AM::ShiftExtendType>(LowerID)
          .Case("lsl", AArch64_AM::LSL)
          .Case("lsr", AArch64_AM::LSR)
          .Case("asr", AArch64_AM::ASR)
          .Case("ror", AArch64_AM::ROR)
          .Case("msl", AArch64_AM::MSL)
          .Case("uxtb", AArch64_AM::UXTB)
          .Case("uxth", AArch64_AM::UXTH)
          .Case("uxtw", AArch64_AM::UXTW)
          .Case("uxtx", AArch64_AM::UXTX)
          .Case("sxtb", AArch64_AM::SXTB)
          .Case("sxth", AArch64_AM::SXTH)
          .Case("sxtw", AArch64_AM::SXTW)
          .Case("sxtx", AArch64_AM::SXTX)
          .Default(AArch64_AM::InvalidShiftExtend);

  if (ShOp == AArch64_AM::InvalidShiftExtend)
    return MatchOperand_NoMatch;

  SMLoc S = Tok.getLoc();
  Parser.Lex();

  bool Hash = getLexer().is(AsmToken::Hash);
  if (!Hash && getLexer().isNot(AsmToken::Integer)) {
    if (ShOp == AArch64_AM::LSL || ShOp == AArch64_AM::LSR ||
        ShOp == AArch64_AM::ASR || ShOp == AArch64_AM::ROR ||
        ShOp == AArch64_AM::MSL) {
      // We expect a number here.
      //TokError("expected #imm after shift specifier");
      return MatchOperand_ParseFail;
    }

    // "extend" type operatoins don't need an immediate, #0 is implicit.
    SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(
        AArch64Operand::CreateShiftExtend(ShOp, 0, false, S, E, getContext()));
    return MatchOperand_Success;
  }

  if (Hash)
    Parser.Lex(); // Eat the '#'.

  // Make sure we do actually have a number or a parenthesized expression.
  SMLoc E = Parser.getTok().getLoc();
  if (!Parser.getTok().is(AsmToken::Integer) &&
      !Parser.getTok().is(AsmToken::LParen)) {
    //Error(E, "expected integer shift amount");
    return MatchOperand_ParseFail;
  }

  const MCExpr *ImmVal;
  if (getParser().parseExpression(ImmVal))
    return MatchOperand_ParseFail;

  const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
  if (!MCE) {
    //Error(E, "expected constant '#imm' after shift specifier");
    return MatchOperand_ParseFail;
  }

  E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  Operands.push_back(AArch64Operand::CreateShiftExtend(
      ShOp, MCE->getValue(), true, S, E, getContext()));
  return MatchOperand_Success;
}

/// parseSysAlias - The IC, DC, AT, and TLBI instructions are simple aliases for
/// the SYS instruction. Parse them specially so that we create a SYS MCInst.
bool AArch64AsmParser::parseSysAlias(StringRef Name, SMLoc NameLoc,
                                   OperandVector &Operands)
{
  if (Name.find('.') != StringRef::npos)
    //return TokError("invalid operand");
    return true;

  Mnemonic = Name;
  Operands.push_back(
      AArch64Operand::CreateToken("sys", false, NameLoc, getContext()));

  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  StringRef Op = Tok.getString();
  SMLoc S = Tok.getLoc();

  const MCExpr *Expr = nullptr;

#define SYS_ALIAS(op1, Cn, Cm, op2)                                            \
  do {                                                                         \
    Expr = MCConstantExpr::create(op1, getContext());                          \
    Operands.push_back(                                                        \
        AArch64Operand::CreateImm(Expr, S, getLoc(), getContext()));           \
    Operands.push_back(                                                        \
        AArch64Operand::CreateSysCR(Cn, S, getLoc(), getContext()));           \
    Operands.push_back(                                                        \
        AArch64Operand::CreateSysCR(Cm, S, getLoc(), getContext()));           \
    Expr = MCConstantExpr::create(op2, getContext());                          \
    Operands.push_back(                                                        \
        AArch64Operand::CreateImm(Expr, S, getLoc(), getContext()));           \
  } while (0)

  if (Mnemonic == "ic") {
    if (!Op.compare_lower("ialluis")) {
      // SYS #0, C7, C1, #0
      SYS_ALIAS(0, 7, 1, 0);
    } else if (!Op.compare_lower("iallu")) {
      // SYS #0, C7, C5, #0
      SYS_ALIAS(0, 7, 5, 0);
    } else if (!Op.compare_lower("ivau")) {
      // SYS #3, C7, C5, #1
      SYS_ALIAS(3, 7, 5, 1);
    } else {
      //return TokError("invalid operand for IC instruction");
      return true;
    }
  } else if (Mnemonic == "dc") {
    if (!Op.compare_lower("zva")) {
      // SYS #3, C7, C4, #1
      SYS_ALIAS(3, 7, 4, 1);
    } else if (!Op.compare_lower("ivac")) {
      // SYS #3, C7, C6, #1
      SYS_ALIAS(0, 7, 6, 1);
    } else if (!Op.compare_lower("isw")) {
      // SYS #0, C7, C6, #2
      SYS_ALIAS(0, 7, 6, 2);
    } else if (!Op.compare_lower("cvac")) {
      // SYS #3, C7, C10, #1
      SYS_ALIAS(3, 7, 10, 1);
    } else if (!Op.compare_lower("csw")) {
      // SYS #0, C7, C10, #2
      SYS_ALIAS(0, 7, 10, 2);
    } else if (!Op.compare_lower("cvau")) {
      // SYS #3, C7, C11, #1
      SYS_ALIAS(3, 7, 11, 1);
    } else if (!Op.compare_lower("civac")) {
      // SYS #3, C7, C14, #1
      SYS_ALIAS(3, 7, 14, 1);
    } else if (!Op.compare_lower("cisw")) {
      // SYS #0, C7, C14, #2
      SYS_ALIAS(0, 7, 14, 2);
    } else if (!Op.compare_lower("cvap")) {
      if (getSTI().getFeatureBits()[AArch64::HasV8_2aOps]) {
        // SYS #3, C7, C12, #1
        SYS_ALIAS(3, 7, 12, 1);
      } else {
        //return TokError("DC CVAP requires ARMv8.2a");
        return true;
      }
    } else {
      //return TokError("invalid operand for DC instruction");
      return true;
    }
  } else if (Mnemonic == "at") {
    if (!Op.compare_lower("s1e1r")) {
      // SYS #0, C7, C8, #0
      SYS_ALIAS(0, 7, 8, 0);
    } else if (!Op.compare_lower("s1e2r")) {
      // SYS #4, C7, C8, #0
      SYS_ALIAS(4, 7, 8, 0);
    } else if (!Op.compare_lower("s1e3r")) {
      // SYS #6, C7, C8, #0
      SYS_ALIAS(6, 7, 8, 0);
    } else if (!Op.compare_lower("s1e1w")) {
      // SYS #0, C7, C8, #1
      SYS_ALIAS(0, 7, 8, 1);
    } else if (!Op.compare_lower("s1e2w")) {
      // SYS #4, C7, C8, #1
      SYS_ALIAS(4, 7, 8, 1);
    } else if (!Op.compare_lower("s1e3w")) {
      // SYS #6, C7, C8, #1
      SYS_ALIAS(6, 7, 8, 1);
    } else if (!Op.compare_lower("s1e0r")) {
      // SYS #0, C7, C8, #3
      SYS_ALIAS(0, 7, 8, 2);
    } else if (!Op.compare_lower("s1e0w")) {
      // SYS #0, C7, C8, #3
      SYS_ALIAS(0, 7, 8, 3);
    } else if (!Op.compare_lower("s12e1r")) {
      // SYS #4, C7, C8, #4
      SYS_ALIAS(4, 7, 8, 4);
    } else if (!Op.compare_lower("s12e1w")) {
      // SYS #4, C7, C8, #5
      SYS_ALIAS(4, 7, 8, 5);
    } else if (!Op.compare_lower("s12e0r")) {
      // SYS #4, C7, C8, #6
      SYS_ALIAS(4, 7, 8, 6);
    } else if (!Op.compare_lower("s12e0w")) {
      // SYS #4, C7, C8, #7
      SYS_ALIAS(4, 7, 8, 7);
    } else if (!Op.compare_lower("s1e1rp")) {
      if (getSTI().getFeatureBits()[AArch64::HasV8_2aOps]) {
        // SYS #0, C7, C9, #0
        SYS_ALIAS(0, 7, 9, 0);
      } else {
        //return TokError("AT S1E1RP requires ARMv8.2a");
        return true;
      }
    } else if (!Op.compare_lower("s1e1wp")) {
      if (getSTI().getFeatureBits()[AArch64::HasV8_2aOps]) {
        // SYS #0, C7, C9, #1
        SYS_ALIAS(0, 7, 9, 1);
      } else {
        //return TokError("AT S1E1WP requires ARMv8.2a");
        return true;
      }
    } else {
      //return TokError("invalid operand for AT instruction");
      return true;
    }
  } else if (Mnemonic == "tlbi") {
    if (!Op.compare_lower("vmalle1is")) {
      // SYS #0, C8, C3, #0
      SYS_ALIAS(0, 8, 3, 0);
    } else if (!Op.compare_lower("alle2is")) {
      // SYS #4, C8, C3, #0
      SYS_ALIAS(4, 8, 3, 0);
    } else if (!Op.compare_lower("alle3is")) {
      // SYS #6, C8, C3, #0
      SYS_ALIAS(6, 8, 3, 0);
    } else if (!Op.compare_lower("vae1is")) {
      // SYS #0, C8, C3, #1
      SYS_ALIAS(0, 8, 3, 1);
    } else if (!Op.compare_lower("vae2is")) {
      // SYS #4, C8, C3, #1
      SYS_ALIAS(4, 8, 3, 1);
    } else if (!Op.compare_lower("vae3is")) {
      // SYS #6, C8, C3, #1
      SYS_ALIAS(6, 8, 3, 1);
    } else if (!Op.compare_lower("aside1is")) {
      // SYS #0, C8, C3, #2
      SYS_ALIAS(0, 8, 3, 2);
    } else if (!Op.compare_lower("vaae1is")) {
      // SYS #0, C8, C3, #3
      SYS_ALIAS(0, 8, 3, 3);
    } else if (!Op.compare_lower("alle1is")) {
      // SYS #4, C8, C3, #4
      SYS_ALIAS(4, 8, 3, 4);
    } else if (!Op.compare_lower("vale1is")) {
      // SYS #0, C8, C3, #5
      SYS_ALIAS(0, 8, 3, 5);
    } else if (!Op.compare_lower("vaale1is")) {
      // SYS #0, C8, C3, #7
      SYS_ALIAS(0, 8, 3, 7);
    } else if (!Op.compare_lower("vmalle1")) {
      // SYS #0, C8, C7, #0
      SYS_ALIAS(0, 8, 7, 0);
    } else if (!Op.compare_lower("alle2")) {
      // SYS #4, C8, C7, #0
      SYS_ALIAS(4, 8, 7, 0);
    } else if (!Op.compare_lower("vale2is")) {
      // SYS #4, C8, C3, #5
      SYS_ALIAS(4, 8, 3, 5);
    } else if (!Op.compare_lower("vale3is")) {
      // SYS #6, C8, C3, #5
      SYS_ALIAS(6, 8, 3, 5);
    } else if (!Op.compare_lower("alle3")) {
      // SYS #6, C8, C7, #0
      SYS_ALIAS(6, 8, 7, 0);
    } else if (!Op.compare_lower("vae1")) {
      // SYS #0, C8, C7, #1
      SYS_ALIAS(0, 8, 7, 1);
    } else if (!Op.compare_lower("vae2")) {
      // SYS #4, C8, C7, #1
      SYS_ALIAS(4, 8, 7, 1);
    } else if (!Op.compare_lower("vae3")) {
      // SYS #6, C8, C7, #1
      SYS_ALIAS(6, 8, 7, 1);
    } else if (!Op.compare_lower("aside1")) {
      // SYS #0, C8, C7, #2
      SYS_ALIAS(0, 8, 7, 2);
    } else if (!Op.compare_lower("vaae1")) {
      // SYS #0, C8, C7, #3
      SYS_ALIAS(0, 8, 7, 3);
    } else if (!Op.compare_lower("alle1")) {
      // SYS #4, C8, C7, #4
      SYS_ALIAS(4, 8, 7, 4);
    } else if (!Op.compare_lower("vale1")) {
      // SYS #0, C8, C7, #5
      SYS_ALIAS(0, 8, 7, 5);
    } else if (!Op.compare_lower("vale2")) {
      // SYS #4, C8, C7, #5
      SYS_ALIAS(4, 8, 7, 5);
    } else if (!Op.compare_lower("vale3")) {
      // SYS #6, C8, C7, #5
      SYS_ALIAS(6, 8, 7, 5);
    } else if (!Op.compare_lower("vaale1")) {
      // SYS #0, C8, C7, #7
      SYS_ALIAS(0, 8, 7, 7);
    } else if (!Op.compare_lower("ipas2e1")) {
      // SYS #4, C8, C4, #1
      SYS_ALIAS(4, 8, 4, 1);
    } else if (!Op.compare_lower("ipas2le1")) {
      // SYS #4, C8, C4, #5
      SYS_ALIAS(4, 8, 4, 5);
    } else if (!Op.compare_lower("ipas2e1is")) {
      // SYS #4, C8, C4, #1
      SYS_ALIAS(4, 8, 0, 1);
    } else if (!Op.compare_lower("ipas2le1is")) {
      // SYS #4, C8, C4, #5
      SYS_ALIAS(4, 8, 0, 5);
    } else if (!Op.compare_lower("vmalls12e1")) {
      // SYS #4, C8, C7, #6
      SYS_ALIAS(4, 8, 7, 6);
    } else if (!Op.compare_lower("vmalls12e1is")) {
      // SYS #4, C8, C3, #6
      SYS_ALIAS(4, 8, 3, 6);
    } else {
      //return TokError("invalid operand for TLBI instruction");
      return true;
    }
  }

#undef SYS_ALIAS

  Parser.Lex(); // Eat operand.

  bool ExpectRegister = (Op.lower().find("all") == StringRef::npos);
  bool HasRegister = false;

  // Check for the optional register operand.
  if (getLexer().is(AsmToken::Comma)) {
    Parser.Lex(); // Eat comma.

    if (Tok.isNot(AsmToken::Identifier) || parseRegister(Operands))
      //return TokError("expected register operand");
      return true;

    HasRegister = true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    Parser.eatToEndOfStatement();
    //return TokError("unexpected token in argument list");
    return true;
  }

  if (ExpectRegister && !HasRegister) {
    //return TokError("specified " + Mnemonic + " op requires a register");
    return true;
  }
  else if (!ExpectRegister && HasRegister) {
    //return TokError("specified " + Mnemonic + " op does not use a register");
    return true;
  }

  Parser.Lex(); // Consume the EndOfStatement
  return false;
}

AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParseBarrierOperand(OperandVector &Operands)
{
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();

  // Can be either a #imm style literal or an option name
  bool Hash = Tok.is(AsmToken::Hash);
  if (Hash || Tok.is(AsmToken::Integer)) {
    // Immediate operand.
    if (Hash)
      Parser.Lex(); // Eat the '#'
    const MCExpr *ImmVal;
    SMLoc ExprLoc = getLoc();
    if (getParser().parseExpression(ImmVal))
      return MatchOperand_ParseFail;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE) {
      //Error(ExprLoc, "immediate value expected for barrier operand");
      return MatchOperand_ParseFail;
    }
    if (MCE->getValue() < 0 || MCE->getValue() > 15) {
      //Error(ExprLoc, "barrier operand out of range");
      return MatchOperand_ParseFail;
    }
    bool Valid;
    auto Mapper = AArch64DB::DBarrierMapper();
    StringRef Name = 
        Mapper.toString(MCE->getValue(), getSTI().getFeatureBits(), Valid);
    Operands.push_back( AArch64Operand::CreateBarrier(MCE->getValue(), Name,
                                                      ExprLoc, getContext()));
    return MatchOperand_Success;
  }

  if (Tok.isNot(AsmToken::Identifier)) {
    //TokError("invalid operand for instruction");
    return MatchOperand_ParseFail;
  }

  bool Valid;
  auto Mapper = AArch64DB::DBarrierMapper();
  unsigned Opt = 
      Mapper.fromString(Tok.getString(), getSTI().getFeatureBits(), Valid);
  if (!Valid) {
    //TokError("invalid barrier option name");
    return MatchOperand_ParseFail;
  }

  // The only valid named option for ISB is 'sy'
  if (Mnemonic == "isb" && Opt != AArch64DB::SY) {
    //TokError("'sy' or #imm operand expected");
    return MatchOperand_ParseFail;
  }

  Operands.push_back( AArch64Operand::CreateBarrier(Opt, Tok.getString(),
                                                    getLoc(), getContext()));
  Parser.Lex(); // Consume the option

  return MatchOperand_Success;
}

AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParseSysReg(OperandVector &Operands)
{
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();

  if (Tok.isNot(AsmToken::Identifier))
    return MatchOperand_NoMatch;

  bool IsKnown;
  auto MRSMapper = AArch64SysReg::MRSMapper();
  uint32_t MRSReg = MRSMapper.fromString(Tok.getString(),
                                         getSTI().getFeatureBits(), IsKnown);
  assert(IsKnown == (MRSReg != -1U) &&
         "register should be -1 if and only if it's unknown");

  auto MSRMapper = AArch64SysReg::MSRMapper();
  uint32_t MSRReg = MSRMapper.fromString(Tok.getString(),
                                         getSTI().getFeatureBits(), IsKnown);
  assert(IsKnown == (MSRReg != -1U) &&
         "register should be -1 if and only if it's unknown");

  auto PStateMapper = AArch64PState::PStateMapper();
  uint32_t PStateField = 
      PStateMapper.fromString(Tok.getString(),
                              getSTI().getFeatureBits(), IsKnown);
  assert(IsKnown == (PStateField != -1U) &&
         "register should be -1 if and only if it's unknown");

  Operands.push_back(AArch64Operand::CreateSysReg(
      Tok.getString(), getLoc(), MRSReg, MSRReg, PStateField, getContext()));
  Parser.Lex(); // Eat identifier

  return MatchOperand_Success;
}

/// tryParseVectorRegister - Parse a vector register operand.
// return true on error
bool AArch64AsmParser::tryParseVectorRegister(OperandVector &Operands)
{
  MCAsmParser &Parser = getParser();
  if (Parser.getTok().isNot(AsmToken::Identifier))
    return true;

  SMLoc S = getLoc();
  // Check for a vector register specifier first.
  StringRef Kind;
  int64_t Reg = tryMatchVectorRegister(Kind, false);
  if (Reg == -1)
    return true;
  Operands.push_back(
      AArch64Operand::CreateReg(Reg, true, S, getLoc(), getContext()));
  // If there was an explicit qualifier, that goes on as a literal text
  // operand.
  if (!Kind.empty())
    Operands.push_back(
        AArch64Operand::CreateToken(Kind, false, S, getContext()));

  // If there is an index specifier following the register, parse that too.
  if (Parser.getTok().is(AsmToken::LBrac)) {
    SMLoc SIdx = getLoc();
    Parser.Lex(); // Eat left bracket token.

    const MCExpr *ImmVal;
    if (getParser().parseExpression(ImmVal))
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE) {
      //TokError("immediate value expected for vector index");
      return true;
    }

    SMLoc E = getLoc();
    if (Parser.getTok().isNot(AsmToken::RBrac)) {
      //Error(E, "']' expected");
      return true;
    }

    Parser.Lex(); // Eat right bracket token.

    Operands.push_back(AArch64Operand::CreateVectorIndex(MCE->getValue(), SIdx,
                                                         E, getContext()));
  }

  return false;
}

/// parseRegister - Parse a non-vector register operand.
// return true on error
bool AArch64AsmParser::parseRegister(OperandVector &Operands)
{
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  // Try for a vector register.
  if (!tryParseVectorRegister(Operands))
    return false;

  // Try for a scalar register.
  int64_t Reg = tryParseRegister();
  if (Reg == -1)
    return true;
  Operands.push_back(
      AArch64Operand::CreateReg(Reg, false, S, getLoc(), getContext()));

  // A small number of instructions (FMOVXDhighr, for example) have "[1]"
  // as a string token in the instruction itself.
  if (getLexer().getKind() == AsmToken::LBrac) {
    SMLoc LBracS = getLoc();
    Parser.Lex();
    const AsmToken &Tok = Parser.getTok();
    if (Tok.is(AsmToken::Integer)) {
      SMLoc IntS = getLoc();
      bool valid;
      int64_t Val = Tok.getIntVal(valid);
      if (!valid)
        return true;
      if (Val == 1) {
        Parser.Lex();
        if (getLexer().getKind() == AsmToken::RBrac) {
          SMLoc RBracS = getLoc();
          Parser.Lex();
          Operands.push_back(
              AArch64Operand::CreateToken("[", false, LBracS, getContext()));
          Operands.push_back(
              AArch64Operand::CreateToken("1", false, IntS, getContext()));
          Operands.push_back(
              AArch64Operand::CreateToken("]", false, RBracS, getContext()));
          return false;
        }
      }
    }
  }

  return false;
}

bool AArch64AsmParser::parseSymbolicImmVal(const MCExpr *&ImmVal)
{
  MCAsmParser &Parser = getParser();
  bool HasELFModifier = false;
  AArch64MCExpr::VariantKind RefKind;

  if (Parser.getTok().is(AsmToken::Colon)) {
    Parser.Lex(); // Eat ':"
    HasELFModifier = true;

    if (Parser.getTok().isNot(AsmToken::Identifier)) {
      //Error(Parser.getTok().getLoc(),
      //      "expect relocation specifier in operand after ':'");
      return true;
    }

    std::string LowerCase = Parser.getTok().getIdentifier().lower();
    RefKind = StringSwitch<AArch64MCExpr::VariantKind>(LowerCase)
                  .Case("lo12", AArch64MCExpr::VK_LO12)
                  .Case("abs_g3", AArch64MCExpr::VK_ABS_G3)
                  .Case("abs_g2", AArch64MCExpr::VK_ABS_G2)
                  .Case("abs_g2_s", AArch64MCExpr::VK_ABS_G2_S)
                  .Case("abs_g2_nc", AArch64MCExpr::VK_ABS_G2_NC)
                  .Case("abs_g1", AArch64MCExpr::VK_ABS_G1)
                  .Case("abs_g1_s", AArch64MCExpr::VK_ABS_G1_S)
                  .Case("abs_g1_nc", AArch64MCExpr::VK_ABS_G1_NC)
                  .Case("abs_g0", AArch64MCExpr::VK_ABS_G0)
                  .Case("abs_g0_s", AArch64MCExpr::VK_ABS_G0_S)
                  .Case("abs_g0_nc", AArch64MCExpr::VK_ABS_G0_NC)
                  .Case("dtprel_g2", AArch64MCExpr::VK_DTPREL_G2)
                  .Case("dtprel_g1", AArch64MCExpr::VK_DTPREL_G1)
                  .Case("dtprel_g1_nc", AArch64MCExpr::VK_DTPREL_G1_NC)
                  .Case("dtprel_g0", AArch64MCExpr::VK_DTPREL_G0)
                  .Case("dtprel_g0_nc", AArch64MCExpr::VK_DTPREL_G0_NC)
                  .Case("dtprel_hi12", AArch64MCExpr::VK_DTPREL_HI12)
                  .Case("dtprel_lo12", AArch64MCExpr::VK_DTPREL_LO12)
                  .Case("dtprel_lo12_nc", AArch64MCExpr::VK_DTPREL_LO12_NC)
                  .Case("tprel_g2", AArch64MCExpr::VK_TPREL_G2)
                  .Case("tprel_g1", AArch64MCExpr::VK_TPREL_G1)
                  .Case("tprel_g1_nc", AArch64MCExpr::VK_TPREL_G1_NC)
                  .Case("tprel_g0", AArch64MCExpr::VK_TPREL_G0)
                  .Case("tprel_g0_nc", AArch64MCExpr::VK_TPREL_G0_NC)
                  .Case("tprel_hi12", AArch64MCExpr::VK_TPREL_HI12)
                  .Case("tprel_lo12", AArch64MCExpr::VK_TPREL_LO12)
                  .Case("tprel_lo12_nc", AArch64MCExpr::VK_TPREL_LO12_NC)
                  .Case("tlsdesc_lo12", AArch64MCExpr::VK_TLSDESC_LO12)
                  .Case("got", AArch64MCExpr::VK_GOT_PAGE)
                  .Case("got_lo12", AArch64MCExpr::VK_GOT_LO12)
                  .Case("gottprel", AArch64MCExpr::VK_GOTTPREL_PAGE)
                  .Case("gottprel_lo12", AArch64MCExpr::VK_GOTTPREL_LO12_NC)
                  .Case("gottprel_g1", AArch64MCExpr::VK_GOTTPREL_G1)
                  .Case("gottprel_g0_nc", AArch64MCExpr::VK_GOTTPREL_G0_NC)
                  .Case("tlsdesc", AArch64MCExpr::VK_TLSDESC_PAGE)
                  .Default(AArch64MCExpr::VK_INVALID);

    if (RefKind == AArch64MCExpr::VK_INVALID) {
      //Error(Parser.getTok().getLoc(),
      //      "expect relocation specifier in operand after ':'");
      return true;
    }

    Parser.Lex(); // Eat identifier

    if (Parser.getTok().isNot(AsmToken::Colon)) {
      //Error(Parser.getTok().getLoc(), "expect ':' after relocation specifier");
      return true;
    }
    Parser.Lex(); // Eat ':'
  }

  if (getParser().parseExpression(ImmVal))
    return true;

  if (HasELFModifier)
    ImmVal = AArch64MCExpr::create(ImmVal, RefKind, getContext());

  return false;
}

/// parseVectorList - Parse a vector list operand for AdvSIMD instructions.
// return true on error
bool AArch64AsmParser::parseVectorList(OperandVector &Operands)
{
  MCAsmParser &Parser = getParser();
  assert(Parser.getTok().is(AsmToken::LCurly) && "Token is not a Left Bracket");
  SMLoc S = getLoc();
  Parser.Lex(); // Eat left bracket token.
  StringRef Kind;
  int64_t FirstReg = tryMatchVectorRegister(Kind, true);
  if (FirstReg == -1)
    return true;
  int64_t PrevReg = FirstReg;
  unsigned Count = 1;

  if (Parser.getTok().is(AsmToken::Minus)) {
    Parser.Lex(); // Eat the minus.

    SMLoc Loc = getLoc();
    StringRef NextKind;
    int64_t Reg = tryMatchVectorRegister(NextKind, true);
    if (Reg == -1)
      return true;
    // Any Kind suffices must match on all regs in the list.
    if (Kind != NextKind)
      return Error(Loc, "mismatched register size suffix");

    unsigned Space = (PrevReg < Reg) ? (Reg - PrevReg) : (Reg + 32 - PrevReg);

    if (Space == 0 || Space > 3) {
      return Error(Loc, "invalid number of vectors");
    }

    Count += Space;
  }
  else {
    while (Parser.getTok().is(AsmToken::Comma)) {
      Parser.Lex(); // Eat the comma token.

      SMLoc Loc = getLoc();
      StringRef NextKind;
      int64_t Reg = tryMatchVectorRegister(NextKind, true);
      if (Reg == -1)
        return true;
      // Any Kind suffices must match on all regs in the list.
      if (Kind != NextKind)
        return Error(Loc, "mismatched register size suffix");

      // Registers must be incremental (with wraparound at 31)
      if (getContext().getRegisterInfo()->getEncodingValue(Reg) !=
          (getContext().getRegisterInfo()->getEncodingValue(PrevReg) + 1) % 32)
       return Error(Loc, "registers must be sequential");

      PrevReg = Reg;
      ++Count;
    }
  }

  if (Parser.getTok().isNot(AsmToken::RCurly))
    return Error(getLoc(), "'}' expected");
  Parser.Lex(); // Eat the '}' token.

  if (Count > 4)
    return Error(S, "invalid number of vectors");

  unsigned NumElements = 0;
  char ElementKind = 0;
  if (!Kind.empty())
    parseValidVectorKind(Kind, NumElements, ElementKind);

  Operands.push_back(AArch64Operand::CreateVectorList(
      FirstReg, Count, NumElements, ElementKind, S, getLoc(), getContext()));

  // If there is an index specifier following the list, parse that too.
  if (Parser.getTok().is(AsmToken::LBrac)) {
    SMLoc SIdx = getLoc();
    Parser.Lex(); // Eat left bracket token.

    const MCExpr *ImmVal;
    if (getParser().parseExpression(ImmVal))
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE) {
      //TokError("immediate value expected for vector index");
      return false;
    }

    SMLoc E = getLoc();
    if (Parser.getTok().isNot(AsmToken::RBrac)) {
      //Error(E, "']' expected");
      return false;
    }

    Parser.Lex(); // Eat right bracket token.

    Operands.push_back(AArch64Operand::CreateVectorIndex(MCE->getValue(), SIdx,
                                                         E, getContext()));
  }
  return false;
}

AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParseGPR64sp0Operand(OperandVector &Operands)
{
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  if (!Tok.is(AsmToken::Identifier))
    return MatchOperand_NoMatch;

  unsigned RegNum = matchRegisterNameAlias(Tok.getString().lower(), false);

  MCContext &Ctx = getContext();
  const MCRegisterInfo *RI = Ctx.getRegisterInfo();
  if (!RI->getRegClass(AArch64::GPR64spRegClassID).contains(RegNum))
    return MatchOperand_NoMatch;

  SMLoc S = getLoc();
  Parser.Lex(); // Eat register

  if (Parser.getTok().isNot(AsmToken::Comma)) {
    Operands.push_back(
        AArch64Operand::CreateReg(RegNum, false, S, getLoc(), Ctx));
    return MatchOperand_Success;
  }
  Parser.Lex(); // Eat comma.

  if (Parser.getTok().is(AsmToken::Hash))
    Parser.Lex(); // Eat hash

  if (Parser.getTok().isNot(AsmToken::Integer)) {
    //Error(getLoc(), "index must be absent or #0");
    return MatchOperand_ParseFail;
  }

  const MCExpr *ImmVal;
  if (Parser.parseExpression(ImmVal) || !isa<MCConstantExpr>(ImmVal) ||
      cast<MCConstantExpr>(ImmVal)->getValue() != 0) {
    //Error(getLoc(), "index must be absent or #0");
    return MatchOperand_ParseFail;
  }

  Operands.push_back(
      AArch64Operand::CreateReg(RegNum, false, S, getLoc(), Ctx));
  return MatchOperand_Success;
}

/// parseOperand - Parse a arm instruction operand.  For now this parses the
/// operand regardless of the mnemonic.
// return true on failure
bool AArch64AsmParser::parseOperand(OperandVector &Operands, bool isCondCode,
                                  bool invertCondCode)
{
  MCAsmParser &Parser = getParser();
  // Check if the current operand has a custom associated parser, if so, try to
  // custom parse the operand, or fallback to the general approach.
  OperandMatchResultTy ResTy = MatchOperandParserImpl(Operands, Mnemonic);
  if (ResTy == MatchOperand_Success)
    return false;
  // If there wasn't a custom match, try the generic matcher below. Otherwise,
  // there was a match, but an error occurred, in which case, just return that
  // the operand parsing failed.
  if (ResTy == MatchOperand_ParseFail)
    return true;

  // Nothing custom, so do general case parsing.
  SMLoc S, E;
  switch (getLexer().getKind()) {
  default: {
    SMLoc S = getLoc();
    const MCExpr *Expr;
    if (parseSymbolicImmVal(Expr))
      return Error(S, "invalid operand");

    SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(AArch64Operand::CreateImm(Expr, S, E, getContext()));
    return false;
  }
  case AsmToken::LBrac: {
    SMLoc Loc = Parser.getTok().getLoc();
    Operands.push_back(AArch64Operand::CreateToken("[", false, Loc,
                                                   getContext()));
    Parser.Lex(); // Eat '['

    // There's no comma after a '[', so we can parse the next operand
    // immediately.
    return parseOperand(Operands, false, false);
  }
  case AsmToken::LCurly:
    return parseVectorList(Operands);
  case AsmToken::Identifier: {
    // If we're expecting a Condition Code operand, then just parse that.
    if (isCondCode)
      return parseCondCode(Operands, invertCondCode);

    // If it's a register name, parse it.
    if (!parseRegister(Operands))
      return false;

    // This could be an optional "shift" or "extend" operand.
    OperandMatchResultTy GotShift = tryParseOptionalShiftExtend(Operands);
    // We can only continue if no tokens were eaten.
    if (GotShift != MatchOperand_NoMatch)
      return GotShift;

    // This was not a register so parse other operands that start with an
    // identifier (like labels) as expressions and create them as immediates.
    const MCExpr *IdVal;
    S = getLoc();
    if (getParser().parseExpression(IdVal))
      return true;

    E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(AArch64Operand::CreateImm(IdVal, S, E, getContext()));
    return false;
  }
  case AsmToken::Integer:
  case AsmToken::Real:
  case AsmToken::Hash: {
    // #42 -> immediate.
    S = getLoc();
    if (getLexer().is(AsmToken::Hash))
      Parser.Lex();

    // Parse a negative sign
    bool isNegative = false;
    if (Parser.getTok().is(AsmToken::Minus)) {
      isNegative = true;
      // We need to consume this token only when we have a Real, otherwise
      // we let parseSymbolicImmVal take care of it
      if (Parser.getLexer().peekTok().is(AsmToken::Real))
        Parser.Lex();
    }

    // The only Real that should come through here is a literal #0.0 for
    // the fcmp[e] r, #0.0 instructions. They expect raw token operands,
    // so convert the value.
    const AsmToken &Tok = Parser.getTok();
    if (Tok.is(AsmToken::Real)) {
      APFloat RealVal(APFloat::IEEEdouble, Tok.getString());
      if (RealVal.bitcastToAPInt().getActiveBits() > 64)
          return true;
      uint64_t IntVal = RealVal.bitcastToAPInt().getZExtValue();
      if (Mnemonic != "fcmp" && Mnemonic != "fcmpe" && Mnemonic != "fcmeq" &&
          Mnemonic != "fcmge" && Mnemonic != "fcmgt" && Mnemonic != "fcmle" &&
          Mnemonic != "fcmlt")
        //return TokError("unexpected floating point literal");
        return true;
      else if (IntVal != 0 || isNegative)
        //return TokError("expected floating-point constant #0.0");
        return true;
      Parser.Lex(); // Eat the token.

      Operands.push_back(
          AArch64Operand::CreateToken("#0", false, S, getContext()));
      Operands.push_back(
          AArch64Operand::CreateToken(".0", false, S, getContext()));
      return false;
    }

    const MCExpr *ImmVal;
    if (parseSymbolicImmVal(ImmVal))
      return true;

    E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(AArch64Operand::CreateImm(ImmVal, S, E, getContext()));
    return false;
  }
  case AsmToken::Equal: {
    SMLoc Loc = Parser.getTok().getLoc();
    if (Mnemonic != "ldr") // only parse for ldr pseudo (e.g. ldr r0, =val)
      return Error(Loc, "unexpected token in operand");
    Parser.Lex(); // Eat '='
    const MCExpr *SubExprVal;
    if (getParser().parseExpression(SubExprVal))
      return true;

    if (Operands.size() < 2 ||
        !static_cast<AArch64Operand &>(*Operands[1]).isReg())
      return Error(Loc, "Only valid when first operand is register");

    bool IsXReg =
        AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
            Operands[1]->getReg());

    MCContext& Ctx = getContext();
    E = SMLoc::getFromPointer(Loc.getPointer() - 1);
    // If the op is an imm and can be fit into a mov, then replace ldr with mov.
    if (isa<MCConstantExpr>(SubExprVal)) {
      uint64_t Imm = (cast<MCConstantExpr>(SubExprVal))->getValue();
      uint32_t ShiftAmt = 0, MaxShiftAmt = IsXReg ? 48 : 16;
      while(Imm > 0xFFFF && countTrailingZeros(Imm) >= 16) {
        ShiftAmt += 16;
        Imm >>= 16;
      }
      if (ShiftAmt <= MaxShiftAmt && Imm <= 0xFFFF) {
          Operands[0] = AArch64Operand::CreateToken("movz", false, Loc, Ctx);
          Operands.push_back(AArch64Operand::CreateImm(
                     MCConstantExpr::create(Imm, Ctx), S, E, Ctx));
        if (ShiftAmt)
          Operands.push_back(AArch64Operand::CreateShiftExtend(AArch64_AM::LSL,
                     ShiftAmt, true, S, E, Ctx));
        return false;
      }
      APInt Simm = APInt(64, Imm << ShiftAmt);
      // check if the immediate is an unsigned or signed 32-bit int for W regs
      if (!IsXReg && !(Simm.isIntN(32) || Simm.isSignedIntN(32)))
        //return Error(Loc, "Immediate too large for register");
        return true;
    }
    // If it is a label or an imm that cannot fit in a movz, put it into CP.
    const MCExpr *CPLoc =
        getTargetStreamer().addConstantPoolEntry(SubExprVal, IsXReg ? 8 : 4, Loc);
    Operands.push_back(AArch64Operand::CreateImm(CPLoc, S, E, Ctx));
    return false;
  }
  }
}

/// ParseInstruction - Parse an AArch64 instruction mnemonic followed by its
/// operands.
// return true on error
bool AArch64AsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                        StringRef Name, SMLoc NameLoc,
                                        OperandVector &Operands, unsigned int &ErrorCode)
{
  MCAsmParser &Parser = getParser();
  Name = StringSwitch<StringRef>(Name.lower())
             .Case("beq", "b.eq")
             .Case("bne", "b.ne")
             .Case("bhs", "b.hs")
             .Case("bcs", "b.cs")
             .Case("blo", "b.lo")
             .Case("bcc", "b.cc")
             .Case("bmi", "b.mi")
             .Case("bpl", "b.pl")
             .Case("bvs", "b.vs")
             .Case("bvc", "b.vc")
             .Case("bhi", "b.hi")
             .Case("bls", "b.ls")
             .Case("bge", "b.ge")
             .Case("blt", "b.lt")
             .Case("bgt", "b.gt")
             .Case("ble", "b.le")
             .Case("bal", "b.al")
             .Case("bnv", "b.nv")
             .Default(Name);

  // First check for the AArch64-specific .req directive.
  if (Parser.getTok().is(AsmToken::Identifier) &&
      Parser.getTok().getIdentifier() == ".req") {
    parseDirectiveReq(Name, NameLoc);
    // We always return 'error' for this, as we're done with this
    // statement and don't need to match the 'instruction."
    return true;
  }

  // Create the leading tokens for the mnemonic, split by '.' characters.
  size_t Start = 0, Next = Name.find('.');
  StringRef Head = Name.slice(Start, Next);

  // IC, DC, AT, and TLBI instructions are aliases for the SYS instruction.
  if (Head == "ic" || Head == "dc" || Head == "at" || Head == "tlbi") {
    bool IsError = parseSysAlias(Head, NameLoc, Operands);
    if (IsError && getLexer().isNot(AsmToken::EndOfStatement))
      Parser.eatToEndOfStatement();
    return IsError;
  }

  Operands.push_back(
      AArch64Operand::CreateToken(Head, false, NameLoc, getContext()));
  Mnemonic = Head;

  // Handle condition codes for a branch mnemonic
  if (Head == "b" && Next != StringRef::npos) {
    Start = Next;
    Next = Name.find('.', Start + 1);
    Head = Name.slice(Start + 1, Next);

    SMLoc SuffixLoc = SMLoc::getFromPointer(NameLoc.getPointer() +
                                            (Head.data() - Name.data()));
    AArch64CC::CondCode CC = parseCondCodeString(Head);
    if (CC == AArch64CC::Invalid)
      //return Error(SuffixLoc, "invalid condition code");
      return true;
    Operands.push_back(
        AArch64Operand::CreateToken(".", true, SuffixLoc, getContext()));
    Operands.push_back(
        AArch64Operand::CreateCondCode(CC, NameLoc, NameLoc, getContext()));
  }

  // Add the remaining tokens in the mnemonic.
  while (Next != StringRef::npos) {
    Start = Next;
    Next = Name.find('.', Start + 1);
    Head = Name.slice(Start, Next);
    SMLoc SuffixLoc = SMLoc::getFromPointer(NameLoc.getPointer() +
                                            (Head.data() - Name.data()) + 1);
    Operands.push_back(
        AArch64Operand::CreateToken(Head, true, SuffixLoc, getContext()));
  }

  // Conditional compare instructions have a Condition Code operand, which needs
  // to be parsed and an immediate operand created.
  bool condCodeFourthOperand =
      (Head == "ccmp" || Head == "ccmn" || Head == "fccmp" ||
       Head == "fccmpe" || Head == "fcsel" || Head == "csel" ||
       Head == "csinc" || Head == "csinv" || Head == "csneg");

  // These instructions are aliases to some of the conditional select
  // instructions. However, the condition code is inverted in the aliased
  // instruction.
  //
  // FIXME: Is this the correct way to handle these? Or should the parser
  //        generate the aliased instructions directly?
  bool condCodeSecondOperand = (Head == "cset" || Head == "csetm");
  bool condCodeThirdOperand =
      (Head == "cinc" || Head == "cinv" || Head == "cneg");

  // Read the remaining operands.
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    // Read the first operand.
    if (parseOperand(Operands, false, false)) {
      Parser.eatToEndOfStatement();
      ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
      return true;
    }

    unsigned N = 2;
    while (getLexer().is(AsmToken::Comma)) {
      Parser.Lex(); // Eat the comma.

      // Parse and remember the operand.
      if (parseOperand(Operands, (N == 4 && condCodeFourthOperand) ||
                                     (N == 3 && condCodeThirdOperand) ||
                                     (N == 2 && condCodeSecondOperand),
                       condCodeSecondOperand || condCodeThirdOperand)) {
        Parser.eatToEndOfStatement();
        ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
        return true;
      }

      // After successfully parsing some operands there are two special cases to
      // consider (i.e. notional operands not separated by commas). Both are due
      // to memory specifiers:
      //  + An RBrac will end an address for load/store/prefetch
      //  + An '!' will indicate a pre-indexed operation.
      //
      // It's someone else's responsibility to make sure these tokens are sane
      // in the given context!
      if (Parser.getTok().is(AsmToken::RBrac)) {
        SMLoc Loc = Parser.getTok().getLoc();
        Operands.push_back(AArch64Operand::CreateToken("]", false, Loc,
                                                       getContext()));
        Parser.Lex();
      }

      if (Parser.getTok().is(AsmToken::Exclaim)) {
        SMLoc Loc = Parser.getTok().getLoc();
        Operands.push_back(AArch64Operand::CreateToken("!", false, Loc,
                                                       getContext()));
        Parser.Lex();
      }

      ++N;
    }
  }

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    //SMLoc Loc = Parser.getTok().getLoc();
    Parser.eatToEndOfStatement();
    //return Error(Loc, "unexpected token in argument list");
    return true;
  }

  Parser.Lex(); // Consume the EndOfStatement
  return false;
}

// FIXME: This entire function is a giant hack to provide us with decent
// operand range validation/diagnostics until TableGen/MC can be extended
// to support autogeneration of this kind of validation.
bool AArch64AsmParser::validateInstruction(MCInst &Inst,
                                         SmallVectorImpl<SMLoc> &Loc)
{
  const MCRegisterInfo *RI = getContext().getRegisterInfo();
  // Check for indexed addressing modes w/ the base register being the
  // same as a destination/source register or pair load where
  // the Rt == Rt2. All of those are undefined behaviour.
  switch (Inst.getOpcode()) {
  case AArch64::LDPSWpre:
  case AArch64::LDPWpost:
  case AArch64::LDPWpre:
  case AArch64::LDPXpost:
  case AArch64::LDPXpre: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    unsigned Rn = Inst.getOperand(3).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      //return Error(Loc[0], "unpredictable LDP instruction, writeback base "
      //                     "is also a destination");
      return true;
    if (RI->isSubRegisterEq(Rn, Rt2))
      //return Error(Loc[1], "unpredictable LDP instruction, writeback base "
      //                     "is also a destination");
      return true;
    // FALLTHROUGH
  }
  case AArch64::LDPDi:
  case AArch64::LDPQi:
  case AArch64::LDPSi:
  case AArch64::LDPSWi:
  case AArch64::LDPWi:
  case AArch64::LDPXi: {
    unsigned Rt = Inst.getOperand(0).getReg();
    unsigned Rt2 = Inst.getOperand(1).getReg();
    if (Rt == Rt2)
      //return Error(Loc[1], "unpredictable LDP instruction, Rt2==Rt");
      return true;
    break;
  }
  case AArch64::LDPDpost:
  case AArch64::LDPDpre:
  case AArch64::LDPQpost:
  case AArch64::LDPQpre:
  case AArch64::LDPSpost:
  case AArch64::LDPSpre:
  case AArch64::LDPSWpost: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    if (Rt == Rt2)
      //return Error(Loc[1], "unpredictable LDP instruction, Rt2==Rt");
      return true;
    break;
  }
  case AArch64::STPDpost:
  case AArch64::STPDpre:
  case AArch64::STPQpost:
  case AArch64::STPQpre:
  case AArch64::STPSpost:
  case AArch64::STPSpre:
  case AArch64::STPWpost:
  case AArch64::STPWpre:
  case AArch64::STPXpost:
  case AArch64::STPXpre: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    unsigned Rn = Inst.getOperand(3).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      //return Error(Loc[0], "unpredictable STP instruction, writeback base "
      //                     "is also a source");
      return true;
    if (RI->isSubRegisterEq(Rn, Rt2))
      //return Error(Loc[1], "unpredictable STP instruction, writeback base "
      //                     "is also a source");
      return true;
    break;
  }
  case AArch64::LDRBBpre:
  case AArch64::LDRBpre:
  case AArch64::LDRHHpre:
  case AArch64::LDRHpre:
  case AArch64::LDRSBWpre:
  case AArch64::LDRSBXpre:
  case AArch64::LDRSHWpre:
  case AArch64::LDRSHXpre:
  case AArch64::LDRSWpre:
  case AArch64::LDRWpre:
  case AArch64::LDRXpre:
  case AArch64::LDRBBpost:
  case AArch64::LDRBpost:
  case AArch64::LDRHHpost:
  case AArch64::LDRHpost:
  case AArch64::LDRSBWpost:
  case AArch64::LDRSBXpost:
  case AArch64::LDRSHWpost:
  case AArch64::LDRSHXpost:
  case AArch64::LDRSWpost:
  case AArch64::LDRWpost:
  case AArch64::LDRXpost: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rn = Inst.getOperand(2).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      //return Error(Loc[0], "unpredictable LDR instruction, writeback base "
      //                     "is also a source");
      return true;
    break;
  }
  case AArch64::STRBBpost:
  case AArch64::STRBpost:
  case AArch64::STRHHpost:
  case AArch64::STRHpost:
  case AArch64::STRWpost:
  case AArch64::STRXpost:
  case AArch64::STRBBpre:
  case AArch64::STRBpre:
  case AArch64::STRHHpre:
  case AArch64::STRHpre:
  case AArch64::STRWpre:
  case AArch64::STRXpre: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rn = Inst.getOperand(2).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      //return Error(Loc[0], "unpredictable STR instruction, writeback base "
      //                     "is also a source");
      return true;
    break;
  }
  }

  // Now check immediate ranges. Separate from the above as there is overlap
  // in the instructions being checked and this keeps the nested conditionals
  // to a minimum.
  switch (Inst.getOpcode()) {
  case AArch64::ADDSWri:
  case AArch64::ADDSXri:
  case AArch64::ADDWri:
  case AArch64::ADDXri:
  case AArch64::SUBSWri:
  case AArch64::SUBSXri:
  case AArch64::SUBWri:
  case AArch64::SUBXri: {
    // Annoyingly we can't do this in the isAddSubImm predicate, so there is
    // some slight duplication here.
    if (Inst.getOperand(2).isExpr()) {
      const MCExpr *Expr = Inst.getOperand(2).getExpr();
      AArch64MCExpr::VariantKind ELFRefKind;
      MCSymbolRefExpr::VariantKind DarwinRefKind;
      int64_t Addend;
      if (!classifySymbolRef(Expr, ELFRefKind, DarwinRefKind, Addend)) {
        //return Error(Loc[2], "invalid immediate expression");
        return true;
      }

      // Only allow these with ADDXri.
      if ((DarwinRefKind == MCSymbolRefExpr::VK_PAGEOFF ||
          DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGEOFF) &&
          Inst.getOpcode() == AArch64::ADDXri)
        return false;

      // Only allow these with ADDXri/ADDWri
      if ((ELFRefKind == AArch64MCExpr::VK_LO12 ||
          ELFRefKind == AArch64MCExpr::VK_DTPREL_HI12 ||
          ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12 ||
          ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12_NC ||
          ELFRefKind == AArch64MCExpr::VK_TPREL_HI12 ||
          ELFRefKind == AArch64MCExpr::VK_TPREL_LO12 ||
          ELFRefKind == AArch64MCExpr::VK_TPREL_LO12_NC ||
          ELFRefKind == AArch64MCExpr::VK_TLSDESC_LO12) &&
          (Inst.getOpcode() == AArch64::ADDXri ||
          Inst.getOpcode() == AArch64::ADDWri))
        return false;

      // Don't allow expressions in the immediate field otherwise
      //return Error(Loc[2], "invalid immediate expression");
      return true;
    }
    return false;
  }
  default:
    return false;
  }
}

bool AArch64AsmParser::showMatchError(SMLoc Loc, unsigned ErrCode)
{
  switch (ErrCode) {
  case Match_MissingFeature:
    Error(Loc,
                 "instruction requires a CPU feature not currently enabled");
    return true;
  case Match_InvalidOperand:
    Error(Loc, "invalid operand for instruction");
    return true;
  case Match_InvalidSuffix:
    Error(Loc, "invalid type suffix for instruction");
    return true;
  case Match_InvalidCondCode:
    Error(Loc, "expected AArch64 condition code");
    return true;
  case Match_AddSubRegExtendSmall:
    Error(Loc,
      "expected '[su]xt[bhw]' or 'lsl' with optional integer in range [0, 4]");
    return true;
  case Match_AddSubRegExtendLarge:
    Error(Loc,
      "expected 'sxtx' 'uxtx' or 'lsl' with optional integer in range [0, 4]");
    return true;
  case Match_AddSubSecondSource:
    Error(Loc,
      "expected compatible register, symbol or integer in range [0, 4095]");
    return true;
  case Match_LogicalSecondSource:
    Error(Loc, "expected compatible register or logical immediate");
    return true;
  case Match_InvalidMovImm32Shift:
    Error(Loc, "expected 'lsl' with optional integer 0 or 16");
    return true;
  case Match_InvalidMovImm64Shift:
    Error(Loc, "expected 'lsl' with optional integer 0, 16, 32 or 48");
    return true;
  case Match_AddSubRegShift32:
    Error(Loc,
       "expected 'lsl', 'lsr' or 'asr' with optional integer in range [0, 31]");
    return true;
  case Match_AddSubRegShift64:
    Error(Loc,
       "expected 'lsl', 'lsr' or 'asr' with optional integer in range [0, 63]");
    return true;
  case Match_InvalidFPImm:
    Error(Loc,
                 "expected compatible register or floating-point constant");
    return true;
  case Match_InvalidMemoryIndexedSImm9:
    Error(Loc, "index must be an integer in range [-256, 255].");
    return true;
  case Match_InvalidMemoryIndexed4SImm7:
    Error(Loc, "index must be a multiple of 4 in range [-256, 252].");
    return true;
  case Match_InvalidMemoryIndexed8SImm7:
    Error(Loc, "index must be a multiple of 8 in range [-512, 504].");
    return true;
  case Match_InvalidMemoryIndexed16SImm7:
    Error(Loc, "index must be a multiple of 16 in range [-1024, 1008].");
    return true;
  case Match_InvalidMemoryWExtend8:
    Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0");
    return true;
  case Match_InvalidMemoryWExtend16:
    Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #1");
    return true;
  case Match_InvalidMemoryWExtend32:
    Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #2");
    return true;
  case Match_InvalidMemoryWExtend64:
    Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #3");
    return true;
  case Match_InvalidMemoryWExtend128:
    Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #4");
    return true;
  case Match_InvalidMemoryXExtend8:
    Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0");
    return true;
  case Match_InvalidMemoryXExtend16:
    Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #1");
    return true;
  case Match_InvalidMemoryXExtend32:
    Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #2");
    return true;
  case Match_InvalidMemoryXExtend64:
    Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #3");
    return true;
  case Match_InvalidMemoryXExtend128:
    Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #4");
    return true;
  case Match_InvalidMemoryIndexed1:
    Error(Loc, "index must be an integer in range [0, 4095].");
    return true;
  case Match_InvalidMemoryIndexed2:
    Error(Loc, "index must be a multiple of 2 in range [0, 8190].");
    return true;
  case Match_InvalidMemoryIndexed4:
    Error(Loc, "index must be a multiple of 4 in range [0, 16380].");
    return true;
  case Match_InvalidMemoryIndexed8:
    Error(Loc, "index must be a multiple of 8 in range [0, 32760].");
    return true;
  case Match_InvalidMemoryIndexed16:
    Error(Loc, "index must be a multiple of 16 in range [0, 65520].");
    return true;
  case Match_InvalidImm0_1:
    Error(Loc, "immediate must be an integer in range [0, 1].");
    return true;
  case Match_InvalidImm0_7:
    Error(Loc, "immediate must be an integer in range [0, 7].");
    return true;
  case Match_InvalidImm0_15:
    Error(Loc, "immediate must be an integer in range [0, 15].");
    return true;
  case Match_InvalidImm0_31:
    Error(Loc, "immediate must be an integer in range [0, 31].");
    return true;
  case Match_InvalidImm0_63:
    Error(Loc, "immediate must be an integer in range [0, 63].");
    return true;
  case Match_InvalidImm0_127:
    Error(Loc, "immediate must be an integer in range [0, 127].");
    return true;
  case Match_InvalidImm0_65535:
    Error(Loc, "immediate must be an integer in range [0, 65535].");
    return true;
  case Match_InvalidImm1_8:
    Error(Loc, "immediate must be an integer in range [1, 8].");
    return true;
  case Match_InvalidImm1_16:
    Error(Loc, "immediate must be an integer in range [1, 16].");
    return true;
  case Match_InvalidImm1_32:
    Error(Loc, "immediate must be an integer in range [1, 32].");
    return true;
  case Match_InvalidImm1_64:
    Error(Loc, "immediate must be an integer in range [1, 64].");
    return true;
  case Match_InvalidIndex1:
    Error(Loc, "expected lane specifier '[1]'");
    return true;
  case Match_InvalidIndexB:
    Error(Loc, "vector lane must be an integer in range [0, 15].");
    return true;
  case Match_InvalidIndexH:
    Error(Loc, "vector lane must be an integer in range [0, 7].");
    return true;
  case Match_InvalidIndexS:
    Error(Loc, "vector lane must be an integer in range [0, 3].");
    return true;
  case Match_InvalidIndexD:
    Error(Loc, "vector lane must be an integer in range [0, 1].");
    return true;
  case Match_InvalidLabel:
    Error(Loc, "expected label or encodable integer pc offset");
    return true;
  case Match_MRS:
    Error(Loc, "expected readable system register");
    return true;
  case Match_MSR:
    return Error(Loc, "expected writable system register or pstate");
  case Match_MnemonicFail:
    return Error(Loc, "unrecognized instruction mnemonic");
  default:
    llvm_unreachable("unexpected error code!");
  }
}

static const char *getSubtargetFeatureName(uint64_t Val);

// return True on error
bool AArch64AsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                               OperandVector &Operands,
                                               MCStreamer &Out,
                                               uint64_t &ErrorInfo,
                                               bool MatchingInlineAsm, unsigned int &ErrorCode, uint64_t &Address)
{
  assert(!Operands.empty() && "Unexpect empty operand list!");
  AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[0]);
  assert(Op.isToken() && "Leading operand should always be a mnemonic!");

  StringRef Tok = Op.getToken();
  unsigned NumOperands = Operands.size();

  if (NumOperands == 4 && Tok == "lsl") {
    AArch64Operand &Op2 = static_cast<AArch64Operand &>(*Operands[2]);
    AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
    if (Op2.isReg() && Op3.isImm()) {
      const MCConstantExpr *Op3CE = dyn_cast<MCConstantExpr>(Op3.getImm());
      if (Op3CE) {
        uint64_t Op3Val = Op3CE->getValue();
        uint64_t NewOp3Val = 0;
        uint64_t NewOp4Val = 0;
        if (AArch64MCRegisterClasses[AArch64::GPR32allRegClassID].contains(
                Op2.getReg())) {
          NewOp3Val = (32 - Op3Val) & 0x1f;
          NewOp4Val = 31 - Op3Val;
        } else {
          NewOp3Val = (64 - Op3Val) & 0x3f;
          NewOp4Val = 63 - Op3Val;
        }

        const MCExpr *NewOp3 = MCConstantExpr::create(NewOp3Val, getContext());
        const MCExpr *NewOp4 = MCConstantExpr::create(NewOp4Val, getContext());

        Operands[0] = AArch64Operand::CreateToken(
            "ubfm", false, Op.getStartLoc(), getContext());
        Operands.push_back(AArch64Operand::CreateImm(
            NewOp4, Op3.getStartLoc(), Op3.getEndLoc(), getContext()));
        Operands[3] = AArch64Operand::CreateImm(NewOp3, Op3.getStartLoc(),
                                                Op3.getEndLoc(), getContext());
      }
    }
  } else if (NumOperands == 4 && Tok == "bfc") {
    // FIXME: Horrible hack to handle BFC->BFM alias.
    AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
    AArch64Operand LSBOp = static_cast<AArch64Operand &>(*Operands[2]);
    AArch64Operand WidthOp = static_cast<AArch64Operand &>(*Operands[3]);

    if (Op1.isReg() && LSBOp.isImm() && WidthOp.isImm()) {
      const MCConstantExpr *LSBCE = dyn_cast<MCConstantExpr>(LSBOp.getImm());
      const MCConstantExpr *WidthCE = dyn_cast<MCConstantExpr>(WidthOp.getImm());

      if (LSBCE && WidthCE) {
        uint64_t LSB = LSBCE->getValue();
        uint64_t Width = WidthCE->getValue();

        uint64_t RegWidth = 0;
        if (AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
                Op1.getReg()))
          RegWidth = 64;
        else
          RegWidth = 32;

        if (LSB >= RegWidth) {
          ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
          return Error(LSBOp.getStartLoc(),
                       "expected integer in range [0, 31]");
        }
        if (Width < 1 || Width > RegWidth) {
          ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
          return Error(WidthOp.getStartLoc(),
                       "expected integer in range [1, 32]");
        }

        uint64_t ImmR = 0;
        if (RegWidth == 32)
          ImmR = (32 - LSB) & 0x1f;
        else
          ImmR = (64 - LSB) & 0x3f;

        uint64_t ImmS = Width - 1;

        if (ImmR != 0 && ImmS >= ImmR) {
          ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
          return Error(WidthOp.getStartLoc(),
                       "requested insert overflows register");
          return true;
        }

        const MCExpr *ImmRExpr = MCConstantExpr::create(ImmR, getContext());
        const MCExpr *ImmSExpr = MCConstantExpr::create(ImmS, getContext());
        Operands[0] = AArch64Operand::CreateToken(
              "bfm", false, Op.getStartLoc(), getContext());
        Operands[2] = AArch64Operand::CreateReg(
            RegWidth == 32 ? AArch64::WZR : AArch64::XZR, false, SMLoc(),
            SMLoc(), getContext());
        Operands[3] = AArch64Operand::CreateImm(
            ImmRExpr, LSBOp.getStartLoc(), LSBOp.getEndLoc(), getContext());
        Operands.emplace_back(
            AArch64Operand::CreateImm(ImmSExpr, WidthOp.getStartLoc(),
                                      WidthOp.getEndLoc(), getContext()));
      }
    }
  } else if (NumOperands == 5) {
    // FIXME: Horrible hack to handle the BFI -> BFM, SBFIZ->SBFM, and
    // UBFIZ -> UBFM aliases.
    if (Tok == "bfi" || Tok == "sbfiz" || Tok == "ubfiz") {
      AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
      AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
      AArch64Operand &Op4 = static_cast<AArch64Operand &>(*Operands[4]);

      if (Op1.isReg() && Op3.isImm() && Op4.isImm()) {
        const MCConstantExpr *Op3CE = dyn_cast<MCConstantExpr>(Op3.getImm());
        const MCConstantExpr *Op4CE = dyn_cast<MCConstantExpr>(Op4.getImm());

        if (Op3CE && Op4CE) {
          uint64_t Op3Val = Op3CE->getValue();
          uint64_t Op4Val = Op4CE->getValue();

          uint64_t RegWidth = 0;
          if (AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
                  Op1.getReg()))
            RegWidth = 64;
          else
            RegWidth = 32;

          if (Op3Val >= RegWidth) {
            //return Error(Op3.getStartLoc(),
            //             "expected integer in range [0, 31]");
            ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
            return true;
          }
          if (Op4Val < 1 || Op4Val > RegWidth) {
            //return Error(Op4.getStartLoc(),
            //             "expected integer in range [1, 32]");
            ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
            return true;
          }

          uint64_t NewOp3Val = 0;
          if (RegWidth == 32)
            NewOp3Val = (32 - Op3Val) & 0x1f;
          else
            NewOp3Val = (64 - Op3Val) & 0x3f;

          uint64_t NewOp4Val = Op4Val - 1;

          if (NewOp3Val != 0 && NewOp4Val >= NewOp3Val) {
            //return Error(Op4.getStartLoc(),
            //             "requested insert overflows register");
            ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
            return true;
          }

          const MCExpr *NewOp3 =
              MCConstantExpr::create(NewOp3Val, getContext());
          const MCExpr *NewOp4 =
              MCConstantExpr::create(NewOp4Val, getContext());
          Operands[3] = AArch64Operand::CreateImm(
              NewOp3, Op3.getStartLoc(), Op3.getEndLoc(), getContext());
          Operands[4] = AArch64Operand::CreateImm(
              NewOp4, Op4.getStartLoc(), Op4.getEndLoc(), getContext());
          if (Tok == "bfi")
            Operands[0] = AArch64Operand::CreateToken(
                "bfm", false, Op.getStartLoc(), getContext());
          else if (Tok == "sbfiz")
            Operands[0] = AArch64Operand::CreateToken(
                "sbfm", false, Op.getStartLoc(), getContext());
          else if (Tok == "ubfiz")
            Operands[0] = AArch64Operand::CreateToken(
                "ubfm", false, Op.getStartLoc(), getContext());
          else
            llvm_unreachable("No valid mnemonic for alias?");
        }
      }

      // FIXME: Horrible hack to handle the BFXIL->BFM, SBFX->SBFM, and
      // UBFX -> UBFM aliases.
    } else if (NumOperands == 5 &&
               (Tok == "bfxil" || Tok == "sbfx" || Tok == "ubfx")) {
      AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
      AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
      AArch64Operand &Op4 = static_cast<AArch64Operand &>(*Operands[4]);

      if (Op1.isReg() && Op3.isImm() && Op4.isImm()) {
        const MCConstantExpr *Op3CE = dyn_cast<MCConstantExpr>(Op3.getImm());
        const MCConstantExpr *Op4CE = dyn_cast<MCConstantExpr>(Op4.getImm());

        if (Op3CE && Op4CE) {
          uint64_t Op3Val = Op3CE->getValue();
          uint64_t Op4Val = Op4CE->getValue();

          uint64_t RegWidth = 0;
          if (AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
                  Op1.getReg()))
            RegWidth = 64;
          else
            RegWidth = 32;

          if (Op3Val >= RegWidth) {
          // TODO: save ErrorCode
            //return Error(Op3.getStartLoc(),
            //             "expected integer in range [0, 31]");
            ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
            return true;
          }
          if (Op4Val < 1 || Op4Val > RegWidth) {
          // TODO: save ErrorCode
            //return Error(Op4.getStartLoc(),
            //             "expected integer in range [1, 32]");
            ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
            return true;
          }

          uint64_t NewOp4Val = Op3Val + Op4Val - 1;

          if (NewOp4Val >= RegWidth || NewOp4Val < Op3Val) {
          // TODO: save ErrorCode
            //return Error(Op4.getStartLoc(),
            //             "requested extract overflows register");
            ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
            return true;
          }

          const MCExpr *NewOp4 =
              MCConstantExpr::create(NewOp4Val, getContext());
          Operands[4] = AArch64Operand::CreateImm(
              NewOp4, Op4.getStartLoc(), Op4.getEndLoc(), getContext());
          if (Tok == "bfxil")
            Operands[0] = AArch64Operand::CreateToken(
                "bfm", false, Op.getStartLoc(), getContext());
          else if (Tok == "sbfx")
            Operands[0] = AArch64Operand::CreateToken(
                "sbfm", false, Op.getStartLoc(), getContext());
          else if (Tok == "ubfx")
            Operands[0] = AArch64Operand::CreateToken(
                "ubfm", false, Op.getStartLoc(), getContext());
          else
            llvm_unreachable("No valid mnemonic for alias?");
        }
      }
    }
  }
  // FIXME: Horrible hack for sxtw and uxtw with Wn src and Xd dst operands.
  //        InstAlias can't quite handle this since the reg classes aren't
  //        subclasses.
  if (NumOperands == 3 && (Tok == "sxtw" || Tok == "uxtw")) {
    // The source register can be Wn here, but the matcher expects a
    // GPR64. Twiddle it here if necessary.
    AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[2]);
    if (Op.isReg()) {
      unsigned Reg = getXRegFromWReg(Op.getReg());
      Operands[2] = AArch64Operand::CreateReg(Reg, false, Op.getStartLoc(),
                                              Op.getEndLoc(), getContext());
    }
  }
  // FIXME: Likewise for sxt[bh] with a Xd dst operand
  else if (NumOperands == 3 && (Tok == "sxtb" || Tok == "sxth")) {
    AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[1]);
    if (Op.isReg() &&
        AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
            Op.getReg())) {
      // The source register can be Wn here, but the matcher expects a
      // GPR64. Twiddle it here if necessary.
      AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[2]);
      if (Op.isReg()) {
        unsigned Reg = getXRegFromWReg(Op.getReg());
        Operands[2] = AArch64Operand::CreateReg(Reg, false, Op.getStartLoc(),
                                                Op.getEndLoc(), getContext());
      }
    }
  }
  // FIXME: Likewise for uxt[bh] with a Xd dst operand
  else if (NumOperands == 3 && (Tok == "uxtb" || Tok == "uxth")) {
    AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[1]);
    if (Op.isReg() &&
        AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
            Op.getReg())) {
      // The source register can be Wn here, but the matcher expects a
      // GPR32. Twiddle it here if necessary.
      AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[1]);
      if (Op.isReg()) {
        unsigned Reg = getWRegFromXReg(Op.getReg());
        Operands[1] = AArch64Operand::CreateReg(Reg, false, Op.getStartLoc(),
                                                Op.getEndLoc(), getContext());
      }
    }
  }

  // Yet another horrible hack to handle FMOV Rd, #0.0 using [WX]ZR.
  if (NumOperands == 3 && Tok == "fmov") {
    AArch64Operand &RegOp = static_cast<AArch64Operand &>(*Operands[1]);
    AArch64Operand &ImmOp = static_cast<AArch64Operand &>(*Operands[2]);
    if (RegOp.isReg() && ImmOp.isFPImm() && ImmOp.getFPImm() == (unsigned)-1) {
      unsigned zreg =
          !AArch64MCRegisterClasses[AArch64::FPR64RegClassID].contains(
              RegOp.getReg())
              ? AArch64::WZR
              : AArch64::XZR;
      Operands[2] = AArch64Operand::CreateReg(zreg, false, Op.getStartLoc(),
                                              Op.getEndLoc(), getContext());
    }
  }

  MCInst Inst(Address);
  // First try to match against the secondary set of tables containing the
  // short-form NEON instructions (e.g. "fadd.2s v0, v1, v2").
  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm, 1);

  // If that fails, try against the alternate table containing long-form NEON:
  // "fadd v0.2s, v1.2s, v2.2s"
  if (MatchResult != Match_Success) {
    // But first, save the short-form match result: we can use it in case the
    // long-form match also fails.
    auto ShortFormNEONErrorInfo = ErrorInfo;
    auto ShortFormNEONMatchResult = MatchResult;

    MatchResult =
        MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm, 0);

    // Now, both matches failed, and the long-form match failed on the mnemonic
    // suffix token operand.  The short-form match failure is probably more
    // relevant: use it instead.
    if (MatchResult == Match_InvalidOperand && ErrorInfo == 1 &&
        Operands.size() > 1 && ((AArch64Operand &)*Operands[1]).isToken() &&
        ((AArch64Operand &)*Operands[1]).isTokenSuffix()) {
      MatchResult = ShortFormNEONMatchResult;
      ErrorInfo = ShortFormNEONErrorInfo;
    }
  }

  // save the error code
  ErrorCode = MatchResult;

  switch (MatchResult) {
  case Match_Success: {
    // Perform range checking and other semantic validations
    SmallVector<SMLoc, 8> OperandLocs;
    NumOperands = Operands.size();
    for (unsigned i = 1; i < NumOperands; ++i)
      OperandLocs.push_back(Operands[i]->getStartLoc());
    if (validateInstruction(Inst, OperandLocs))
      return true;

    Inst.setLoc(IDLoc);
    Out.EmitInstruction(Inst, getSTI(), ErrorCode);
    if (ErrorCode == 0) {
        Address = Inst.getAddress(); // Keystone update address
        return false;
    } else
        return true;
  }
  case Match_MissingFeature: {
    assert(ErrorInfo && "Unknown missing feature!");
    // Special case the error message for the very common case where only
    // a single subtarget feature is missing (neon, e.g.).
    std::string Msg = "instruction requires:";
    uint64_t Mask = 1;
    for (unsigned i = 0; i < (sizeof(ErrorInfo)*8-1); ++i) {
      if (ErrorInfo & Mask) {
        Msg += " ";
        Msg += getSubtargetFeatureName(ErrorInfo & Mask);
      }
      Mask <<= 1;
    }
    //return Error(IDLoc, Msg);
    ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
    return true;
  }
  case Match_MnemonicFail:
    return showMatchError(IDLoc, MatchResult);
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;

    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size()) {
        //return Error(IDLoc, "too few operands for instruction");
        ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
        return true;
      }

      ErrorLoc = ((AArch64Operand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    // If the match failed on a suffix token operand, tweak the diagnostic
    // accordingly.
    if (((AArch64Operand &)*Operands[ErrorInfo]).isToken() &&
        ((AArch64Operand &)*Operands[ErrorInfo]).isTokenSuffix())
      MatchResult = Match_InvalidSuffix;

    return showMatchError(ErrorLoc, MatchResult);
  }
  case Match_InvalidMemoryIndexed1:
  case Match_InvalidMemoryIndexed2:
  case Match_InvalidMemoryIndexed4:
  case Match_InvalidMemoryIndexed8:
  case Match_InvalidMemoryIndexed16:
  case Match_InvalidCondCode:
  case Match_AddSubRegExtendSmall:
  case Match_AddSubRegExtendLarge:
  case Match_AddSubSecondSource:
  case Match_LogicalSecondSource:
  case Match_AddSubRegShift32:
  case Match_AddSubRegShift64:
  case Match_InvalidMovImm32Shift:
  case Match_InvalidMovImm64Shift:
  case Match_InvalidFPImm:
  case Match_InvalidMemoryWExtend8:
  case Match_InvalidMemoryWExtend16:
  case Match_InvalidMemoryWExtend32:
  case Match_InvalidMemoryWExtend64:
  case Match_InvalidMemoryWExtend128:
  case Match_InvalidMemoryXExtend8:
  case Match_InvalidMemoryXExtend16:
  case Match_InvalidMemoryXExtend32:
  case Match_InvalidMemoryXExtend64:
  case Match_InvalidMemoryXExtend128:
  case Match_InvalidMemoryIndexed4SImm7:
  case Match_InvalidMemoryIndexed8SImm7:
  case Match_InvalidMemoryIndexed16SImm7:
  case Match_InvalidMemoryIndexedSImm9:
  case Match_InvalidImm0_1:
  case Match_InvalidImm0_7:
  case Match_InvalidImm0_15:
  case Match_InvalidImm0_31:
  case Match_InvalidImm0_63:
  case Match_InvalidImm0_127:
  case Match_InvalidImm0_65535:
  case Match_InvalidImm1_8:
  case Match_InvalidImm1_16:
  case Match_InvalidImm1_32:
  case Match_InvalidImm1_64:
  case Match_InvalidIndex1:
  case Match_InvalidIndexB:
  case Match_InvalidIndexH:
  case Match_InvalidIndexS:
  case Match_InvalidIndexD:
  case Match_InvalidLabel:
  case Match_MSR:
  case Match_MRS: {
    if (ErrorInfo >= Operands.size()) {
      //return Error(IDLoc, "too few operands for instruction");
      ErrorCode = KS_ERR_ASM_INVALIDOPERAND;
      return true;
    }
    // Any time we get here, there's nothing fancy to do. Just get the
    // operand SMLoc and display the diagnostic.
    SMLoc ErrorLoc = ((AArch64Operand &)*Operands[ErrorInfo]).getStartLoc();
    if (ErrorLoc == SMLoc())
      ErrorLoc = IDLoc;
    return showMatchError(ErrorLoc, MatchResult);
  }
  }

  llvm_unreachable("Implement any new match types added!");
}

/// ParseDirective parses the arm specific directives
bool AArch64AsmParser::ParseDirective(AsmToken DirectiveID) {
  const MCObjectFileInfo::Environment Format =
    getContext().getObjectFileInfo()->getObjectFileType();
  bool IsMachO = Format == MCObjectFileInfo::IsMachO;
  bool IsCOFF = Format == MCObjectFileInfo::IsCOFF;

  StringRef IDVal = DirectiveID.getIdentifier();
  SMLoc Loc = DirectiveID.getLoc();
  if (IDVal == ".hword")
    return parseDirectiveWord(2, Loc);
  if (IDVal == ".word")
    return parseDirectiveWord(4, Loc);
  if (IDVal == ".xword")
    return parseDirectiveWord(8, Loc);
  if (IDVal == ".tlsdesccall")
    return parseDirectiveTLSDescCall(Loc);
  if (IDVal == ".ltorg" || IDVal == ".pool")
    return parseDirectiveLtorg(Loc);
  if (IDVal == ".unreq")
    return parseDirectiveUnreq(Loc);

  if (!IsMachO && !IsCOFF) {
    if (IDVal == ".inst")
      return parseDirectiveInst(Loc);
  }

  return parseDirectiveLOH(IDVal, Loc);
}

/// parseDirectiveWord
///  ::= .word [ expression (, expression)* ]
bool AArch64AsmParser::parseDirectiveWord(unsigned Size, SMLoc L)
{
  MCAsmParser &Parser = getParser();
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    for (;;) {
      const MCExpr *Value;
      if (getParser().parseExpression(Value))
        return true;

      getParser().getStreamer().EmitValue(Value, Size, L);

      if (getLexer().is(AsmToken::EndOfStatement))
        break;

      // FIXME: Improve diagnostic.
      if (getLexer().isNot(AsmToken::Comma))
        //return Error(L, "unexpected token in directive");
        return true;
      Parser.Lex();
    }
  }

  Parser.Lex();
  return false;
}

/// parseDirectiveInst
///  ::= .inst opcode [, ...]
bool AArch64AsmParser::parseDirectiveInst(SMLoc Loc)
{
  MCAsmParser &Parser = getParser();
  if (getLexer().is(AsmToken::EndOfStatement)) {
    Parser.eatToEndOfStatement();
    //Error(Loc, "expected expression following directive");
    return false;
  }

  for (;;) {
    const MCExpr *Expr;

    if (getParser().parseExpression(Expr)) {
      //Error(Loc, "expected expression");
      return false;
    }

    const MCConstantExpr *Value = dyn_cast_or_null<MCConstantExpr>(Expr);
    if (!Value) {
      //Error(Loc, "expected constant expression");
      return false;
    }

    getTargetStreamer().emitInst(Value->getValue());

    if (getLexer().is(AsmToken::EndOfStatement))
      break;

    if (getLexer().isNot(AsmToken::Comma)) {
      //Error(Loc, "unexpected token in directive");
      return false;
    }

    Parser.Lex(); // Eat comma.
  }

  Parser.Lex();
  return false;
}

// parseDirectiveTLSDescCall:
//   ::= .tlsdesccall symbol
bool AArch64AsmParser::parseDirectiveTLSDescCall(SMLoc L) {
  StringRef Name;
  if (getParser().parseIdentifier(Name))
    //return Error(L, "expected symbol after directive");
    return true;

  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, getContext());
  Expr = AArch64MCExpr::create(Expr, AArch64MCExpr::VK_TLSDESC, getContext());

  MCInst Inst;
  Inst.setOpcode(AArch64::TLSDESCCALL);
  Inst.addOperand(MCOperand::createExpr(Expr));

  unsigned int KsError;
  getParser().getStreamer().EmitInstruction(Inst, getSTI(), KsError);
  return false;
}

/// ::= .loh <lohName | lohId> label1, ..., labelN
/// The number of arguments depends on the loh identifier.
bool AArch64AsmParser::parseDirectiveLOH(StringRef IDVal, SMLoc Loc)
{
  if (IDVal != MCLOHDirectiveName())
    return true;
  MCLOHType Kind;
  if (getParser().getTok().isNot(AsmToken::Identifier)) {
    if (getParser().getTok().isNot(AsmToken::Integer))
      //return TokError("expected an identifier or a number in directive");
      return true;
    // We successfully get a numeric value for the identifier.
    // Check if it is valid.
    bool valid;
    int64_t Id = getParser().getTok().getIntVal(valid);
    if (!valid)
      return true;
    if (Id <= -1U && !isValidMCLOHType(Id))
      //return TokError("invalid numeric identifier in directive");
      return true;
    Kind = (MCLOHType)Id;
  } else {
    StringRef Name = getTok().getIdentifier();
    // We successfully parse an identifier.
    // Check if it is a recognized one.
    int Id = MCLOHNameToId(Name);

    if (Id == -1)
      //return TokError("invalid identifier in directive");
      return true;
    Kind = (MCLOHType)Id;
  }
  // Consume the identifier.
  Lex();
  // Get the number of arguments of this LOH.
  int NbArgs = MCLOHIdToNbArgs(Kind);

  assert(NbArgs != -1 && "Invalid number of arguments");

  SmallVector<MCSymbol *, 3> Args;
  for (int Idx = 0; Idx < NbArgs; ++Idx) {
    StringRef Name;
    if (getParser().parseIdentifier(Name))
      //return TokError("expected identifier in directive");
      return true;
    Args.push_back(getContext().getOrCreateSymbol(Name));

    if (Idx + 1 == NbArgs)
      break;
    if (getLexer().isNot(AsmToken::Comma))
      //return TokError("unexpected token in '" + Twine(IDVal) + "' directive");
      return true;
    Lex();
  }
  if (getLexer().isNot(AsmToken::EndOfStatement))
    //return TokError("unexpected token in '" + Twine(IDVal) + "' directive");
    return true;

  getStreamer().EmitLOHDirective((MCLOHType)Kind, Args);
  return false;
}

/// parseDirectiveLtorg
///  ::= .ltorg | .pool
bool AArch64AsmParser::parseDirectiveLtorg(SMLoc L) {
  getTargetStreamer().emitCurrentConstantPool();
  return false;
}

/// parseDirectiveReq
///  ::= name .req registername
bool AArch64AsmParser::parseDirectiveReq(StringRef Name, SMLoc L)
{
  MCAsmParser &Parser = getParser();
  Parser.Lex(); // Eat the '.req' token.
  //SMLoc SRegLoc = getLoc();
  unsigned RegNum = tryParseRegister();
  bool IsVector = false;

  if (RegNum == static_cast<unsigned>(-1)) {
    StringRef Kind;
    RegNum = tryMatchVectorRegister(Kind, false);
    if (!Kind.empty()) {
      //Error(SRegLoc, "vector register without type specifier expected");
      return false;
    }
    IsVector = true;
  }

  if (RegNum == static_cast<unsigned>(-1)) {
    Parser.eatToEndOfStatement();
    //Error(SRegLoc, "register name or alias expected");
    return false;
  }

  // Shouldn't be anything else.
  if (Parser.getTok().isNot(AsmToken::EndOfStatement)) {
    //Error(Parser.getTok().getLoc(), "unexpected input in .req directive");
    Parser.eatToEndOfStatement();
    return false;
  }

  Parser.Lex(); // Consume the EndOfStatement

  auto pair = std::make_pair(IsVector, RegNum);
  if (RegisterReqs.insert(std::make_pair(Name, pair)).first->second != pair)
    Warning(L, "ignoring redefinition of register alias '" + Name + "'");

  return true;
}

/// parseDirectiveUneq
///  ::= .unreq registername
bool AArch64AsmParser::parseDirectiveUnreq(SMLoc L)
{
  MCAsmParser &Parser = getParser();
  if (Parser.getTok().isNot(AsmToken::Identifier)) {
    //Error(Parser.getTok().getLoc(), "unexpected input in .unreq directive.");
    Parser.eatToEndOfStatement();
    return false;
  }
  RegisterReqs.erase(Parser.getTok().getIdentifier().lower());
  Parser.Lex(); // Eat the identifier.
  return false;
}

bool
AArch64AsmParser::classifySymbolRef(const MCExpr *Expr,
                                    AArch64MCExpr::VariantKind &ELFRefKind,
                                    MCSymbolRefExpr::VariantKind &DarwinRefKind,
                                    int64_t &Addend) {
  ELFRefKind = AArch64MCExpr::VK_INVALID;
  DarwinRefKind = MCSymbolRefExpr::VK_None;
  Addend = 0;

  if (const AArch64MCExpr *AE = dyn_cast<AArch64MCExpr>(Expr)) {
    ELFRefKind = AE->getKind();
    Expr = AE->getSubExpr();
  }

  const MCSymbolRefExpr *SE = dyn_cast<MCSymbolRefExpr>(Expr);
  if (SE) {
    // It's a simple symbol reference with no addend.
    DarwinRefKind = SE->getKind();
    return true;
  }

  const MCBinaryExpr *BE = dyn_cast<MCBinaryExpr>(Expr);
  if (!BE)
    return false;

  SE = dyn_cast<MCSymbolRefExpr>(BE->getLHS());
  if (!SE)
    return false;
  DarwinRefKind = SE->getKind();

  if (BE->getOpcode() != MCBinaryExpr::Add &&
      BE->getOpcode() != MCBinaryExpr::Sub)
    return false;

  // See if the addend is is a constant, otherwise there's more going
  // on here than we can deal with.
  auto AddendExpr = dyn_cast<MCConstantExpr>(BE->getRHS());
  if (!AddendExpr)
    return false;

  Addend = AddendExpr->getValue();
  if (BE->getOpcode() == MCBinaryExpr::Sub)
    Addend = -Addend;

  // It's some symbol reference + a constant addend, but really
  // shouldn't use both Darwin and ELF syntax.
  return ELFRefKind == AArch64MCExpr::VK_INVALID ||
         DarwinRefKind == MCSymbolRefExpr::VK_None;
}

/// Force static initialization.
extern "C" void LLVMInitializeAArch64AsmParser() {
  RegisterMCAsmParser<AArch64AsmParser> X(TheAArch64leTarget);
  RegisterMCAsmParser<AArch64AsmParser> Y(TheAArch64beTarget);
  RegisterMCAsmParser<AArch64AsmParser> Z(TheARM64Target);
}

#define GET_REGISTER_MATCHER
#define GET_SUBTARGET_FEATURE_NAME
#define GET_MATCHER_IMPLEMENTATION
#include "AArch64GenAsmMatcher.inc"

// Define this matcher function after the auto-generated include so we
// have the match class enum definitions.
unsigned AArch64AsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                      unsigned Kind) {
  AArch64Operand &Op = static_cast<AArch64Operand &>(AsmOp);
  // If the kind is a token for a literal immediate, check if our asm
  // operand matches. This is for InstAliases which have a fixed-value
  // immediate in the syntax.
  int64_t ExpectedVal;
  switch (Kind) {
  default:
    return Match_InvalidOperand;
  case MCK__35_0:
    ExpectedVal = 0;
    break;
  case MCK__35_1:
    ExpectedVal = 1;
    break;
  case MCK__35_12:
    ExpectedVal = 12;
    break;
  case MCK__35_16:
    ExpectedVal = 16;
    break;
  case MCK__35_2:
    ExpectedVal = 2;
    break;
  case MCK__35_24:
    ExpectedVal = 24;
    break;
  case MCK__35_3:
    ExpectedVal = 3;
    break;
  case MCK__35_32:
    ExpectedVal = 32;
    break;
  case MCK__35_4:
    ExpectedVal = 4;
    break;
  case MCK__35_48:
    ExpectedVal = 48;
    break;
  case MCK__35_6:
    ExpectedVal = 6;
    break;
  case MCK__35_64:
    ExpectedVal = 64;
    break;
  case MCK__35_8:
    ExpectedVal = 8;
    break;
  }
  if (!Op.isImm())
    return Match_InvalidOperand;
  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Op.getImm());
  if (!CE)
    return Match_InvalidOperand;
  if (CE->getValue() == ExpectedVal)
    return Match_Success;
  return Match_InvalidOperand;
}


AArch64AsmParser::OperandMatchResultTy
AArch64AsmParser::tryParseGPRSeqPair(OperandVector &Operands)
{
  SMLoc S = getLoc();

  if (getParser().getTok().isNot(AsmToken::Identifier)) {
    //Error(S, "expected register");
    return MatchOperand_ParseFail;
  }

  int FirstReg = tryParseRegister();
  if (FirstReg == -1) {
    return MatchOperand_ParseFail;
  }
  const MCRegisterClass &WRegClass =
      AArch64MCRegisterClasses[AArch64::GPR32RegClassID];
  const MCRegisterClass &XRegClass =
      AArch64MCRegisterClasses[AArch64::GPR64RegClassID];

  bool isXReg = XRegClass.contains(FirstReg),
       isWReg = WRegClass.contains(FirstReg);
  if (!isXReg && !isWReg) {
    //Error(S, "expected first even register of a "
    //         "consecutive same-size even/odd register pair");
    return MatchOperand_ParseFail;
  }

  const MCRegisterInfo *RI = getContext().getRegisterInfo();
  unsigned FirstEncoding = RI->getEncodingValue(FirstReg);

  if (FirstEncoding & 0x1) {
    //Error(S, "expected first even register of a "
    //         "consecutive same-size even/odd register pair");
    return MatchOperand_ParseFail;
  }

  //SMLoc M = getLoc();
  if (getParser().getTok().isNot(AsmToken::Comma)) {
    //Error(M, "expected comma");
    return MatchOperand_ParseFail;
  }
  // Eat the comma
  getParser().Lex();

  //SMLoc E = getLoc();
  int SecondReg = tryParseRegister();
  if (SecondReg ==-1) {
    return MatchOperand_ParseFail;
  }

 if (RI->getEncodingValue(SecondReg) != FirstEncoding + 1 ||
      (isXReg && !XRegClass.contains(SecondReg)) ||
      (isWReg && !WRegClass.contains(SecondReg))) {
    //Error(E,"expected second odd register of a "
    //         "consecutive same-size even/odd register pair");
    return MatchOperand_ParseFail;
  }
  
  unsigned Pair = 0;
  if(isXReg) {
    Pair = RI->getMatchingSuperReg(FirstReg, AArch64::sube64,
           &AArch64MCRegisterClasses[AArch64::XSeqPairsClassRegClassID]);
  } else {
    Pair = RI->getMatchingSuperReg(FirstReg, AArch64::sube32,
           &AArch64MCRegisterClasses[AArch64::WSeqPairsClassRegClassID]);
  }

  Operands.push_back(AArch64Operand::CreateReg(Pair, false, S, getLoc(),
      getContext()));

  return MatchOperand_Success;
}
