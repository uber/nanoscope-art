/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_COMPILER_UTILS_X86_ASSEMBLER_X86_H_
#define ART_COMPILER_UTILS_X86_ASSEMBLER_X86_H_

#include <vector>

#include "base/arena_containers.h"
#include "base/bit_utils.h"
#include "base/macros.h"
#include "constants_x86.h"
#include "globals.h"
#include "managed_register_x86.h"
#include "offsets.h"
#include "utils/array_ref.h"
#include "utils/assembler.h"

namespace art {
namespace x86 {

class Immediate : public ValueObject {
 public:
  explicit Immediate(int32_t value_in) : value_(value_in) {}

  int32_t value() const { return value_; }

  bool is_int8() const { return IsInt<8>(value_); }
  bool is_uint8() const { return IsUint<8>(value_); }
  bool is_int16() const { return IsInt<16>(value_); }
  bool is_uint16() const { return IsUint<16>(value_); }

 private:
  const int32_t value_;
};


class Operand : public ValueObject {
 public:
  uint8_t mod() const {
    return (encoding_at(0) >> 6) & 3;
  }

  Register rm() const {
    return static_cast<Register>(encoding_at(0) & 7);
  }

  ScaleFactor scale() const {
    return static_cast<ScaleFactor>((encoding_at(1) >> 6) & 3);
  }

  Register index() const {
    return static_cast<Register>((encoding_at(1) >> 3) & 7);
  }

  Register base() const {
    return static_cast<Register>(encoding_at(1) & 7);
  }

  int8_t disp8() const {
    CHECK_GE(length_, 2);
    return static_cast<int8_t>(encoding_[length_ - 1]);
  }

  int32_t disp32() const {
    CHECK_GE(length_, 5);
    int32_t value;
    memcpy(&value, &encoding_[length_ - 4], sizeof(value));
    return value;
  }

  bool IsRegister(Register reg) const {
    return ((encoding_[0] & 0xF8) == 0xC0)  // Addressing mode is register only.
        && ((encoding_[0] & 0x07) == reg);  // Register codes match.
  }

 protected:
  // Operand can be sub classed (e.g: Address).
  Operand() : length_(0), fixup_(nullptr) { }

  void SetModRM(int mod_in, Register rm_in) {
    CHECK_EQ(mod_in & ~3, 0);
    encoding_[0] = (mod_in << 6) | rm_in;
    length_ = 1;
  }

  void SetSIB(ScaleFactor scale_in, Register index_in, Register base_in) {
    CHECK_EQ(length_, 1);
    CHECK_EQ(scale_in & ~3, 0);
    encoding_[1] = (scale_in << 6) | (index_in << 3) | base_in;
    length_ = 2;
  }

  void SetDisp8(int8_t disp) {
    CHECK(length_ == 1 || length_ == 2);
    encoding_[length_++] = static_cast<uint8_t>(disp);
  }

  void SetDisp32(int32_t disp) {
    CHECK(length_ == 1 || length_ == 2);
    int disp_size = sizeof(disp);
    memmove(&encoding_[length_], &disp, disp_size);
    length_ += disp_size;
  }

  AssemblerFixup* GetFixup() const {
    return fixup_;
  }

  void SetFixup(AssemblerFixup* fixup) {
    fixup_ = fixup;
  }

 private:
  uint8_t length_;
  uint8_t encoding_[6];

  // A fixup can be associated with the operand, in order to be applied after the
  // code has been generated. This is used for constant area fixups.
  AssemblerFixup* fixup_;

  explicit Operand(Register reg) : fixup_(nullptr) { SetModRM(3, reg); }

  // Get the operand encoding byte at the given index.
  uint8_t encoding_at(int index_in) const {
    CHECK_GE(index_in, 0);
    CHECK_LT(index_in, length_);
    return encoding_[index_in];
  }

  friend class X86Assembler;
};


class Address : public Operand {
 public:
  Address(Register base_in, int32_t disp) {
    Init(base_in, disp);
  }

  Address(Register base_in, int32_t disp, AssemblerFixup *fixup) {
    Init(base_in, disp);
    SetFixup(fixup);
  }

  Address(Register base_in, Offset disp) {
    Init(base_in, disp.Int32Value());
  }

  Address(Register base_in, FrameOffset disp) {
    CHECK_EQ(base_in, ESP);
    Init(ESP, disp.Int32Value());
  }

  Address(Register base_in, MemberOffset disp) {
    Init(base_in, disp.Int32Value());
  }

  Address(Register index_in, ScaleFactor scale_in, int32_t disp) {
    CHECK_NE(index_in, ESP);  // Illegal addressing mode.
    SetModRM(0, ESP);
    SetSIB(scale_in, index_in, EBP);
    SetDisp32(disp);
  }

  Address(Register base_in, Register index_in, ScaleFactor scale_in, int32_t disp) {
    Init(base_in, index_in, scale_in, disp);
  }

  Address(Register base_in,
          Register index_in,
          ScaleFactor scale_in,
          int32_t disp, AssemblerFixup *fixup) {
    Init(base_in, index_in, scale_in, disp);
    SetFixup(fixup);
  }

  static Address Absolute(uintptr_t addr) {
    Address result;
    result.SetModRM(0, EBP);
    result.SetDisp32(addr);
    return result;
  }

  static Address Absolute(ThreadOffset<4> addr) {
    return Absolute(addr.Int32Value());
  }

 private:
  Address() {}

  void Init(Register base_in, int32_t disp) {
    if (disp == 0 && base_in != EBP) {
      SetModRM(0, base_in);
      if (base_in == ESP) SetSIB(TIMES_1, ESP, base_in);
    } else if (disp >= -128 && disp <= 127) {
      SetModRM(1, base_in);
      if (base_in == ESP) SetSIB(TIMES_1, ESP, base_in);
      SetDisp8(disp);
    } else {
      SetModRM(2, base_in);
      if (base_in == ESP) SetSIB(TIMES_1, ESP, base_in);
      SetDisp32(disp);
    }
  }

  void Init(Register base_in, Register index_in, ScaleFactor scale_in, int32_t disp) {
    CHECK_NE(index_in, ESP);  // Illegal addressing mode.
    if (disp == 0 && base_in != EBP) {
      SetModRM(0, ESP);
      SetSIB(scale_in, index_in, base_in);
    } else if (disp >= -128 && disp <= 127) {
      SetModRM(1, ESP);
      SetSIB(scale_in, index_in, base_in);
      SetDisp8(disp);
    } else {
      SetModRM(2, ESP);
      SetSIB(scale_in, index_in, base_in);
      SetDisp32(disp);
    }
  }
};


// This is equivalent to the Label class, used in a slightly different context. We
// inherit the functionality of the Label class, but prevent unintended
// derived-to-base conversions by making the base class private.
class NearLabel : private Label {
 public:
  NearLabel() : Label() {}

  // Expose the Label routines that we need.
  using Label::Position;
  using Label::LinkPosition;
  using Label::IsBound;
  using Label::IsUnused;
  using Label::IsLinked;

 private:
  using Label::BindTo;
  using Label::LinkTo;

  friend class x86::X86Assembler;

  DISALLOW_COPY_AND_ASSIGN(NearLabel);
};

/**
 * Class to handle constant area values.
 */
class ConstantArea {
 public:
  explicit ConstantArea(ArenaAllocator* arena) : buffer_(arena->Adapter(kArenaAllocAssembler)) {}

  // Add a double to the constant area, returning the offset into
  // the constant area where the literal resides.
  size_t AddDouble(double v);

  // Add a float to the constant area, returning the offset into
  // the constant area where the literal resides.
  size_t AddFloat(float v);

  // Add an int32_t to the constant area, returning the offset into
  // the constant area where the literal resides.
  size_t AddInt32(int32_t v);

  // Add an int32_t to the end of the constant area, returning the offset into
  // the constant area where the literal resides.
  size_t AppendInt32(int32_t v);

  // Add an int64_t to the constant area, returning the offset into
  // the constant area where the literal resides.
  size_t AddInt64(int64_t v);

  bool IsEmpty() const {
    return buffer_.size() == 0;
  }

  size_t GetSize() const {
    return buffer_.size() * elem_size_;
  }

  ArrayRef<const int32_t> GetBuffer() const {
    return ArrayRef<const int32_t>(buffer_);
  }

 private:
  static constexpr size_t elem_size_ = sizeof(int32_t);
  ArenaVector<int32_t> buffer_;
};

class X86Assembler FINAL : public Assembler {
 public:
  explicit X86Assembler(ArenaAllocator* arena) : Assembler(arena), constant_area_(arena) {}
  virtual ~X86Assembler() {}

  /*
   * Emit Machine Instructions.
   */
  void call(Register reg);
  void call(const Address& address);
  void call(Label* label);
  void call(const ExternalLabel& label);

  void pushl(Register reg);
  void pushl(const Address& address);
  void pushl(const Immediate& imm);

  void popl(Register reg);
  void popl(const Address& address);

  void movl(Register dst, const Immediate& src);
  void movl(Register dst, Register src);

  void movl(Register dst, const Address& src);
  void movl(const Address& dst, Register src);
  void movl(const Address& dst, const Immediate& imm);
  void movl(const Address& dst, Label* lbl);

  void movntl(const Address& dst, Register src);

  void bswapl(Register dst);

  void bsfl(Register dst, Register src);
  void bsfl(Register dst, const Address& src);
  void bsrl(Register dst, Register src);
  void bsrl(Register dst, const Address& src);

  void popcntl(Register dst, Register src);
  void popcntl(Register dst, const Address& src);

  void rorl(Register reg, const Immediate& imm);
  void rorl(Register operand, Register shifter);
  void roll(Register reg, const Immediate& imm);
  void roll(Register operand, Register shifter);

  void movzxb(Register dst, ByteRegister src);
  void movzxb(Register dst, const Address& src);
  void movsxb(Register dst, ByteRegister src);
  void movsxb(Register dst, const Address& src);
  void movb(Register dst, const Address& src);
  void movb(const Address& dst, ByteRegister src);
  void movb(const Address& dst, const Immediate& imm);

  void movzxw(Register dst, Register src);
  void movzxw(Register dst, const Address& src);
  void movsxw(Register dst, Register src);
  void movsxw(Register dst, const Address& src);
  void movw(Register dst, const Address& src);
  void movw(const Address& dst, Register src);
  void movw(const Address& dst, const Immediate& imm);

  void leal(Register dst, const Address& src);

  void cmovl(Condition condition, Register dst, Register src);
  void cmovl(Condition condition, Register dst, const Address& src);

  void setb(Condition condition, Register dst);

  void movaps(XmmRegister dst, XmmRegister src);
  void movss(XmmRegister dst, const Address& src);
  void movss(const Address& dst, XmmRegister src);
  void movss(XmmRegister dst, XmmRegister src);

  void movd(XmmRegister dst, Register src);
  void movd(Register dst, XmmRegister src);

  void addss(XmmRegister dst, XmmRegister src);
  void addss(XmmRegister dst, const Address& src);
  void subss(XmmRegister dst, XmmRegister src);
  void subss(XmmRegister dst, const Address& src);
  void mulss(XmmRegister dst, XmmRegister src);
  void mulss(XmmRegister dst, const Address& src);
  void divss(XmmRegister dst, XmmRegister src);
  void divss(XmmRegister dst, const Address& src);

  void movsd(XmmRegister dst, const Address& src);
  void movsd(const Address& dst, XmmRegister src);
  void movsd(XmmRegister dst, XmmRegister src);

  void psrlq(XmmRegister reg, const Immediate& shift_count);
  void punpckldq(XmmRegister dst, XmmRegister src);

  void movhpd(XmmRegister dst, const Address& src);
  void movhpd(const Address& dst, XmmRegister src);

  void psrldq(XmmRegister reg, const Immediate& shift_count);

  void addsd(XmmRegister dst, XmmRegister src);
  void addsd(XmmRegister dst, const Address& src);
  void subsd(XmmRegister dst, XmmRegister src);
  void subsd(XmmRegister dst, const Address& src);
  void mulsd(XmmRegister dst, XmmRegister src);
  void mulsd(XmmRegister dst, const Address& src);
  void divsd(XmmRegister dst, XmmRegister src);
  void divsd(XmmRegister dst, const Address& src);

  void cvtsi2ss(XmmRegister dst, Register src);
  void cvtsi2sd(XmmRegister dst, Register src);

  void cvtss2si(Register dst, XmmRegister src);
  void cvtss2sd(XmmRegister dst, XmmRegister src);

  void cvtsd2si(Register dst, XmmRegister src);
  void cvtsd2ss(XmmRegister dst, XmmRegister src);

  void cvttss2si(Register dst, XmmRegister src);
  void cvttsd2si(Register dst, XmmRegister src);

  void cvtdq2pd(XmmRegister dst, XmmRegister src);

  void comiss(XmmRegister a, XmmRegister b);
  void comisd(XmmRegister a, XmmRegister b);
  void ucomiss(XmmRegister a, XmmRegister b);
  void ucomiss(XmmRegister a, const Address& b);
  void ucomisd(XmmRegister a, XmmRegister b);
  void ucomisd(XmmRegister a, const Address& b);

  void roundsd(XmmRegister dst, XmmRegister src, const Immediate& imm);
  void roundss(XmmRegister dst, XmmRegister src, const Immediate& imm);

  void sqrtsd(XmmRegister dst, XmmRegister src);
  void sqrtss(XmmRegister dst, XmmRegister src);

  void xorpd(XmmRegister dst, const Address& src);
  void xorpd(XmmRegister dst, XmmRegister src);
  void xorps(XmmRegister dst, const Address& src);
  void xorps(XmmRegister dst, XmmRegister src);

  void andpd(XmmRegister dst, XmmRegister src);
  void andpd(XmmRegister dst, const Address& src);
  void andps(XmmRegister dst, XmmRegister src);
  void andps(XmmRegister dst, const Address& src);

  void orpd(XmmRegister dst, XmmRegister src);
  void orps(XmmRegister dst, XmmRegister src);

  void flds(const Address& src);
  void fstps(const Address& dst);
  void fsts(const Address& dst);

  void fldl(const Address& src);
  void fstpl(const Address& dst);
  void fstl(const Address& dst);

  void fstsw();

  void fucompp();

  void fnstcw(const Address& dst);
  void fldcw(const Address& src);

  void fistpl(const Address& dst);
  void fistps(const Address& dst);
  void fildl(const Address& src);
  void filds(const Address& src);

  void fincstp();
  void ffree(const Immediate& index);

  void fsin();
  void fcos();
  void fptan();
  void fprem();

  void xchgl(Register dst, Register src);
  void xchgl(Register reg, const Address& address);

  void cmpw(const Address& address, const Immediate& imm);

  void cmpl(Register reg, const Immediate& imm);
  void cmpl(Register reg0, Register reg1);
  void cmpl(Register reg, const Address& address);

  void cmpl(const Address& address, Register reg);
  void cmpl(const Address& address, const Immediate& imm);

  void testl(Register reg1, Register reg2);
  void testl(Register reg, const Immediate& imm);
  void testl(Register reg1, const Address& address);

  void andl(Register dst, const Immediate& imm);
  void andl(Register dst, Register src);
  void andl(Register dst, const Address& address);

  void orl(Register dst, const Immediate& imm);
  void orl(Register dst, Register src);
  void orl(Register dst, const Address& address);

  void xorl(Register dst, Register src);
  void xorl(Register dst, const Immediate& imm);
  void xorl(Register dst, const Address& address);

  void addl(Register dst, Register src);
  void addl(Register reg, const Immediate& imm);
  void addl(Register reg, const Address& address);

  void addl(const Address& address, Register reg);
  void addl(const Address& address, const Immediate& imm);

  void adcl(Register dst, Register src);
  void adcl(Register reg, const Immediate& imm);
  void adcl(Register dst, const Address& address);

  void subl(Register dst, Register src);
  void subl(Register reg, const Immediate& imm);
  void subl(Register reg, const Address& address);
  void subl(const Address& address, Register src);

  void cdq();

  void idivl(Register reg);

  void imull(Register dst, Register src);
  void imull(Register reg, const Immediate& imm);
  void imull(Register dst, Register src, const Immediate& imm);
  void imull(Register reg, const Address& address);

  void imull(Register reg);
  void imull(const Address& address);

  void mull(Register reg);
  void mull(const Address& address);

  void sbbl(Register dst, Register src);
  void sbbl(Register reg, const Immediate& imm);
  void sbbl(Register reg, const Address& address);
  void sbbl(const Address& address, Register src);

  void incl(Register reg);
  void incl(const Address& address);

  void decl(Register reg);
  void decl(const Address& address);

  void shll(Register reg, const Immediate& imm);
  void shll(Register operand, Register shifter);
  void shll(const Address& address, const Immediate& imm);
  void shll(const Address& address, Register shifter);
  void shrl(Register reg, const Immediate& imm);
  void shrl(Register operand, Register shifter);
  void shrl(const Address& address, const Immediate& imm);
  void shrl(const Address& address, Register shifter);
  void sarl(Register reg, const Immediate& imm);
  void sarl(Register operand, Register shifter);
  void sarl(const Address& address, const Immediate& imm);
  void sarl(const Address& address, Register shifter);
  void shld(Register dst, Register src, Register shifter);
  void shld(Register dst, Register src, const Immediate& imm);
  void shrd(Register dst, Register src, Register shifter);
  void shrd(Register dst, Register src, const Immediate& imm);

  void negl(Register reg);
  void notl(Register reg);

  void enter(const Immediate& imm);
  void leave();

  void ret();
  void ret(const Immediate& imm);

  void nop();
  void int3();
  void hlt();

  void j(Condition condition, Label* label);
  void j(Condition condition, NearLabel* label);
  void jecxz(NearLabel* label);

  void jmp(Register reg);
  void jmp(const Address& address);
  void jmp(Label* label);
  void jmp(NearLabel* label);

  void repne_scasw();
  void repe_cmpsw();
  void repe_cmpsl();
  void rep_movsw();

  void rdtsc();

  X86Assembler* lock();
  void cmpxchgl(const Address& address, Register reg);
  void cmpxchg8b(const Address& address);

  void mfence();

  X86Assembler* fs();
  X86Assembler* gs();

  //
  // Macros for High-level operations.
  //

  void AddImmediate(Register reg, const Immediate& imm);

  void LoadLongConstant(XmmRegister dst, int64_t value);
  void LoadDoubleConstant(XmmRegister dst, double value);

  void LockCmpxchgl(const Address& address, Register reg) {
    lock()->cmpxchgl(address, reg);
  }

  void LockCmpxchg8b(const Address& address) {
    lock()->cmpxchg8b(address);
  }

  //
  // Misc. functionality
  //
  int PreferredLoopAlignment() { return 16; }
  void Align(int alignment, int offset);
  void Bind(Label* label) OVERRIDE;
  void Jump(Label* label) OVERRIDE {
    jmp(label);
  }
  void Bind(NearLabel* label);

  //
  // Overridden common assembler high-level functionality
  //

  // Emit code that will create an activation on the stack
  void BuildFrame(size_t frame_size, ManagedRegister method_reg,
                  const std::vector<ManagedRegister>& callee_save_regs,
                  const ManagedRegisterEntrySpills& entry_spills) OVERRIDE;

  // Emit code that will remove an activation from the stack
  void RemoveFrame(size_t frame_size, const std::vector<ManagedRegister>& callee_save_regs)
      OVERRIDE;

  void IncreaseFrameSize(size_t adjust) OVERRIDE;
  void DecreaseFrameSize(size_t adjust) OVERRIDE;

  // Store routines
  void Store(FrameOffset offs, ManagedRegister src, size_t size) OVERRIDE;
  void StoreRef(FrameOffset dest, ManagedRegister src) OVERRIDE;
  void StoreRawPtr(FrameOffset dest, ManagedRegister src) OVERRIDE;

  void StoreImmediateToFrame(FrameOffset dest, uint32_t imm, ManagedRegister scratch) OVERRIDE;

  void StoreImmediateToThread32(ThreadOffset<4> dest, uint32_t imm, ManagedRegister scratch)
      OVERRIDE;

  void StoreStackOffsetToThread32(ThreadOffset<4> thr_offs, FrameOffset fr_offs,
                                  ManagedRegister scratch) OVERRIDE;

  void StoreStackPointerToThread32(ThreadOffset<4> thr_offs) OVERRIDE;

  void StoreSpanning(FrameOffset dest, ManagedRegister src, FrameOffset in_off,
                     ManagedRegister scratch) OVERRIDE;

  // Load routines
  void Load(ManagedRegister dest, FrameOffset src, size_t size) OVERRIDE;

  void LoadFromThread32(ManagedRegister dest, ThreadOffset<4> src, size_t size) OVERRIDE;

  void LoadRef(ManagedRegister dest, FrameOffset src) OVERRIDE;

  void LoadRef(ManagedRegister dest, ManagedRegister base, MemberOffset offs,
               bool unpoison_reference) OVERRIDE;

  void LoadRawPtr(ManagedRegister dest, ManagedRegister base, Offset offs) OVERRIDE;

  void LoadRawPtrFromThread32(ManagedRegister dest, ThreadOffset<4> offs) OVERRIDE;

  // Copying routines
  void Move(ManagedRegister dest, ManagedRegister src, size_t size) OVERRIDE;

  void CopyRawPtrFromThread32(FrameOffset fr_offs, ThreadOffset<4> thr_offs,
                              ManagedRegister scratch) OVERRIDE;

  void CopyRawPtrToThread32(ThreadOffset<4> thr_offs, FrameOffset fr_offs, ManagedRegister scratch)
      OVERRIDE;

  void CopyRef(FrameOffset dest, FrameOffset src, ManagedRegister scratch) OVERRIDE;

  void Copy(FrameOffset dest, FrameOffset src, ManagedRegister scratch, size_t size) OVERRIDE;

  void Copy(FrameOffset dest, ManagedRegister src_base, Offset src_offset, ManagedRegister scratch,
            size_t size) OVERRIDE;

  void Copy(ManagedRegister dest_base, Offset dest_offset, FrameOffset src, ManagedRegister scratch,
            size_t size) OVERRIDE;

  void Copy(FrameOffset dest, FrameOffset src_base, Offset src_offset, ManagedRegister scratch,
            size_t size) OVERRIDE;

  void Copy(ManagedRegister dest, Offset dest_offset, ManagedRegister src, Offset src_offset,
            ManagedRegister scratch, size_t size) OVERRIDE;

  void Copy(FrameOffset dest, Offset dest_offset, FrameOffset src, Offset src_offset,
            ManagedRegister scratch, size_t size) OVERRIDE;

  void MemoryBarrier(ManagedRegister) OVERRIDE;

  // Sign extension
  void SignExtend(ManagedRegister mreg, size_t size) OVERRIDE;

  // Zero extension
  void ZeroExtend(ManagedRegister mreg, size_t size) OVERRIDE;

  // Exploit fast access in managed code to Thread::Current()
  void GetCurrentThread(ManagedRegister tr) OVERRIDE;
  void GetCurrentThread(FrameOffset dest_offset, ManagedRegister scratch) OVERRIDE;

  // Set up out_reg to hold a Object** into the handle scope, or to be null if the
  // value is null and null_allowed. in_reg holds a possibly stale reference
  // that can be used to avoid loading the handle scope entry to see if the value is
  // null.
  void CreateHandleScopeEntry(ManagedRegister out_reg, FrameOffset handlescope_offset,
                              ManagedRegister in_reg, bool null_allowed) OVERRIDE;

  // Set up out_off to hold a Object** into the handle scope, or to be null if the
  // value is null and null_allowed.
  void CreateHandleScopeEntry(FrameOffset out_off, FrameOffset handlescope_offset,
                              ManagedRegister scratch, bool null_allowed) OVERRIDE;

  // src holds a handle scope entry (Object**) load this into dst
  void LoadReferenceFromHandleScope(ManagedRegister dst, ManagedRegister src) OVERRIDE;

  // Heap::VerifyObject on src. In some cases (such as a reference to this) we
  // know that src may not be null.
  void VerifyObject(ManagedRegister src, bool could_be_null) OVERRIDE;
  void VerifyObject(FrameOffset src, bool could_be_null) OVERRIDE;

  // Call to address held at [base+offset]
  void Call(ManagedRegister base, Offset offset, ManagedRegister scratch) OVERRIDE;
  void Call(FrameOffset base, Offset offset, ManagedRegister scratch) OVERRIDE;
  void CallFromThread32(ThreadOffset<4> offset, ManagedRegister scratch) OVERRIDE;

  // Generate code to check if Thread::Current()->exception_ is non-null
  // and branch to a ExceptionSlowPath if it is.
  void ExceptionPoll(ManagedRegister scratch, size_t stack_adjust) OVERRIDE;

  //
  // Heap poisoning.
  //

  // Poison a heap reference contained in `reg`.
  void PoisonHeapReference(Register reg) { negl(reg); }
  // Unpoison a heap reference contained in `reg`.
  void UnpoisonHeapReference(Register reg) { negl(reg); }
  // Unpoison a heap reference contained in `reg` if heap poisoning is enabled.
  void MaybeUnpoisonHeapReference(Register reg) {
    if (kPoisonHeapReferences) {
      UnpoisonHeapReference(reg);
    }
  }

  // Add a double to the constant area, returning the offset into
  // the constant area where the literal resides.
  size_t AddDouble(double v) { return constant_area_.AddDouble(v); }

  // Add a float to the constant area, returning the offset into
  // the constant area where the literal resides.
  size_t AddFloat(float v)   { return constant_area_.AddFloat(v); }

  // Add an int32_t to the constant area, returning the offset into
  // the constant area where the literal resides.
  size_t AddInt32(int32_t v) {
    return constant_area_.AddInt32(v);
  }

  // Add an int32_t to the end of the constant area, returning the offset into
  // the constant area where the literal resides.
  size_t AppendInt32(int32_t v) {
    return constant_area_.AppendInt32(v);
  }

  // Add an int64_t to the constant area, returning the offset into
  // the constant area where the literal resides.
  size_t AddInt64(int64_t v) { return constant_area_.AddInt64(v); }

  // Add the contents of the constant area to the assembler buffer.
  void AddConstantArea();

  // Is the constant area empty? Return true if there are no literals in the constant area.
  bool IsConstantAreaEmpty() const { return constant_area_.IsEmpty(); }

  // Return the current size of the constant area.
  size_t ConstantAreaSize() const { return constant_area_.GetSize(); }

 private:
  inline void EmitUint8(uint8_t value);
  inline void EmitInt32(int32_t value);
  inline void EmitRegisterOperand(int rm, int reg);
  inline void EmitXmmRegisterOperand(int rm, XmmRegister reg);
  inline void EmitFixup(AssemblerFixup* fixup);
  inline void EmitOperandSizeOverride();

  void EmitOperand(int rm, const Operand& operand);
  void EmitImmediate(const Immediate& imm);
  void EmitComplex(int rm, const Operand& operand, const Immediate& immediate);
  void EmitLabel(Label* label, int instruction_size);
  void EmitLabelLink(Label* label);
  void EmitLabelLink(NearLabel* label);

  void EmitGenericShift(int rm, const Operand& operand, const Immediate& imm);
  void EmitGenericShift(int rm, const Operand& operand, Register shifter);

  ConstantArea constant_area_;

  DISALLOW_COPY_AND_ASSIGN(X86Assembler);
};

inline void X86Assembler::EmitUint8(uint8_t value) {
  buffer_.Emit<uint8_t>(value);
}

inline void X86Assembler::EmitInt32(int32_t value) {
  buffer_.Emit<int32_t>(value);
}

inline void X86Assembler::EmitRegisterOperand(int rm, int reg) {
  CHECK_GE(rm, 0);
  CHECK_LT(rm, 8);
  buffer_.Emit<uint8_t>(0xC0 + (rm << 3) + reg);
}

inline void X86Assembler::EmitXmmRegisterOperand(int rm, XmmRegister reg) {
  EmitRegisterOperand(rm, static_cast<Register>(reg));
}

inline void X86Assembler::EmitFixup(AssemblerFixup* fixup) {
  buffer_.EmitFixup(fixup);
}

inline void X86Assembler::EmitOperandSizeOverride() {
  EmitUint8(0x66);
}

// Slowpath entered when Thread::Current()->_exception is non-null
class X86ExceptionSlowPath FINAL : public SlowPath {
 public:
  explicit X86ExceptionSlowPath(size_t stack_adjust) : stack_adjust_(stack_adjust) {}
  virtual void Emit(Assembler *sp_asm) OVERRIDE;
 private:
  const size_t stack_adjust_;
};

}  // namespace x86
}  // namespace art

#endif  // ART_COMPILER_UTILS_X86_ASSEMBLER_X86_H_
