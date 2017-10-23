/*
 * Copyright (C) 2015 Kernkonzept GmbH.
 * Author(s): Sarah Hoffmann <sarah.hoffmann@kernkonzept.com>
 *
 * This file is distributed under the terms of the GNU General Public
 * License, version 2.  Please see the COPYING-GPL-2 file for details.
 */
#pragma once

#include <l4/cxx/ref_ptr>
#include <l4/vbus/vbus>
#include <l4/l4virtio/l4virtio>

#include "cpc.h"
#include "cm.h"
#include "core_ic.h"
#include "debug.h"
#include "device_tree.h"
#include "generic_guest.h"
#include "cpu_dev_array.h"
#include "irq.h"
#include "vmprint.h"
#include "mips_instructions.h"

constexpr l4_addr_t sign_ext(l4_uint32_t addr)
{ return (l4_addr_t) ((l4_mword_t) ((l4_int32_t) addr)); }

namespace Vmm {

class Guest : public Generic_guest
{
  enum Hypcall_code
  {
    Hypcall_base     = 0x160,
    Hypcall_outchar  = Hypcall_base + 0,
  };

public:
  enum
  {
    Default_rambase = 0,
    Boot_offset = sign_ext(0x80000000)
  };

  Guest();
  cxx::Ref_ptr<Gic::Mips_core_ic> core_ic() const  { return _core_ic; }

  void setup_device_tree(Vdev::Device_tree dt);

  L4virtio::Ptr<void> load_linux_kernel(Ram_ds *ram, char const *kernel, l4_addr_t *entry);

  void prepare_linux_run(Vcpu_ptr vcpu, l4_addr_t entry,
                         Ram_ds *ram, char const *kernel,
                         char const *cmd_line, l4_addr_t dt_boot_addr);

  void run(cxx::Ref_ptr<Cpu_dev_array> const &cpus);

  int dispatch_hypcall(Hypcall_code hypcall_code, Vcpu_ptr vcpu);
  void handle_entry(Vcpu_ptr vcpu);

  void show_state_interrupts(FILE *f, Vcpu_ptr vcpu)
  {
    if (_core_ic)
      _core_ic->show_state(f, vcpu);
  }

  static Guest *create_instance();

private:
  int handle_gpsi_mfc0(Vcpu_ptr vcpu, Mips::Instruction insn)
  {
    l4_umword_t val;
    unsigned reg = (insn.rd() << 3) | (insn.func() & 0x7);

    trace().printf("MFC0 for 0x%x in register %d\n",
                   reg, (unsigned) insn.rt());

    switch (reg)
      {
      case L4_VM_CP0_GLOBAL_NUMBER:
        val = vcpu.get_vcpu_id() << 8;
        break;
      case L4_VM_CP0_PROC_ID:
        val = vcpu.proc_id();
        break;
      case L4_VM_CP0_SRS_CTL:
        val = 0;
        break;
      case L4_VM_CP0_CMGCR_BASE:
        val = Vdev::Coherency_manager::mem_region().start >> 4;
        break;
      case L4_VM_CP0_MAAR_0:
      case L4_VM_CP0_MAAR_1:
      case L4_VM_CP0_ERR_CTL:
      case L4_VM_CP0_CONFIG_6:
      case L4_VM_CP0_CONFIG_7:
        val = 0; break;
      default: return -L4_ENOSYS;
      }

    if (sizeof(l4_addr_t) == 4 || insn.rs() == Mips::Op::Cop0_dmf)
      vcpu->r.r[insn.rt()] = val;
    else
      vcpu->r.r[insn.rt()] = sign_ext((l4_uint32_t) val);
    return Jump_instr;
  }

  int handle_gpsi_mtc0(Vcpu_ptr vcpu, Mips::Instruction insn)
  {
    unsigned reg = (insn.rd() << 3) | (insn.func() & 0x7);

    trace().printf("MTC0 for 0x%x in register %u\n", reg, (unsigned) insn.rt());

    switch (reg)
      {
      case L4_VM_CP0_COUNT:
        {
          l4_uint32_t newcnt = vcpu->r.r[insn.rt()];
          l4_uint32_t kcnt;
          asm volatile("rdhwr\t%0, $2" : "=r"(kcnt)); // timer counter

          vcpu.state()->guest_timer_offset = (l4_int32_t) (newcnt - kcnt);
          vcpu.state()->set_modified(L4_VM_MOD_GTOFFSET);
          return Jump_instr;
        }
      case L4_VM_CP0_CONFIG_0:
      case L4_VM_CP0_CONFIG_1:
      case L4_VM_CP0_CONFIG_2:
      case L4_VM_CP0_CONFIG_3:
      case L4_VM_CP0_CONFIG_4:
      case L4_VM_CP0_CONFIG_5:
      case L4_VM_CP0_CONFIG_6:
      case L4_VM_CP0_CONFIG_7:
        return Jump_instr; // XXX config registers are read-only atm
      case L4_VM_CP0_MAAR_0: // XXX MAAR and parity are not supported
      case L4_VM_CP0_MAAR_1:
      case L4_VM_CP0_ERR_CTL:
      case L4_VM_CP0_TAG_LO_0: // cache tagging ignored
      case L4_VM_CP0_DATA_LO_0:
      case L4_VM_CP0_TAG_LO_1:
      case L4_VM_CP0_DATA_LO_1:
      case L4_VM_CP0_TAG_HI_0:
      case L4_VM_CP0_DATA_HI_0:
      case L4_VM_CP0_TAG_HI_1:
      case L4_VM_CP0_DATA_HI_1:
        return Jump_instr;
      }

    return -L4_EINVAL;
  }

  int handle_software_field_change(Vcpu_ptr vcpu, Mips::Instruction insn)
  {
    l4_umword_t val = vcpu->r.r[insn.rt()];
    unsigned reg = (insn.rd() << 3) | (insn.func() & 0x7);
    auto *s = vcpu.state();

    trace().printf("MTC0(soft) for 0x%x in register %d (0x%lx) \n",
                   reg, (unsigned) insn.rt(), val);

    switch (reg)
      {
      case L4_VM_CP0_STATUS:
        s->g_status = val;
        s->set_modified(L4_VM_MOD_STATUS);
        return Jump_instr;

      case L4_VM_CP0_CAUSE:
        enum { Cause_mask = 0x8c00ff00UL };
        s->get_state(L4_VM_MOD_CAUSE);
        s->g_cause &= ~Cause_mask;
        s->g_cause |= val & Cause_mask;
        s->set_modified(L4_VM_MOD_CAUSE);
        return Jump_instr;
      }

    return -L4_EINVAL;
  }

  int handle_wait(Vcpu_ptr vcpu, l4_utcb_t *utcb)
  {
    auto *s = vcpu.state();
    auto *kip = l4re_kip();

    l4_cpu_time_t kip_time;
    // get kip time and hardware in sync
    do
      {
        kip_time = l4_kip_clock(kip);
        s->update_state(L4_VM_MOD_CAUSE | L4_VM_MOD_COMPARE);

        if (s->g_cause & (1UL << 30))
          return Jump_instr; // there was a timer interrupt

        l4_mb();
      }
    while (kip_time != l4_kip_clock(kip));

    l4_uint32_t gcnt = s->saved_cause_timestamp
                       + (l4_int32_t) s->guest_timer_offset;
    l4_uint32_t diff;
    l4_uint32_t cmp = s->g_compare;
    if (gcnt < cmp)
      diff = cmp - gcnt;
    else
      diff = (0xffffffff - gcnt) + cmp;

    auto freq = kip->frequency_cpu / 2;
    diff = ((diff + freq - 1) / freq) * 1000;
    // make sure the timer interrupt has passed on the Fiasco clock tick
    diff += kip->scheduler_granularity;

    l4_timeout_t to;
    l4_rcv_timeout(l4_timeout_abs_u(kip_time + diff, 8, utcb), &to);

    wait_for_ipc(utcb, to);

    return Jump_instr;
  }

  Guest_print_buffer _hypcall_print;
  cxx::Ref_ptr<Gic::Mips_core_ic> _core_ic;
  cxx::Ref_ptr<Vdev::Coherency_manager> _cm;
  cxx::Ref_ptr<Vdev::Mips_cpc> _cpc;
};


} // namespace
